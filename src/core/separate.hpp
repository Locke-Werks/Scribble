#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "wavio.hpp"

namespace scribe {

namespace fs = std::filesystem;

/// Strips music and background from a recording, keeping the vocal stem.
///
/// This is the largest single accuracy gain available on broadcast, field and
/// music-bedded audio, and a small loss on already-clean speech, which is why
/// `IsolateMode::Auto` gates it on a measured noise floor rather than running
/// it on everything.
///
/// UVR models work at their own sample rate on stereo input, so isolation has
/// to happen before the pipeline collapses audio to 16 kHz mono. Running it
/// afterwards feeds the model a rate it was never trained on.
class Separator {
public:
    ~Separator();

    Separator(const Separator &) = delete;
    Separator &operator=(const Separator &) = delete;

    static std::unique_ptr<Separator> create(const Config &cfg, const fs::path &model_file,
                                             std::string *error);

    /// The rate the model expects and produces. Decode to this before calling.
    int sample_rate() const;

    /// Returns the vocal stem. `input` may be mono or stereo.
    bool isolate_vocals(const WavData &input, WavData *vocals, std::string *error);

private:
    Separator();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace scribe
