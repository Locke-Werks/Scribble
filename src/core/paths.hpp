#pragma once

#include <filesystem>

#include "config.hpp"

namespace scribble {

namespace fs = std::filesystem;

/// %LOCALAPPDATA%\Scribble on Windows, ~/.local/share/Scribble
/// elsewhere. Models live here rather than beside the exe so an install under
/// Program Files stays read-only and a reinstall does not discard several
/// gigabytes of downloaded weights.
fs::path app_data_dir();

fs::path default_model_dir();
fs::path default_config_path();

/// %USERPROFILE%\Documents, where transcripts go by default. Resolved through
/// the known folder API because the Documents folder is frequently redirected
/// to OneDrive or a network share.
fs::path documents_dir();

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

/// Provider for the small models that run over many short windows: speaker
/// segmentation and voiceprints.
///
/// These are launch-overhead bound, so the GPU is not automatically the right
/// answer. On a 16-core Ryzen 9 7950X against an RTX 4090, diarizing 5.5
/// minutes took 153s on the GPU and 112s on the CPU. On a four-core laptop the
/// same comparison inverts, because the CPU side of that result is the part
/// that scales with the machine.
///
/// Auto therefore decides from the CPU actually present rather than from
/// whether a GPU exists. It is a heuristic, and `onnx_accel` overrides it.
const char *onnx_small_model_provider(Accel requested);

/// Provider for large single-pass models, currently source separation. One
/// model over the whole recording keeps the GPU busy enough to win regardless
/// of the CPU: 38s against 113s on the machine above.
const char *onnx_large_model_provider(Accel requested);

}  // namespace scribble
