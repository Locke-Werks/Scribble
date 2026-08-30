#include "models.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

#include "proc.hpp"
#include "util.hpp"

namespace scribe {
namespace {

constexpr const char *kWhisperBase =
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/";
constexpr const char *kSherpaSegmentation =
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-segmentation-models/";
// The release tag really is spelled this way upstream. Correcting it here
// produces a 404.
constexpr const char *kSherpaSpeaker =
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/";

ModelSpec whisper_model(const std::string &name, std::int64_t bytes,
                        const std::string &description) {
    ModelSpec s;
    s.name = name;
    s.kind = ModelKind::Whisper;
    s.filename = "ggml-" + name + ".bin";
    s.url = std::string(kWhisperBase) + s.filename;
    s.description = description;
    s.approx_bytes = bytes;
    return s;
}

ModelSpec speaker_model(const std::string &name, const std::string &file, std::int64_t bytes,
                        const std::string &description) {
    ModelSpec s;
    s.name = name;
    s.kind = ModelKind::Embedding;
    s.filename = file;
    s.url = std::string(kSherpaSpeaker) + file;
    s.description = description;
    s.approx_bytes = bytes;
    return s;
}

std::int64_t content_length(const fs::path &curl, const std::string &url) {
    std::string out;
    int code = 1;
    if (!run_process(curl, {"-sIL", "--fail", url}, &out, &code, nullptr) || code != 0) {
        return 0;
    }
    // Redirects mean several header blocks come back. The last content-length
    // is the one describing the file actually served.
    std::int64_t best = 0;
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        std::string lower = to_lower(trim(line));
        if (starts_with(lower, "content-length:")) {
            best = std::atoll(trim(lower.substr(15)).c_str());
        }
    }
    return best;
}

bool download(const fs::path &curl, const std::string &url, const fs::path &dest,
              const std::string &display, const DownloadProgress &progress,
              std::string *error) {
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);

    fs::path part = dest;
    part += ".part";
    fs::remove(part, ec);

    const std::int64_t total = content_length(curl, url);
    if (progress) {
        progress(display, 0, total);
    }

    std::atomic<bool> running{true};
    std::thread watcher;
    if (progress) {
        // curl writes its progress meter to stderr in a format that is awkward
        // to parse reliably, so the partial file size is polled instead.
        watcher = std::thread([&] {
            while (running.load(std::memory_order_relaxed)) {
                std::error_code size_ec;
                auto done = static_cast<std::int64_t>(fs::file_size(part, size_ec));
                if (!size_ec) {
                    progress(display, done, total);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
            }
        });
    }

    std::string out;
    int code = 1;
    bool started = run_process(curl,
                               {"-L", "--fail", "--silent", "--show-error", "--retry", "3",
                                "--retry-delay", "2", "-o", part.string(), url},
                               &out, &code, error);

    running.store(false, std::memory_order_relaxed);
    if (watcher.joinable()) {
        watcher.join();
    }

    if (!started) {
        fs::remove(part, ec);
        return false;
    }
    if (code != 0) {
        fs::remove(part, ec);
        if (error) {
            *error = "download failed for " + url + ": " + trim(out);
        }
        return false;
    }

    fs::rename(part, dest, ec);
    if (ec) {
        if (error) {
            *error = "cannot move downloaded model into place: " + ec.message();
        }
        return false;
    }
    if (progress) {
        progress(display, total, total);
    }
    return true;
}

bool extract_member(const fs::path &tar, const fs::path &archive, const std::string &member,
                    const fs::path &dest, std::string *error) {
    std::error_code ec;
    fs::path staging = archive.parent_path() / (archive.stem().string() + ".extract");
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);

    std::string out;
    int code = 1;
    if (!run_process(tar, {"-xf", archive.string(), "-C", staging.string()}, &out, &code,
                     error)) {
        return false;
    }
    if (code != 0) {
        if (error) {
            *error = "cannot extract " + archive.filename().string() + ": " + trim(out);
        }
        fs::remove_all(staging, ec);
        return false;
    }

