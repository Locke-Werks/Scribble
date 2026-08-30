#include "gpu_runtime.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

#include "paths.hpp"
#include "proc.hpp"
#include "util.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace scribe {
namespace {

// Pinned deliberately. onnxruntime's CUDA provider must match the
// onnxruntime.dll the program was built against, so this version tracks the one
// in cmake/Dependencies.cmake and the two move together or not at all.
constexpr const char *kOrtVersion = "1.27.1";

std::string ort_provider_url() {
    return std::string("https://github.com/microsoft/onnxruntime/releases/download/v") +
           kOrtVersion + "/onnxruntime-win-x64-gpu_cuda13-" + kOrtVersion + ".zip";
}

bool have_file(const fs::path &dir, const std::string &name) {
    std::error_code ec;
    if (fs::exists(dir / name, ec)) {
        return true;
    }
#ifdef _WIN32
    // Already resolvable elsewhere, typically because a CUDA toolkit or a
    // previous NVIDIA install put it on the search path.
    HMODULE module = LoadLibraryExW(widen(name).c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (module != nullptr) {
        FreeLibrary(module);
        return true;
    }
#endif
    return false;
}

bool extract(const fs::path &archive, const fs::path &staging, const std::string &pattern,
             std::string *error) {
    std::error_code ec;
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);

    fs::path tar = find_system_tool("tar");
    if (tar.empty()) {
        if (error) {
            *error = "tar not found, cannot unpack " + archive.filename().string();
        }
        return false;
    }

    // Only the wanted members are extracted. These archives run to gigabytes
    // and unpacking them whole would need the space twice over.
    std::vector<std::string> args = {"-xf", archive.string(), "-C", staging.string()};
    if (!pattern.empty()) {
        args.push_back(pattern);
    }

    std::string out;
    int code = 1;
    if (!run_process(tar, args, &out, &code, error)) {
        return false;
    }
    if (code != 0) {
        if (error) {
            *error = "cannot unpack " + archive.filename().string() + ": " + trim(out);
        }
        return false;
    }
    return true;
}

bool collect(const fs::path &staging, const std::vector<std::string> &required,
             const fs::path &dest, std::string *error) {
    std::error_code ec;
    fs::create_directories(dest, ec);

    // Everything extracted is kept, not just the named files. cuDNN splits
    // itself across engine and helper libraries that load each other, and the
    // set changes between releases, so copying the archive's own idea of what
    // belongs together is more durable than a list written here.
    int copied = 0;
    for (auto it = fs::recursive_directory_iterator(staging, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        if (!iequals(it->path().extension().string(), ".dll")) {
            continue;
        }
        fs::copy_file(it->path(), dest / it->path().filename(),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (error) {
                *error = "cannot place " + it->path().filename().string() + ": " + ec.message();
            }
            return false;
        }
        ++copied;
    }

    if (copied == 0) {
        if (error) {
            *error = "archive contained no libraries";
        }
        return false;
    }

    for (const auto &name : required) {
        if (!fs::exists(dest / name, ec)) {
            if (error) {
                *error = "archive did not contain " + name;
            }
            return false;
        }
    }
    return true;
}

bool download(const fs::path &curl, const GpuComponent &component, const fs::path &dest,
              const GpuProgress &progress, const CancelToken &cancel, std::string *error) {
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);

    std::atomic<bool> running{true};
    std::thread watcher;
    if (progress) {
        watcher = std::thread([&] {
            while (running.load(std::memory_order_relaxed)) {
                std::error_code size_ec;
                auto done = static_cast<std::int64_t>(fs::file_size(dest, size_ec));
                if (!size_ec) {
                    progress(component.name, done, component.download_bytes);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
    }

    std::string out;
    int code = 1;
    // Resumed rather than restarted. These are gigabyte downloads and a dropped
    // connection partway through should not cost the whole thing.
    const bool started = run_process(curl,
                                     {"-L", "--fail", "--silent", "--show-error", "-C", "-",
                                      "--retry", "5", "--retry-delay", "3", "-o",
                                      dest.string(), component.url},
                                     &out, &code, error);

    running.store(false, std::memory_order_relaxed);
    if (watcher.joinable()) {
        watcher.join();
    }

    if (!started) {
        return false;
    }
    if (code != 0) {
        if (error) {
            *error = "download failed for " + component.name + ": " + trim(out);
        }
        return false;
    }
    if (cancel.stop_requested()) {
        if (error) {
            *error = "cancelled";
        }
        return false;
    }
    return true;
}

}  // namespace

fs::path gpu_runtime_dir() { return app_data_dir() / "runtime"; }

const std::vector<GpuComponent> &gpu_components() {
    static const std::vector<GpuComponent> components = [] {
        std::vector<GpuComponent> v;

        GpuComponent cudnn;
        cudnn.name = "cuDNN 9";
        cudnn.url = "https://developer.download.nvidia.com/compute/cudnn/redist/cudnn/"
                    "windows-x86_64/cudnn-windows-x86_64-9.25.1.1_cuda13-archive.zip";
        // Matched by filename at any depth. NVIDIA moves these between bin/ and
        // bin/x64/ across releases, and a path-shaped pattern breaks silently
        // after a gigabyte has already been downloaded.
        cudnn.archive_glob = "*cudnn*.dll";
        cudnn.files = {
            "cudnn64_9.dll",
            "cudnn_graph64_9.dll",
            "cudnn_ops64_9.dll",
            "cudnn_cnn64_9.dll",
            "cudnn_engines_precompiled64_9.dll",
        };
        cudnn.download_bytes = 1'234'000'000;
        v.push_back(cudnn);

        GpuComponent cufft;
        cufft.name = "cuFFT";
        cufft.url = "https://developer.download.nvidia.com/compute/cuda/redist/libcufft/"
                    "windows-x86_64/libcufft-windows-x86_64-12.3.0.29-archive.zip";
        cufft.archive_glob = "*cufft64_12.dll";
        cufft.files = {"cufft64_12.dll"};
        cufft.download_bytes = 182'000'000;
        v.push_back(cufft);

        GpuComponent provider;
        provider.name = "onnxruntime CUDA provider";
        provider.url = ort_provider_url();
        provider.archive_glob = "*onnxruntime_providers_cuda.dll";
        provider.files = {"onnxruntime_providers_cuda.dll"};
        provider.download_bytes = 334'000'000;
        v.push_back(provider);

        return v;
    }();
    return components;
}

std::string GpuRuntimeStatus::summary() const {
    if (ready) {
        return "GPU acceleration for isolation, diarization and voiceprints is ready.";
    }
    std::vector<std::string> names;
    for (const auto &c : missing) {
        names.push_back(c.name);
    }
    char size[64];
    std::snprintf(size, sizeof(size), "%.1f GB",
                  static_cast<double>(download_bytes) / 1'000'000'000.0);
    return "Needs " + join(names, ", ") + ", about " + size + " from NVIDIA and Microsoft.";
}

GpuRuntimeStatus gpu_runtime_status() {
    GpuRuntimeStatus status;
    const fs::path dir = gpu_runtime_dir();

    for (const auto &component : gpu_components()) {
        bool complete = true;
        for (const auto &file : component.files) {
            if (!have_file(dir, file)) {
                complete = false;
                break;
            }
        }
        if (!complete) {
            status.missing.push_back(component);
            status.download_bytes += component.download_bytes;
        }
    }
    status.ready = status.missing.empty();
    return status;
}

void activate_gpu_runtime() {
#ifdef _WIN32
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    const fs::path dir = gpu_runtime_dir();
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return;
    }

    // Prepended to PATH rather than registered with AddDllDirectory, because
    // the loader resolves onnxruntime_providers_cuda.dll's own imports through
    // the standard search order and never sees our added directories.
    const char *existing = std::getenv("PATH");
    std::string value = dir.string();
    if (existing != nullptr && *existing != '\0') {
        value += ";";
        value += existing;
    }
    SetEnvironmentVariableW(L"PATH", widen(value).c_str());
    _putenv_s("PATH", value.c_str());
#endif
}

bool install_gpu_runtime(const GpuRuntimeStatus &status, const GpuProgress &progress,
                         const CancelToken &cancel, std::string *error) {
    if (status.ready) {
        return true;
    }

    const fs::path curl = find_system_tool("curl");
    if (curl.empty()) {
        if (error) {
            *error = "curl not found, cannot download the GPU runtime";
        }
        return false;
    }

    const fs::path dir = gpu_runtime_dir();
    const fs::path work = dir / "download";
    std::error_code ec;
    fs::create_directories(work, ec);

    for (const auto &component : status.missing) {
        if (cancel.stop_requested()) {
            if (error) {
                *error = "cancelled";
            }
            return false;
        }

        const fs::path archive = work / fs::path(component.url).filename();
        if (!download(curl, component, archive, progress, cancel, error)) {
            return false;
        }

        const fs::path staging = work / "extract";
        if (!extract(archive, staging, component.archive_glob, error)) {
            return false;
        }
        if (!collect(staging, component.files, dir, error)) {
            fs::remove_all(staging, ec);
            return false;
        }

        fs::remove_all(staging, ec);
        fs::remove(archive, ec);

        if (progress) {
            progress(component.name, component.download_bytes, component.download_bytes);
        }
    }

    fs::remove_all(work, ec);
    activate_gpu_runtime();
    return true;
}

}  // namespace scribe
