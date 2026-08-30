#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "events.hpp"

namespace scribe {

namespace fs = std::filesystem;

/// Optional GPU acceleration for the onnxruntime stages: diarization,
/// voiceprints, source separation and Parakeet.
///
/// These need cuDNN, which NVIDIA's licence does not allow us to redistribute,
/// so it is fetched from NVIDIA on the user's behalf rather than shipped. The
/// same applies to cuFFT and to onnxruntime's CUDA provider, which is too large
/// to justify installing for machines that will never have cuDNN beside it.
///
/// Whisper is unaffected. It reaches the GPU through cuBLAS, which does ship.
struct GpuComponent {
    std::string name;
    std::string url;
    std::string sha256;       ///< empty when the source does not publish one
    std::string archive_glob; ///< bsdtar pattern selecting the members to keep
    std::vector<std::string> files;  ///< DLLs that must end up in the runtime dir
    std::int64_t download_bytes = 0;
};

struct GpuRuntimeStatus {
    bool ready = false;
    /// Components with at least one file missing, in install order.
    std::vector<GpuComponent> missing;
    std::int64_t download_bytes = 0;

    std::string summary() const;
};

/// Where downloaded GPU libraries live. Kept out of the install directory so an
/// install under Program Files stays read-only and an uninstall does not
/// silently discard a 1.6 GB download.
fs::path gpu_runtime_dir();

const std::vector<GpuComponent> &gpu_components();

/// Checks the runtime directory and the system search path.
GpuRuntimeStatus gpu_runtime_status();

/// Puts the runtime directory on the process search path so the loader can
/// resolve cuDNN when onnxruntime pulls in its CUDA provider. Call before any
/// ONNX engine is created. Safe to call more than once.
void activate_gpu_runtime();

using GpuProgress =
    std::function<void(const std::string &component, std::int64_t done, std::int64_t total)>;

/// Downloads and unpacks everything reported missing. Returns false and fills
/// `error` on the first component that fails; components already present are
/// skipped, so a resumed install does not refetch what it already has.
bool install_gpu_runtime(const GpuRuntimeStatus &status, const GpuProgress &progress,
                         const CancelToken &cancel, std::string *error);

}  // namespace scribe
