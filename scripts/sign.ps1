<#
.SYNOPSIS
    Signs ScribeEveryone binaries with Azure Trusted Signing.

.DESCRIPTION
    Signs every file given, or the whole release output when called with no
    arguments. Both executables and the sherpa-onnx DLLs are signed, because
    the installer extracts payload members verbatim: an unsigned DLL going in
    stays unsigned on disk no matter how the installer itself is signed.

    Authentication is a plain Entra app registration with a client secret.
    AZURE_TENANT_ID, AZURE_CLIENT_ID and AZURE_CLIENT_SECRET are the whole of
    it. There is no federated credential, no workload identity and no OIDC
    subject to configure, and signing/metadata.json excludes every other
    credential type so the chain cannot wander somewhere unprovisioned and
    produce an error that points nowhere useful.

    The certificate identifies Specter Point Intelligence, LLC, the parent
    organisation. That is expected, not a mis-signing.

.EXAMPLE
    .\scripts\sign.ps1
    .\scripts\sign.ps1 build\bin\Release\scribe.exe
#>

param(
    # lwforge invokes this as `sign.ps1 -FilePath "<installer>"`, so the alias
    # is load-bearing: without it the --sign step fails with an unbound
    # parameter after the container has already been written.
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [Alias('FilePath')]
    [string[]]$Paths
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot     = Split-Path -Parent $PSScriptRoot
$MetadataJson = Join-Path $RepoRoot 'signing\metadata.json'
$Dlib         = Join-Path $env:LOCALAPPDATA 'Microsoft\MicrosoftArtifactSigningClientTools\Azure.CodeSigning.Dlib.dll'

# Discovered newest first rather than pinned. A pinned SDK path fails as
# "not found" on the next SDK release instead of saying the SDK moved.
$SignTool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Directory `
    -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^10\.' } |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
    Where-Object { Test-Path $_ } |
    Select-Object -First 1

if (-not $SignTool) {
    Write-Error 'signtool.exe not found. Install the Windows SDK signing tools.'
    exit 1
}

foreach ($envVar in @('AZURE_TENANT_ID', 'AZURE_CLIENT_ID', 'AZURE_CLIENT_SECRET')) {
    if (-not [Environment]::GetEnvironmentVariable($envVar)) {
        Write-Error "Environment variable $envVar is not set."
        exit 1
    }
}

foreach ($path in @($Dlib, $SignTool, $MetadataJson)) {
    if (-not (Test-Path $path)) {
        Write-Error "Not found: $path"
        exit 1
    }
}

if (-not $Paths -or $Paths.Count -eq 0) {
    $releaseDir = Join-Path $RepoRoot 'build\bin\Release'
    if (-not (Test-Path $releaseDir)) {
        Write-Error "No paths given and $releaseDir does not exist."
        exit 1
    }
    # Qt's DLLs arrive signed by the Qt Company and onnxruntime's by Microsoft.
    # Re-signing either replaces a valid upstream signature with ours for no
    # benefit, and the CUDA provider alone is over 300 MB to rewrite.
    $Paths = Get-ChildItem $releaseDir -File |
        Where-Object { $_.Extension -in '.exe', '.dll' } |
        Where-Object { $_.Name -notlike 'Qt6*' } |
        Where-Object { $_.Name -notlike 'onnxruntime*' } |
        Where-Object { $_.Name -notlike 'opengl32sw*' } |
        Where-Object { $_.Name -notlike 'D3Dcompiler*' } |
        Where-Object { $_.Name -notlike 'dxcompiler*' } |
        Where-Object { $_.Name -notlike 'dxil*' } |
        ForEach-Object { $_.FullName }
}

$failed = @()
foreach ($path in $Paths) {
    if (-not (Test-Path $path)) {
        Write-Error "Not found: $path"
        exit 1
    }
    $resolved = (Resolve-Path $path).Path
    Write-Host "--- Signing $resolved ---" -ForegroundColor Cyan

    & $SignTool sign /fd SHA256 /tr http://timestamp.acs.microsoft.com /td SHA256 `
        /dlib $Dlib /dmdf $MetadataJson $resolved
    if ($LASTEXITCODE -ne 0) {
        $failed += $resolved
        continue
    }

    & $SignTool verify /pa /q $resolved
    if ($LASTEXITCODE -ne 0) {
        $failed += $resolved
    }
}

if ($failed.Count -gt 0) {
    Write-Host ''
    Write-Error ("Signing failed for:`n  " + ($failed -join "`n  "))
    exit 1
}

Write-Host "`nSigned and verified $($Paths.Count) file(s)." -ForegroundColor Green
