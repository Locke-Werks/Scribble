#include "paths.hpp"

#include <cstdlib>
#include <thread>

#include "gpu_runtime.hpp"
#include "util.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace scribe {
namespace {

#ifdef _WIN32
constexpr const char *kFfmpegName = "ffmpeg.exe";
constexpr const char *kFfprobeName = "ffprobe.exe";
constexpr char kPathSep = ';';
#else
constexpr const char *kFfmpegName = "ffmpeg";
constexpr const char *kFfprobeName = "ffprobe";
constexpr char kPathSep = ':';
#endif

fs::path search_path(const char *name) {
    // A copy beside the exe wins, so an installed build is not at the mercy of
    // whatever ffmpeg happens to be on the machine's PATH.
    std::error_code ec;
    fs::path local = executable_dir() / name;
    if (fs::exists(local, ec)) {
        return local;
    }
    local = executable_dir() / "ffmpeg" / name;
    if (fs::exists(local, ec)) {
        return local;
    }

    const char *env = std::getenv("PATH");
    if (env == nullptr) {
        return {};
    }
    for (const auto &dir : split(env, kPathSep)) {
        if (dir.empty()) {
            continue;
        }
        fs::path candidate = fs::path(dir) / name;
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

}  // namespace

fs::path executable_dir() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) {
            return fs::current_path();
        }
        if (len < buf.size()) {
            buf.resize(len);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return fs::path(buf).parent_path();
#else
    std::error_code ec;
    fs::path self = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : self.parent_path();
#endif
}

fs::path app_data_dir() {
#ifdef _WIN32
    PWSTR raw = nullptr;
    fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        base = fs::path(raw);
        CoTaskMemFree(raw);
    } else {
        const char *env = std::getenv("LOCALAPPDATA");
        base = env ? fs::path(env) : fs::current_path();
    }
    return base / "ScribeEveryone";
#else
    const char *xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        return fs::path(xdg) / "ScribeEveryone";
    }
    const char *home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) / ".local" / "share" : fs::current_path();
    return base / "ScribeEveryone";
#endif
}

fs::path default_model_dir() { return app_data_dir() / "models"; }

fs::path default_config_path() { return app_data_dir() / "scribe.toml"; }

fs::path find_ffmpeg() { return search_path(kFfmpegName); }

fs::path find_ffprobe() { return search_path(kFfprobeName); }

bool onnx_cuda_available() {
#if defined(_WIN32) && defined(SCRIBE_HAVE_CUDA)
    static const bool available = [] {
        // The downloaded runtime directory goes on the search path first,
        // otherwise a freshly installed cuDNN is invisible to this probe and
        // to the loader that will need it moments later.
        activate_gpu_runtime();

        // All three must resolve. cuDNN and cuFFT are NVIDIA downloads that
        // cannot be redistributed, and the CUDA provider is fetched alongside
        // them rather than installed for machines that will never have them.
        for (const wchar_t *name : {L"cudnn64_9.dll", L"cufft64_12.dll",
                                    L"onnxruntime_providers_cuda.dll"}) {
            HMODULE module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_AS_DATAFILE);
            if (module == nullptr) {
                return false;
            }
            FreeLibrary(module);
        }
        return true;
    }();
    return available;
#else
    return false;
#endif
}

namespace {

/// Above this many logical processors the CPU wins on the small per-window
/// models. Derived from the one paired measurement available, a 7950X at 32
/// logical processors beating a 4090 by roughly a third, then set below that
/// crossover so the GPU keeps the work on any machine where the comparison is
/// close. Wrong in one direction costs some throughput, never correctness.
constexpr unsigned kCpuWinsThreads = 24;

}  // namespace

const char *onnx_small_model_provider(Accel requested) {
    if (requested == Accel::Cpu) {
        return "cpu";
    }
    if (!onnx_cuda_available()) {
        return "cpu";
    }
    if (requested == Accel::Cuda) {
        return "cuda";
    }
    const unsigned threads = std::thread::hardware_concurrency();
    return (threads != 0 && threads >= kCpuWinsThreads) ? "cpu" : "cuda";
}

const char *onnx_large_model_provider(Accel requested) {
    if (requested == Accel::Cpu) {
        return "cpu";
    }
    return onnx_cuda_available() ? "cuda" : "cpu";
}

}  // namespace scribe
