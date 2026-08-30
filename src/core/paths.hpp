#pragma once

#include <filesystem>

namespace scribe {

namespace fs = std::filesystem;

/// %LOCALAPPDATA%\ScribeEveryone on Windows, ~/.local/share/ScribeEveryone
/// elsewhere. Models live here rather than beside the exe so an install under
/// Program Files stays read-only and a reinstall does not discard several
/// gigabytes of downloaded weights.
fs::path app_data_dir();

fs::path default_model_dir();
fs::path default_config_path();

/// Locates ffmpeg and ffprobe. Prefers the copies shipped beside the exe, then
/// falls back to PATH. Returns an empty path when neither is present.
fs::path find_ffmpeg();
fs::path find_ffprobe();

/// Directory holding the running executable.
fs::path executable_dir();

/// Whether onnxruntime's CUDA execution provider can actually load.
///
/// It needs cuDNN, which the CUDA toolkit does not install and the driver does
/// not ship. When it is absent sherpa-onnx does not degrade to CPU, it aborts
/// the process, so the provider has to be chosen by probing rather than by
/// asking for CUDA and handling failure. whisper.cpp is unaffected: it goes
/// through cuBLAS and needs no cuDNN.
bool onnx_cuda_available();

}  // namespace scribe
