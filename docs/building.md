# Building ScribeEveryone

## Toolchain

| Component | Version used | Notes |
|---|---|---|
| Visual Studio | 2022, v17.14 | C++ desktop workload |
| CMake | 4.3 | 3.24 minimum |
| CUDA Toolkit | 13.3 | optional, `-DSCRIBE_CUDA=OFF` to skip |
| Qt | 6.8.3 msvc2022_64 | GUI only |
| ffmpeg / ffprobe | anything recent | runtime dependency, not a build one |

CMake fetches the rest and pins it in `cmake/Dependencies.cmake`: whisper.cpp,
sherpa-onnx (which drags in onnxruntime whether you wanted it or not), toml++,
and the SQLite amalgamation. Nothing to install by hand.

## Configure and build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release
```

Binaries in `build/bin/Release`.

| Option | Default | Effect |
|---|---|---|
| `SCRIBE_CUDA` | `ON` | GPU backends |
| `SCRIBE_BUILD_GUI` | `ON` | Qt6 app |
| `SCRIBE_BUILD_CLI` | `ON` | `scribe.exe` |

First configure clones several repos and downloads onnxruntime. First build
compiles ggml's CUDA kernels. There are 186 of them, nvcc processes them one at
a time at roughly 25 seconds each, and no, `-j` does not save you. Go get lunch.
Go outside. It'll still be going.

---

# The traps

Everything below is something that already ate somebody's afternoon. Reading it
costs five minutes. Rediscovering it costs considerably more.

## Do not put project settings at the top level

`CMakeLists.txt` sets no global `CMAKE_CXX_STANDARD`, no `add_compile_options`,
no `add_compile_definitions`, and includes the dependencies **before** it
defines anything of its own.

That is not fussiness. Global settings hit every target in the tree including
the fetched ones, and two of them detonate on contact:

- `simple-sentencepiece`, which sherpa-onnx drags in, uses `std::result_of`.
  C++20 deleted that. A global `CMAKE_CXX_STANDARD 20` fails its compile with a
  wall of template errors that look nothing whatsoever like "you set a language
  standard too high".
- A global `/MP` gets forwarded to nvcc by ggml's CUDA target and breaks its
  temp file handling.

Project flags go on the `scribe_flags` interface target. Only our targets link
it. Keep it that way.

## Everything rides the dynamic CRT

`SHERPA_ONNX_USE_STATIC_CRT` is forced `OFF`.

sherpa-onnx defaults to the static runtime. CMake, whisper.cpp and prebuilt Qt
all use the dynamic one. Mix them and you get several hundred `LNK2005`
duplicate symbol errors out of `libcpmt.lib`, which is a genuinely upsetting
thing to scroll through. Qt can't be rebuilt against `/MT` without, you know,
rebuilding Qt. So everything else moves to `/MD`.

## And `/NODEFAULTLIB:LIBCMT` on top of that

ggml enables OpenMP, which drags in MSVC's `VCOMP.lib`, which carries its own
`/DEFAULTLIB:LIBCMT` directive regardless of what every other object in the
build was compiled against. So you link `/MD`, everything looks fine, and you
get `LNK4098` and a quietly mixed CRT.

Excluding the static CRT explicitly is the documented fix and it's only safe
because everything else is already `/MD`.

When you inevitably need to find where a `LIBCMT` reference came from: scanning
`.lib` files for the string does not work. Don't bother. Ask the linker.

```powershell
target_link_options(scribe_cli PRIVATE /VERBOSE:LIB)
```

Whatever library it searched immediately before `LIBCMT.lib` is your guy.

## onnxruntime is repointed at CUDA 13 on purpose

`cmake/patch_sherpa_ort.cmake` rewrites sherpa-onnx's pinned onnxruntime archive
from the CUDA 12 build to the CUDA 13 build of the same version.

sherpa-onnx hardcodes CUDA 12. Its provider imports `cublas64_12` and
`cudart64_12`. whisper.cpp is on CUDA 13. Accept both and you ship two entire
CUDA runtimes side by side, which is several hundred megabytes of duplicate BLAS
so that two libraries can each have their own feelings about linear algebra. The
CUDA 13 build imports `cublas64_13` and `cudart64_13`, which already ship for
whisper, leaving only cuDNN and cuFFT to go find.

It runs as a `PATCH_COMMAND`, so it re-applies when the pinned sherpa tag moves,
and it fails the build **loudly** if upstream changes onnxruntime version rather
than silently sliding back to CUDA 12. When that fires, verify the new build
imports what you think:

```powershell
dumpbin /dependents build\bin\Release\onnxruntime_providers_cuda.dll
```

And do not try to sneak the CUDA 13 zip in under the CUDA 12 filename. sherpa
checks the hash. Ask me how I know.

## Probing for a DLL means actually loading it

`LOAD_LIBRARY_AS_DATAFILE` maps an image without resolving a single import.

It will cheerfully report `onnxruntime_providers_cuda.dll` as present and ready
on a machine that has none of its dependencies. Then onnxruntime loads it for
real, the dependency chain fails, and sherpa-onnx **aborts the entire process**
instead of falling back to CPU like a reasonable library.

That killed 42 of 43 files in a real batch. Every one of them reported the same
useless error. `onnx_cuda_available()` in `paths.cpp` now does a real
`LoadLibraryW`, which resolves the whole chain and answers the only question
that was ever worth asking: is this actually going to work.

## Share the dependency cache between build directories

```powershell
cmake -S . -B build-debug -DFETCHCONTENT_BASE_DIR="C:/path/to/ScribeEveryone/build/_deps"
```

Do this. Re-cloning sherpa-onnx and re-downloading onnxruntime for a second
build directory is several minutes of your life you don't get back.

## Layout

```
src/core/     scribe_core, static lib, knows nothing about Qt
src/cli/      scribe.exe
src/gui/      ScribeEveryone.exe, Qt6 Widgets
resources/    version resource and icon
tools/        icon generation, community-1 ONNX export
```

Core reports progress through `EventSink` in `src/core/events.hpp`. The CLI
implements it by printing. The GUI implements it by marshalling every event onto
the UI thread. `Pipeline::run()` blocks and must never be called on a UI thread,
which should be obvious and is written down anyway because obvious things get
done wrong at 2am.

## Runtime files do not live in the install directory

Models download into `%LOCALAPPDATA%\ScribeEveryone\models`. Not next to the
exe.

An install under Program Files is read-only. The database, the scratch
directory, all of it goes per-user. This was learned by shipping an installer
whose first launch immediately failed to create its own database, because the
Start Menu shortcut sets the working directory to the install directory and the
default paths were relative. The window came up completely empty behind an error
box. Very polished. Unset paths in `Config` now resolve to per-user locations; a
path you explicitly set resolves against the working directory.

## Packaging

```powershell
$env:AZURE_TENANT_ID = '...'
$env:AZURE_CLIENT_ID = '...'
$env:AZURE_CLIENT_SECRET = '...'
.\scripts\package.ps1
```

Stages the payload, signs it, forges `build\ScribeEveryone-Setup.exe`.
`-SkipSign` gives you a dev build.

**The script signs the payload before forging, and that ordering is the whole
point.** Payload members get extracted verbatim, so anything unsigned going in
stays unsigned on disk no matter how beautifully the installer itself is signed.
The script used to just print a warning about it. Warnings are things you scroll
past, which is how a signed installer shipped wrapping an entirely unsigned
payload. Now it signs them. The stub gets signed too, because the uninstaller is
extracted out of it at install time.

Third-party binaries are left alone. ffmpeg ships unsigned rather than wearing
our certificate, because stamping our name on somebody else's build claims an
authorship we don't have.

The installer is about 500 MB and roughly 490 MB of that is CUDA
redistributables. whisper.cpp links cuBLAS as a hard import, so the program will
not start without them even to run on CPU, and `cublasLt64_13.dll` alone is 442
MB. There is no clever trick here short of shipping a second CPU-only build.

cuDNN, cuFFT and onnxruntime's CUDA provider are deliberately not in there. They
get fetched at runtime from NVIDIA's public redistributable index and Microsoft's
releases, because cuDNN can't be redistributed under NVIDIA's licence and the
provider is 300 MB of dead weight without it.

`scripts/package.ps1` passes repository-relative paths to `lwforge` on purpose.
Forge's `long_path()` slaps `\\?\` on absolute paths, which disables Win32 path
normalisation, so an absolute `--config` makes it resolve `product.icon` to a
path still containing `..` and fail with `0x7b`. That one's a Forge bug and it's
written up here so the next person doesn't spend an hour on it.
