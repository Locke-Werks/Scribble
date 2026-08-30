# Repoints sherpa-onnx at the CUDA 13 onnxruntime build.
#
# sherpa-onnx hardcodes the CUDA 12 archive, whose provider imports cublas64_12
# and cudart64_12. whisper.cpp is on CUDA 13, so taking that as given would mean
# shipping two complete CUDA runtimes side by side. The CUDA 13 build of the
# same onnxruntime version imports cublas64_13 and cudart64_13, which are
# already shipped for whisper, leaving only cuDNN and cuFFT to obtain.
#
# Run as a PATCH_COMMAND so it applies once, at populate time, and re-applies
# automatically whenever the pinned sherpa-onnx tag changes.

set(target "${DIR}/cmake/onnxruntime-win-x64-gpu.cmake")

if(NOT EXISTS "${target}")
    message(FATAL_ERROR "cannot patch, not found: ${target}")
endif()

file(READ "${target}" contents)

if(contents MATCHES "gpu_cuda13")
    return()
endif()

string(REPLACE
    "onnxruntime-win-x64-gpu_cuda12-1.27.1"
    "onnxruntime-win-x64-gpu_cuda13-1.27.1"
    contents "${contents}")

string(REPLACE
    "78d4de5ab262f79ac5dd59f08ff0d049b1cea605497f375f8df5ba1a52f26111"
    "1a88828fb1ca78c9920637acf0f8d8ce40a854bef874128c55193e3d1a2b9ef8"
    contents "${contents}")

if(NOT contents MATCHES "gpu_cuda13")
    message(FATAL_ERROR
        "patch did not apply: sherpa-onnx changed its pinned onnxruntime version. "
        "Update cmake/patch_sherpa_ort.cmake to match, and re-check that the "
        "CUDA 13 build still imports cublas64_13 rather than cublas64_12.")
endif()

file(WRITE "${target}" "${contents}")
message(STATUS "sherpa-onnx repointed at the CUDA 13 onnxruntime build")
