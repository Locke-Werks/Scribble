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
whisper.cpp, sherpa-onnx (which drags in onnxruntime), toml++ and the SQLite
amalgamation. Nothing needs installing by hand.

## Configure and build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release
```

Binaries land in `build/bin/Release`.

| Option | Default | Effect |
|---|---|---|
| `SCRIBE_CUDA` | `ON` | GPU backends for whisper.cpp and onnxruntime |
| `SCRIBE_BUILD_GUI` | `ON` | Qt6 desktop application |
| `SCRIBE_BUILD_CLI` | `ON` | `scribe.exe` |

The first configure clones several repositories and downloads onnxruntime, so
give it a few minutes. The first build compiles ggml's CUDA kernels, which takes
considerably longer than everything else combined. Go and do something else.
There are 186 of them and nvcc is not in a hurry.

---

Everything below is a trap somebody already fell into. Reading it is cheaper
than rediscovering it.

## Project settings do not go at the top level

`CMakeLists.txt` sets no global `CMAKE_CXX_STANDARD`, no `add_compile_options`
and no `add_compile_definitions`, and it includes the dependencies **before**
defining anything of its own. This is not style, it is load bearing.

Global settings apply to every target in the tree, including the fetched ones,
and two of them detonate immediately:

- `simple-sentencepiece`, pulled in by sherpa-onnx, uses `std::result_of`, which
  C++20 removed. A global `CMAKE_CXX_STANDARD 20` fails its compile with a wall
  of errors that look nothing like "you set the wrong standard".
- A global `/MP` gets forwarded to nvcc by ggml's CUDA target and breaks its
  temporary file handling.

Project-wide flags belong on the `scribe_flags` interface target, which only
ScribeEveryone's own targets link against.

## Everything is on the dynamic CRT

`SHERPA_ONNX_USE_STATIC_CRT` is forced `OFF`. sherpa-onnx defaults to the static
runtime while CMake, whisper.cpp and the prebuilt Qt binaries all use the
dynamic one. Mixing them fails at link with several hundred `LNK2005` duplicate
symbols out of `libcpmt.lib`. Qt cannot be rebuilt against `/MT` without
building Qt, so everything else moves to `/MD`.

## `/NODEFAULTLIB:LIBCMT` is not optional either

ggml enables OpenMP, which drags in MSVC's `VCOMP.lib`. That import library
carries its own `/DEFAULTLIB:LIBCMT` directive regardless of what everything
else was built against, so linking `/MD` produces `LNK4098` and a quietly mixed
CRT.

Excluding the static CRT explicitly is the documented remedy, and it is safe
here only because every other target is already on `/MD`.

If you ever need to find out where a `LIBCMT` reference is coming from, scanning
`.lib` files for the string does not work. Ask the linker:

```powershell
target_link_options(scribe_cli PRIVATE /VERBOSE:LIB)
```

The library searched immediately before `LIBCMT.lib` is your culprit.

## onnxruntime is repointed at CUDA 13

`cmake/patch_sherpa_ort.cmake` rewrites sherpa-onnx's pinned onnxruntime archive
from the CUDA 12 build to the CUDA 13 build of the same version.

sherpa-onnx hardcodes CUDA 12, whose provider imports `cublas64_12` and
`cudart64_12`. whisper.cpp is on CUDA 13. Accepting both means shipping two
complete CUDA runtimes side by side, which is several hundred megabytes of
duplicate BLAS for no benefit whatsoever. The CUDA 13 build imports
`cublas64_13` and `cudart64_13`, which already ship for whisper, leaving only
cuDNN and cuFFT to obtain.

It runs as a `PATCH_COMMAND`, so it re-applies whenever the pinned sherpa-onnx
tag moves, and it fails the build loudly if upstream changes onnxruntime version
rather than silently reverting to CUDA 12. When that happens, confirm the new
CUDA 13 build still imports what you think it does:

```powershell
dumpbin /dependents build\bin\Release\onnxruntime_providers_cuda.dll
```

Do not try to sneak the CUDA 13 archive in under the CUDA 12 filename. sherpa
verifies the hash.

## Probing for a DLL requires actually loading it

`LOAD_LIBRARY_AS_DATAFILE` maps an image without resolving a single import. It
will happily report `onnxruntime_providers_cuda.dll` as present on a machine
that has none of its dependencies, at which point onnxruntime loads it for real,
fails, and sherpa-onnx **aborts the process** rather than degrading to CPU.

That cost an entire 43 file batch once. `onnx_cuda_available()` in `paths.cpp`
now does a real `LoadLibraryW`, which resolves the whole chain and answers the
only question worth asking: will this actually come up.

## Sharing a dependency cache between build directories

```powershell
cmake -S . -B build-debug -DFETCHCONTENT_BASE_DIR="C:/path/to/ScribeEveryone/build/_deps"
```

Worth doing. Re-fetching onnxruntime and re-cloning sherpa-onnx for a second
build directory is several minutes of nothing useful happening.

## Layout

```
src/core/     scribe_core, a static library with no Qt dependency
src/cli/      scribe.exe
src/gui/      ScribeEveryone.exe, Qt6 Widgets
resources/    version resource and icon
tools/        icon generation, community-1 ONNX export
```

The core library knows nothing about Qt and reports progress through `EventSink`
in `src/core/events.hpp`. The CLI implements it by printing, the GUI by
marshalling each event onto the UI thread. `Pipeline::run()` blocks and must
never be called on a UI thread.

## Runtime files

Models download on first use into `%LOCALAPPDATA%\ScribeEveryone\models`, not
next to the executable. An install under Program Files stays read-only, and
reinstalling does not cost you several gigabytes of downloads again.

The same logic applies to the database and the scratch directory. Nothing the
program writes at runtime lives in the install directory, because the install
directory is not writable and finding that out through a failed first launch is
a bad experience. Unset paths in `Config` resolve to per-user locations; a path
you explicitly configure resolves against the working directory.

## Packaging

```powershell
$env:AZURE_TENANT_ID = '...'
$env:AZURE_CLIENT_ID = '...'
$env:AZURE_CLIENT_SECRET = '...'
.\scripts\package.ps1
```

Stages the payload, signs it, and forges `build\ScribeEveryone-Setup.exe` with
Forge. `-SkipSign` produces a development build instead.

**Order matters and the script enforces it.** Payload members are extracted
verbatim, so anything unsigned going in stays unsigned on disk no matter how
thoroughly the installer itself is signed. The script signs them before forging
rather than warning about it afterwards, because a warning is something you
scroll past. The stub gets signed too, since the uninstaller is extracted from
it at install time.

Third-party binaries are left alone. ffmpeg ships unsigned rather than wearing
our certificate, because stamping our name on someone else's build claims an
authorship we do not have.

The installer is around 500 MB, and roughly 490 MB of that is CUDA
redistributables. whisper.cpp links cuBLAS as a hard import, so the program will
not start without them even to run on CPU, and `cublasLt64_13.dll` alone is 442
MB. There is no clever way around this short of shipping a second CPU-only
build.

cuDNN, cuFFT and onnxruntime's CUDA provider are deliberately **not** in the
installer. They are fetched at runtime from NVIDIA's public redistributable
index and Microsoft's releases, because cuDNN cannot be redistributed under
NVIDIA's licence and the provider is dead weight without it.

`scripts/package.ps1` passes repository-relative paths to `lwforge` on purpose.
Forge's `long_path()` adds the `\\?\` prefix to absolute paths, which disables
Win32 path normalisation, so an absolute `--config` makes it resolve
`product.icon` to a path still containing `..` and fail with `0x7b`.
