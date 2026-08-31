<#
.SYNOPSIS
    Stages the installer payload and forges Setup.exe.

.DESCRIPTION
    Assembles everything the installed program needs into one directory, then
    hands it to lwforge. Payload members are extracted verbatim, so anything
    that should be signed must already be signed before this runs: signing the
    installer does nothing for the files inside it.

    The onnxruntime CUDA provider is deliberately left out. It cannot load
    without cuDNN, which NVIDIA's licence does not let us redistribute, so
    shipping 313 MB of provider that will fail to initialise helps nobody.
    Diarization, voiceprints, isolation and Parakeet run on CPU unless the user
    installs cuDNN themselves. Whisper reaches the GPU through cuBLAS, which is
    included, so the throughput that matters most is unaffected.

.EXAMPLE
    .\scripts\package.ps1
    .\scripts\package.ps1 -SkipSign
#>

param(
    [string]$Config = 'Release',
    [switch]$SkipSign,
    # Where a local Forge build tree lives. Only needed for local packaging; CI
    # takes the signed lwforge.exe and lwstub.exe from the Forge release
    # instead. See .github/workflows/release.yml.
    [string]$ForgeRoot = $env:FORGE_ROOT
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BinDir   = Join-Path $RepoRoot "build\bin\$Config"
$Payload  = Join-Path $RepoRoot 'build\payload'
$OutFile  = Join-Path $RepoRoot 'build\Scribble-Setup.exe'
$Toml     = Join-Path $RepoRoot 'installer\installer.toml'

if (-not (Test-Path $BinDir)) {
    Write-Error "Build output not found at $BinDir. Build the Release configuration first."
    exit 1
}

if (-not $ForgeRoot) {
    # Checked out beside this repo is the usual arrangement. Set FORGE_ROOT or
    # pass -ForgeRoot if Forge lives somewhere else.
    $ForgeRoot = Join-Path (Split-Path -Parent $RepoRoot) 'Forge'
}
if (-not (Test-Path $ForgeRoot)) {
    Write-Error "Forge not found at $ForgeRoot. Set FORGE_ROOT or pass -ForgeRoot."
    exit 1
}

$Stub = Get-ChildItem $ForgeRoot -Recurse -Filter 'lwstub.exe' `
    -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notlike '*Debug*' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

$Forge = Get-ChildItem $ForgeRoot -Recurse -Filter 'lwforge.exe' `
    -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notlike '*Debug*' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

foreach ($tool in @($Stub, $Forge)) {
    if (-not $tool) {
        Write-Error "lwstub.exe or lwforge.exe not found under $ForgeRoot. Build Forge first."
        exit 1
    }
}

# Staging lives in its own script so the release workflow assembles the same
# payload this does. See scripts/stage-payload.ps1.
#
# None of the calls to our own scripts below check $LASTEXITCODE. A .ps1 only
# sets it by calling `exit`, so on success it keeps whatever the session
# already had, which is unset in a fresh shell, and `$null -ne 0` is true. Every
# script here sets $ErrorActionPreference = 'Stop', so a real failure arrives as
# a terminating error and stops this script without a guard.
$OursList = Join-Path $RepoRoot 'build\payload-ours.txt'
& (Join-Path $PSScriptRoot 'stage-payload.ps1') `
    -Config $Config -BinDir $BinDir -Payload $Payload -OurBinariesList $OursList

# Payload members are extracted verbatim, so they must be signed here, before
# forging. Signing the installer does nothing for the files inside it, and a
# rebuild produces unsigned binaries every time, so this cannot be left to a
# separate step that someone remembers to run.
if (-not $SkipSign) {
    $ours = @(Get-Content $OursList | Where-Object { $_ -and $_.Trim() })
    if ($ours) {
        & (Join-Path $PSScriptRoot 'sign.ps1') @ours
    }
}

$unsigned = Get-ChildItem $Payload -Recurse -File -Include '*.exe', '*.dll' |
    Where-Object { (Get-AuthenticodeSignature $_.FullName).Status -ne 'Valid' } |
    ForEach-Object { $_.Name }
if ($unsigned) {
    Write-Host "  unsigned members: $($unsigned -join ', ')" -ForegroundColor Yellow
}

Write-Host "`nForging installer" -ForegroundColor Cyan

# The uninstaller is extracted from the stub at install time, so a stub that was
# not signed before forging produces a valid installer with an unsigned
# uninstaller. Signing a working copy leaves the Forge build tree alone.
$StubCopy = Join-Path $RepoRoot 'build\lwstub-signed.exe'
Copy-Item $Stub.FullName $StubCopy -Force
if (-not $SkipSign) {
    & (Join-Path $PSScriptRoot 'sign.ps1') $StubCopy
}

# Paths are passed relative to the repository root on purpose. lwforge resolves
# product.icon against the config file, and that resolution fails with 0x7b when
# --config is absolute. Reported against Forge; until it is fixed, running from
# the root with relative paths is the reliable form.
Push-Location $RepoRoot
try {
    $forgeArgs = @('build',
                   '--config',  'installer\installer.toml',
                   '--payload', 'build\payload',
                   '--stub',    'build\lwstub-signed.exe',
                   '--out',     'build\Scribble-Setup.exe')
    if (-not $SkipSign) { $forgeArgs += '--sign' } else { $forgeArgs += '--dev' }

    & $Forge.FullName @forgeArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "lwforge failed with exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}

$out = Get-Item $OutFile
Write-Host ("`n{0}  {1:N0} MB" -f $out.FullName, ($out.Length / 1MB)) -ForegroundColor Green
Write-Host ("signature: {0}" -f (Get-AuthenticodeSignature $OutFile).Status)