    // The archive layout can shift between releases, so the member is located
    // by name rather than by an assumed path.
    fs::path found;
    for (auto it = fs::recursive_directory_iterator(staging, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (it->is_regular_file(ec) && it->path().filename().string() == member) {
            found = it->path();
            break;
        }
    }

    if (found.empty()) {
        if (error) {
            *error = "archive " + archive.filename().string() + " does not contain " + member;
        }
        fs::remove_all(staging, ec);
        return false;
    }

    fs::create_directories(dest.parent_path(), ec);
    fs::remove(dest, ec);
    fs::rename(found, dest, ec);
    if (ec) {
        fs::copy_file(found, dest, fs::copy_options::overwrite_existing, ec);
    }
    fs::remove_all(staging, ec);
    fs::remove(archive, ec);

    if (!fs::exists(dest, ec)) {
        if (error) {
            *error = "cannot place extracted model at " + dest.string();
        }
        return false;
    }
    return true;
}

}  // namespace

const std::vector<ModelSpec> &model_catalogue() {
    static const std::vector<ModelSpec> catalogue = [] {
        std::vector<ModelSpec> v;

        v.push_back(whisper_model("tiny", 78'000'000, "Fastest, lowest accuracy"));
        v.push_back(whisper_model("base", 148'000'000, "Fast, low accuracy"));
        v.push_back(whisper_model("small", 488'000'000, "Usable for clean speech"));
        v.push_back(whisper_model("medium", 1'530'000'000, "Good accuracy"));
        v.push_back(whisper_model("large-v2", 3'090'000'000, "Strong, occasionally better "
                                                             "than v3 on noisy audio"));
        v.push_back(whisper_model("large-v3", 3'100'000'000, "Best general accuracy"));
        v.push_back(whisper_model("large-v3-turbo", 1'620'000'000,
                                  "Near large-v3 accuracy at several times the speed"));

        ModelSpec seg;
        seg.name = "pyannote-segmentation-3.0";
        seg.kind = ModelKind::Segmentation;
        seg.url = std::string(kSherpaSegmentation) +
                  "sherpa-onnx-pyannote-segmentation-3-0.tar.bz2";
        seg.filename = "pyannote-segmentation-3.0.onnx";
        seg.archive = true;
        seg.member = "model.onnx";
        seg.description = "Speaker segmentation, ONNX export of pyannote 3.0";
        seg.approx_bytes = 6'000'000;
        v.push_back(seg);

        v.push_back(speaker_model("wespeaker-resnet293", "wespeaker_en_voxceleb_resnet293_LM.onnx",
                                  110'000'000,
                                  "English voiceprints, strongest of the available set"));
        v.push_back(speaker_model("3dspeaker-campplus-en",
                                  "3dspeaker_speech_campplus_sv_en_voxceleb_16k.onnx",
                                  28'000'000, "English voiceprints, much smaller and faster"));
        v.push_back(speaker_model("3dspeaker-eres2netv2",
                                  "3dspeaker_speech_eres2netv2_sv_zh-cn_16k-common.onnx",
                                  68'000'000, "Mandarin voiceprints"));

        return v;
    }();
    return catalogue;
}

const ModelSpec *find_model(const std::string &name, ModelKind kind) {
    for (const auto &spec : model_catalogue()) {
        if (spec.kind == kind && iequals(spec.name, name)) {
            return &spec;
        }
    }
    return nullptr;
}

bool resolve_model(const std::string &name, ModelKind kind, const fs::path &model_dir,
                   const DownloadProgress &progress, fs::path *out, std::string *error) {
    std::error_code ec;

    fs::path direct(name);
    if (direct.is_absolute() && fs::exists(direct, ec)) {
        *out = direct;
        return true;
    }

    const ModelSpec *spec = find_model(name, kind);
    fs::path target = model_dir / (spec ? spec->filename : name);
    if (fs::exists(target, ec)) {
        *out = target;
        return true;
    }
    if (!spec) {
        if (error) {
            *error = "unknown model \"" + name +
                     "\". Use a catalogue name or an absolute path to a model file.";
        }
        return false;
    }

    fs::path curl = find_system_tool("curl");
    if (curl.empty()) {
        if (error) {
            *error = "curl not found, cannot download " + spec->name;
        }
        return false;
    }

    if (spec->archive) {
        fs::path archive = model_dir / fs::path(spec->url).filename();
        if (!download(curl, spec->url, archive, spec->name, progress, error)) {
            return false;
        }
        fs::path tar = find_system_tool("tar");
        if (tar.empty()) {
            if (error) {
                *error = "tar not found, cannot unpack " + archive.filename().string();
            }
            return false;
        }
        if (!extract_member(tar, archive, spec->member, target, error)) {
            return false;
        }
    } else if (!download(curl, spec->url, target, spec->name, progress, error)) {
        return false;
    }

    *out = target;
    return true;
}

}  // namespace scribe
