<#
.SYNOPSIS
    Assembles the installer payload directory.

.DESCRIPTION
    Everything that lands on a user's disk, gathered in one place. This is split
    out of package.ps1 so the release workflow stages the same set of files the
    local build does. Two copies of this logic would drift, and the way that
    drift shows up is a release missing a DLL that every local build had.

    Signing is not done here. Payload members are extracted verbatim, so they
    must be signed before forging, but local builds sign through sign.ps1 and CI
    signs through the Azure action, and neither works in the other's
    environment. This script writes the list of files that need a signature to
    -OurBinariesList and lets the caller sign them.

    The onnxruntime CUDA provider is deliberately left out. It cannot load
    without cuDNN, which NVIDIA's licence does not let us redistribute, so
    shipping 313 MB of provider that will fail to initialise helps nobody.
    Whisper reaches the GPU through cuBLAS, which is included.
#>

param(
    [string]$Config = 'Release',
    [string]$BinDir,
    [string]$Payload,
    [string]$OurBinariesList
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BinDir)          { $BinDir          = Join-Path $RepoRoot "build\bin\$Config" }
if (-not $Payload)         { $Payload         = Join-Path $RepoRoot 'build\payload' }
if (-not $OurBinariesList) { $OurBinariesList = Join-Path $RepoRoot 'build\payload-ours.txt' }

if (-not (Test-Path $BinDir)) {
    Write-Error "Build output not found at $BinDir. Build the $Config configuration first."
    exit 1
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
    if ((Get-Item $real).Length -lt 1MB) {
        Write-Error "$tool resolved to a $((Get-Item $real).Length) byte shim, not the real binary."
        exit 1
    }
    Copy-Item $real (Join-Path $Payload "$tool.exe")
}

# whisper.cpp links cuBLAS as a hard import, so the program will not start
# without these even to run on CPU. CUDA_PATH is honoured first because a CI
# runner installs the toolkit wherever its action decides to put it.
$cudaRoots = @()
if ($env:CUDA_PATH) { $cudaRoots += (Join-Path $env:CUDA_PATH 'bin\x64'), (Join-Path $env:CUDA_PATH 'bin') }
$cudaRoots += Get-ChildItem 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA' -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    ForEach-Object { (Join-Path $_.FullName 'bin\x64'), (Join-Path $_.FullName 'bin') }

$cudaBin = $cudaRoots |
    Where-Object { $_ -and (Test-Path (Join-Path $_ 'cublas64_13.dll')) } |
    Select-Object -First 1

if (-not $cudaBin) {
    Write-Error 'CUDA redistributable DLLs not found. Looked for cublas64_13.dll under CUDA_PATH and the default toolkit location.'
    exit 1
}
foreach ($dll in 'cublas64_13.dll', 'cublasLt64_13.dll', 'cudart64_13.dll') {
    $src = Join-Path $cudaBin $dll
    if (-not (Test-Path $src)) {
        Write-Error "$dll not found in $cudaBin."
        exit 1
    }
    Copy-Item $src $Payload
}

Copy-Item (Join-Path $RepoRoot 'LICENSE') (Join-Path $Payload 'LICENSE.txt')

# Our own binaries, which are the only ones we may sign. Everything else in here
# belongs to Qt, NVIDIA, Microsoft or ffmpeg, and stamping our certificate on
# someone else's binary claims an authorship we do not have.
$ours = Get-ChildItem $Payload -File |
    Where-Object { $_.Extension -in '.exe', '.dll' } |
    Where-Object { $_.Name -notlike 'Qt6*' } |
    Where-Object { $_.Name -notlike 'onnxruntime*' } |
    Where-Object { $_.Name -notlike 'cublas*' } |
    Where-Object { $_.Name -notlike 'cudart*' } |
    Where-Object { $_.Name -notlike 'opengl32sw*' } |
    Where-Object { $_.Name -notlike 'D3Dcompiler*' } |
    Where-Object { $_.Name -notlike 'dxcompiler*' } |
    Where-Object { $_.Name -notlike 'dxil*' } |
    Where-Object { $_.BaseName -notin 'ffmpeg', 'ffprobe' } |
    ForEach-Object { $_.FullName }

if (-not $ours) {
    Write-Error 'No signable binaries found in the payload. The staging step produced nothing of ours.'
    exit 1
}
Set-Content -Path $OurBinariesList -Value $ours -Encoding UTF8

$size  = (Get-ChildItem $Payload -Recurse -File | Measure-Object Length -Sum).Sum
$count = (Get-ChildItem $Payload -Recurse -File).Count
Write-Host ("  {0} files, {1:N0} MB" -f $count, ($size / 1MB))
Write-Host ("  {0} of them ours, listed in {1}" -f @($ours).Count, $OurBinariesList)
