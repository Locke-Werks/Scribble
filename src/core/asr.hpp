#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "events.hpp"
#include "types.hpp"

namespace scribe {

namespace fs = std::filesystem;

/// Wraps whisper.cpp. Constructed once per run and reused for every file:
/// reloading a large-v3 model per file costs more than the transcription on
/// short inputs, and this is a batch tool.
class Transcriber {
public:
    ~Transcriber();

    Transcriber(const Transcriber &) = delete;
    Transcriber &operator=(const Transcriber &) = delete;

    static std::unique_ptr<Transcriber> create(const Config &cfg, const fs::path &model_file,
                                               std::string *error);

    /// Parakeet is a transducer, so it needs the unpacked model directory and a
    /// VAD to cut the recording into utterances. Whisper does its own windowing.
    static std::unique_ptr<Transcriber> create_parakeet(const Config &cfg,
                                                        const fs::path &model_dir,
                                                        const fs::path &vad_model,
                                                        std::string *error);

    struct Result {
        std::vector<Segment> segments;
        std::string language;
        bool cancelled = false;
    };

    /// `on_segment` fires as each utterance is decoded, which is what lets the
    /// UI show text while the file is still being processed. `on_progress`
    /// receives 0..1.
    bool transcribe(const std::vector<float> &samples,
                    const std::function<void(const Segment &)> &on_segment,
                    const std::function<void(double)> &on_progress, const CancelToken &cancel,
                    Result *result, std::string *error);

    std::string model_name() const;
    bool using_gpu() const;

private:
    Transcriber();

    bool transcribe_parakeet(const std::vector<float> &samples,
                             const std::function<void(const Segment &)> &on_segment,
                             const std::function<void(double)> &on_progress,
                             const CancelToken &cancel, Result *result, std::string *error);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace scribe
