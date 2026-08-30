# Building ScribeEveryone

## Toolchain

| Component | Version used | Notes |
|---|---|---|
| Visual Studio | 2022, v17.14 | C++ desktop workload |
| CMake | 4.3 | 3.24 is the minimum |
| CUDA Toolkit | 13.3 | optional, `-DSCRIBE_CUDA=OFF` to skip |
| Qt | 6.8.3 msvc2022_64 | GUI only |
| ffmpeg / ffprobe | any recent | runtime dependency, not a build one |

Dependencies are fetched by CMake and pinned in `cmake/Dependencies.cmake`:
whisper.cpp, sherpa-onnx (which pulls onnxruntime), toml++ and the SQLite
amalgamation. Nothing needs installing by hand.

## Configure and build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release
```

Binaries land in `build/bin/Release`.

Options:

| Option | Default | Effect |
|---|---|---|
| `SCRIBE_CUDA` | `ON` | GPU backends for whisper.cpp and onnxruntime |
| `SCRIBE_BUILD_GUI` | `ON` | Qt6 desktop application |
| `SCRIBE_BUILD_CLI` | `ON` | `scribe.exe` |

The first configure clones several repositories and downloads onnxruntime, so
it takes a few minutes. The first build compiles ggml's CUDA kernels, which
takes considerably longer than everything else combined.

## Project settings do not go at the top level

`CMakeLists.txt` deliberately sets no global `CMAKE_CXX_STANDARD`, no
`add_compile_options` and no `add_compile_definitions`, and includes the
dependencies before defining anything. Global settings apply to every target in
the tree including the fetched ones, and two of them break immediately:

- `simple-sentencepiece`, pulled in by sherpa-onnx, uses `std::result_of`,
  which C++20 removed. A global `CMAKE_CXX_STANDARD 20` fails its compile.
- A global `/MP` is forwarded to nvcc by ggml's CUDA target and breaks its
  temporary file handling.

Project-wide flags belong on the `scribe_flags` interface target, which only
ScribeEveryone's own targets link against.

## Sharing a dependency cache between build directories

```powershell
cmake -S . -B build-debug -DFETCHCONTENT_BASE_DIR="C:/path/to/ScribeEveryone/build/_deps"
```

Worth doing. Re-fetching onnxruntime and re-cloning sherpa-onnx for a second
build directory is several minutes of nothing useful.

## Layout

```
src/core/     scribe_core, a static library with no Qt dependency
src/cli/      scribe.exe
src/gui/      ScribeEveryone.exe, Qt6 Widgets
resources/    version resource and icon
tools/        icon generation, community-1 ONNX export
```

The core library knows nothing about Qt and reports progress through
`EventSink` in `src/core/events.hpp`. The CLI implements it by printing, the
GUI by marshalling each event onto the UI thread. `Pipeline::run()` blocks and
must not be called on a UI thread.

## Runtime files

Models are downloaded on first use into `%LOCALAPPDATA%\ScribeEveryone\models`,
not next to the executable. An install under Program Files stays read-only, and
reinstalling does not discard several gigabytes of weights.

## Packaging

```powershell
$env:AZURE_TENANT_ID = '...'
$env:AZURE_CLIENT_ID = '...'
$env:AZURE_CLIENT_SECRET = '...'
.\scripts\package.ps1
```

Stages the payload, signs it, and forges `build\ScribeEveryone-Setup.exe` with
Forge. `-SkipSign` produces a development build instead.

Order matters. Payload members are extracted verbatim, so anything unsigned
going in stays unsigned on disk regardless of the installer's own signature.
The stub is signed before forging too, because the uninstaller is extracted
from it at install time.

The installer is around 500 MB, and roughly 490 MB of that is the CUDA
redistributables. whisper.cpp links cuBLAS as a hard import, so the program
will not start without them even to run on CPU, and `cublasLt64_13.dll` alone
is 442 MB.

Two things are deliberately left out:

- **onnxruntime's CUDA provider**, and **cuDNN**, and **cuFFT**. These are
  fetched at runtime instead, from NVIDIA's public redistributable index and
  Microsoft's releases, because cuDNN cannot be redistributed under NVIDIA's
  licence and the provider is useless without it. `scribe gpu install` pulls
  all three, about 1.2 GB, into `%LOCALAPPDATA%\ScribeEveryone\runtime`.

Whisper is unaffected, so GPU transcription works out of the box. Diarization,
voiceprints and isolation run on CPU until that runtime is installed, which
`paths.cpp` probes for at startup.

## onnxruntime is repointed at CUDA 13

`cmake/patch_sherpa_ort.cmake` rewrites sherpa-onnx's pinned onnxruntime
archive from the CUDA 12 build to the CUDA 13 build of the same version.

sherpa-onnx hardcodes CUDA 12, whose provider imports `cublas64_12` and
`cudart64_12`. whisper.cpp is on CUDA 13. Accepting both would mean shipping
two complete CUDA runtimes side by side, several hundred megabytes of duplicate
BLAS. The CUDA 13 build imports `cublas64_13` and `cudart64_13`, which already
ship for whisper, leaving only cuDNN and cuFFT to obtain.

It runs as a `PATCH_COMMAND`, so it re-applies whenever the pinned sherpa-onnx
tag moves, and fails the build loudly if upstream changes onnxruntime version
rather than silently reverting to CUDA 12. When that happens, check that the
new CUDA 13 build still imports `cublas64_13`:

```powershell
dumpbin /dependents build\bin\Release\onnxruntime_providers_cuda.dll
```

`scripts/package.ps1` passes repository-relative paths to `lwforge` on purpose.
Forge's `long_path()` adds the `\\?\` prefix to absolute paths, which disables
Win32 path normalisation, so an absolute `--config` makes it resolve
`product.icon` to a path still containing `..` and fail with `0x7b`.
