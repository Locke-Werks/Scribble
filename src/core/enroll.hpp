#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "db.hpp"
#include "models.hpp"

namespace scribble {

namespace fs = std::filesystem;

/// A piece of reference audio offered for enrolment. `end` at 0 means "to the
/// end of the file", so the common case of a whole clip needs no offsets.
struct EnrollSource {
    fs::path path;
    double start = 0.0;
    double end = 0.0;
};

struct EnrollOptions {
    /// Analysis window. Long enough that a window is dominated by the speaker
    /// rather than by whichever phoneme it landed on, short enough that a
    /// second voice in the clip lands in windows of its own instead of being
    /// averaged into every one of them.
    double window = 4.0;
    double hop = 2.0;
    /// Below this there is not enough speech to characterise a voice, and the
    /// resulting profile matches on channel rather than on the person.
    double min_speech = 3.0;
    /// Coherence under this gets reported to the caller. Not an error: a
    /// legitimately expressive clip can sit here, and the human looking at the
    /// number is better placed to judge than a constant is.
    float coherence_warn = 0.72f;
};

/// What analysing one clip produced, whether or not it is usable.
struct EnrollClipResult {
    fs::path source;
    double start = 0.0;
    double end = 0.0;
    double file_duration = 0.0;  ///< audio examined, before the silence gate
    double duration = 0.0;       ///< speech kept
    int windows = 0;
    float coherence = 0.0f;
    std::vector<float> centroid;
    bool usable = false;
    /// Why it is unusable, or what is wrong with it if it is usable anyway.
    std::string problem;
};

/// Turns reference audio into voiceprints for a known person.
///
/// Separate from Embedder because the inputs are different in kind: the
/// pipeline embeds spans that diarization already vouched for, whereas a clip
/// handed over by a human is an unknown quantity that has to be gated for
/// silence, measured for self-consistency, and reported on rather than
/// silently accepted. Enrolling a clip with two people in it is worse than not
/// enrolling at all, because it produces a confident profile of nobody.
class Enroller {
public:
    ~Enroller();

    Enroller(const Enroller &) = delete;
    Enroller &operator=(const Enroller &) = delete;

    /// Resolves and loads the speaker embedding model, downloading it on first
    /// use. `progress` may be empty.
    static std::unique_ptr<Enroller> create(const Config &cfg, const DownloadProgress &progress,
                                            std::string *error);

    /// Decodes, gates, windows and embeds one clip. Returns false only when the
    /// clip could not be read at all; a clip that decodes but is unusable comes
    /// back with `usable` false and `problem` filled in.
    bool analyse(const EnrollSource &source, const EnrollOptions &opts, EnrollClipResult *out,
                 std::string *error);

    /// Same, over audio already decoded to 16 kHz mono. Public because the
    /// gating and the scoring are the useful part and a caller that already has
    /// the samples should not have to write them to a file to reach it.
    bool analyse_samples(const std::vector<float> &samples, const EnrollOptions &opts,
                         EnrollClipResult *out, std::string *error);

private:
    Enroller();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Adds an analysed clip to an identity, creating the identity when
/// `global_id` is negative. Returns the identity that now owns the clip.
std::int64_t commit_enrollment(Database &db, std::int64_t global_id, const std::string &name,
                               const EnrollClipResult &clip);

}  // namespace scribble
