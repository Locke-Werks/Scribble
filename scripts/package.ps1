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
    [switch]$SkipSign
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BinDir   = Join-Path $RepoRoot "build\bin\$Config"
$Payload  = Join-Path $RepoRoot 'build\payload'
$OutFile  = Join-Path $RepoRoot 'build\ScribeEveryone-Setup.exe'
$Toml     = Join-Path $RepoRoot 'installer\installer.toml'

if (-not (Test-Path $BinDir)) {
    Write-Error "Build output not found at $BinDir. Build the Release configuration first."
    exit 1
}

$Stub = Get-ChildItem '..\Forge\build' -Recurse -Filter 'lwstub.exe' `
    -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notlike '*Debug*' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

$Forge = Get-ChildItem '..\Forge\build' -Recurse -Filter 'lwforge.exe' `
    -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notlike '*Debug*' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

foreach ($tool in @($Stub, $Forge)) {
    if (-not $tool) {
        Write-Error 'lwstub.exe or lwforge.exe not found. Build Forge first.'
        exit 1
    }
}

Write-Host "Staging payload" -ForegroundColor Cyan
Remove-Item $Payload -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $Payload | Out-Null

# Program, its own libraries, and the Qt runtime windeployqt already staged.
$exclude = @('onnxruntime_providers_cuda.dll', 'onnxruntime_providers_tensorrt.dll')
Get-ChildItem $BinDir -File |
    Where-Object { $_.Extension -in '.exe', '.dll' } |
    Where-Object { $_.Name -notin $exclude } |
    ForEach-Object { Copy-Item $_.FullName $Payload }

foreach ($dir in 'platforms', 'styles', 'imageformats', 'iconengines', 'tls',
                 'networkinformation', 'generic') {
    $src = Join-Path $BinDir $dir
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $Payload $dir) -Recurse
    }
}

# ffmpeg is a hard runtime dependency for every input file, so it ships rather
# than being left to PATH. paths.cpp looks beside the executable first.
foreach ($tool in 'ffmpeg', 'ffprobe') {
    $found = Get-Command $tool -ErrorAction SilentlyContinue
    if (-not $found) {
        Write-Error "$tool not found on PATH."
        exit 1
    }
    $real = $found.Source
    # Chocolatey installs shims that are a few kilobytes and only work with the
    # chocolatey layout present, so the real binary is resolved behind them.
    if ((Get-Item $real).Length -lt 1MB) {
        $candidate = Get-ChildItem 'C:\ProgramData\chocolatey\lib' -Recurse -Filter "$tool.exe" `
            -ErrorAction SilentlyContinue |
            Sort-Object Length -Descending | Select-Object -First 1
        if ($candidate) { $real = $candidate.FullName }
    }
    Copy-Item $real (Join-Path $Payload "$tool.exe")
}

# whisper.cpp links cuBLAS as a hard import, so the program will not start
# without these even to run on CPU.
$cudaBin = Get-ChildItem 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA' -Directory |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'bin\x64' } |
    Where-Object { Test-Path (Join-Path $_ 'cublas64_13.dll') } |
    Select-Object -First 1

if (-not $cudaBin) {
    Write-Error 'CUDA redistributable DLLs not found.'
    exit 1
}
foreach ($dll in 'cublas64_13.dll', 'cublasLt64_13.dll', 'cudart64_13.dll') {
    Copy-Item (Join-Path $cudaBin $dll) $Payload
}

Copy-Item (Join-Path $RepoRoot 'LICENSE') (Join-Path $Payload 'LICENSE.txt')

$size = (Get-ChildItem $Payload -Recurse -File | Measure-Object Length -Sum).Sum
$count = (Get-ChildItem $Payload -Recurse -File).Count
Write-Host ("  {0} files, {1:N0} MB" -f $count, ($size / 1MB))

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
    if ($LASTEXITCODE -ne 0) {
        Write-Error 'Signing the stub failed.'
        exit 1
    }
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
                   '--out',     'build\ScribeEveryone-Setup.exe')
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
