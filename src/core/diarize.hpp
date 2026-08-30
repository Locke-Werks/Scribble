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

struct DiarizedTurn {
    double start = 0.0;
    double end = 0.0;
    int speaker = 0;  ///< cluster index within this file only

    double duration() const { return end - start; }
};

/// Wraps sherpa-onnx offline diarization: a pyannote segmentation model plus a
/// speaker embedding model plus clustering, all ONNX.
///
/// This runs after transcription because it needs the whole file, whereas
/// Whisper streams. Doing it first would leave the user watching nothing.
class Diarizer {
public:
    ~Diarizer();

    Diarizer(const Diarizer &) = delete;
    Diarizer &operator=(const Diarizer &) = delete;

    static std::unique_ptr<Diarizer> create(const Config &cfg, const fs::path &segmentation_model,
                                            const fs::path &embedding_model, std::string *error);

    bool diarize(const std::vector<float> &samples,
                 const std::function<void(double)> &on_progress, const CancelToken &cancel,
                 std::vector<DiarizedTurn> *turns, std::string *error);

private:
    Diarizer();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Assigns each transcript segment a speaker label.
///
/// Assignment is by word, not by segment midpoint. A single Whisper utterance
/// routinely spans a speaker change, and midpoint assignment silently hands
/// the whole line to whoever was talking in the middle of it. Words carry
/// their own timings, so the segment goes to whoever owns most of its words.
void assign_speakers(std::vector<Segment> &segments, const std::vector<DiarizedTurn> &turns);

/// "speaker_00" style label used inside one file before global resolution.
std::string local_label_for(int speaker);

}  // namespace scribe
