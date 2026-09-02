#include "enroll.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "embed.hpp"
#include "media.hpp"
#include "paths.hpp"
#include "speakers.hpp"
#include "util.hpp"
#include "wavio.hpp"

namespace scribble {
namespace {

/// Windows quieter than this fraction of the clip's loud energy are dropped.
///
/// Relative rather than absolute, because a reference clip can arrive at any
/// level and a fixed dBFS gate either passes a whole quiet file or rejects one.
/// A speaker embedding taken over silence is not empty, it is a confident
/// voiceprint of the room, and enrolling one is how a profile ends up matching
/// on microphone rather than on the person.
constexpr float kSilenceRatio = 0.12f;

/// Below this a window carries no signal worth measuring, at roughly -74 dBFS.
///
/// An absolute floor is needed as well as the relative one because the relative
/// gate is derived from the clip's own content: a clip that is almost entirely
/// silence has a quiet reference level, and scaling that by a ratio produces a
/// gate low enough to admit the silence it was supposed to reject.
constexpr float kAbsoluteFloor = 2e-4f;

/// Seconds to one decimal. format_duration rounds to the whole second, which
/// turns "2.4s of speech, needs 3.0s" into "2s, needs 3s".
std::string seconds_text(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1fs", value);
    return buf;
}

float rms(const std::vector<float> &samples, size_t begin, size_t end) {
    if (end <= begin) {
        return 0.0f;
    }
    double sum = 0.0;
    for (size_t i = begin; i < end; ++i) {
        sum += static_cast<double>(samples[i]) * samples[i];
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(end - begin)));
}

/// Level of the loud part of the clip.
///
/// A high percentile rather than the peak, so one door slam does not set the
/// gate above the speech. Taken over the windows that carry signal at all
/// rather than over every window, which is the part that matters: a clip of one
/// sentence followed by four minutes of room tone has silence at its 90th
/// percentile, and a gate derived from that admits the room tone as speech.
float loud_level(const std::vector<float> &levels) {
    std::vector<float> sounded;
    sounded.reserve(levels.size());
    for (float level : levels) {
        if (level >= kAbsoluteFloor) {
            sounded.push_back(level);
        }
    }
    if (sounded.empty()) {
        return 0.0f;
    }
    std::sort(sounded.begin(), sounded.end());
    const auto idx = static_cast<size_t>(static_cast<double>(sounded.size() - 1) * 0.9);
    return sounded[idx];
}

}  // namespace

struct Enroller::Impl {
    Config cfg;
    fs::path ffmpeg;
    std::unique_ptr<Embedder> embedder;
};

Enroller::Enroller() : impl_(std::make_unique<Impl>()) {}
Enroller::~Enroller() = default;

std::unique_ptr<Enroller> Enroller::create(const Config &cfg, const DownloadProgress &progress,
                                           std::string *error) {
    fs::path model;
    if (!resolve_model(cfg.embedding_model, ModelKind::Embedding, cfg.model_dir, progress, &model,
                       error)) {
        return nullptr;
    }

    auto embedder = Embedder::create(cfg, model, error);
    if (!embedder) {
        return nullptr;
    }

    std::unique_ptr<Enroller> self(new Enroller());
    self->impl_->cfg = cfg;
    self->impl_->ffmpeg = find_ffmpeg();
    self->impl_->embedder = std::move(embedder);
    return self;
}

bool Enroller::analyse(const EnrollSource &source, const EnrollOptions &opts,
                       EnrollClipResult *out, std::string *error) {
    out->source = source.path;
    out->start = source.start;
    out->end = source.end;

    std::error_code ec;
    if (!fs::exists(source.path, ec)) {
        if (error) {
            *error = "no such file: " + source.path.string();
        }
        return false;
    }
    if (impl_->ffmpeg.empty()) {
        if (error) {
            *error = "ffmpeg is required to read reference audio but was not found on PATH";
        }
        return false;
    }

    // Named from the source and a hash of the range so two clips cut from one
    // file cannot collide, and so a second pass over the same clip overwrites
    // its own scratch file rather than accumulating.
    const std::string tag = std::to_string(
        std::hash<std::string>{}(source.path.string() + "|" + std::to_string(source.start) + "|" +
                                 std::to_string(source.end)));
    fs::path wav = impl_->cfg.work_dir / ("enroll." + tag + ".wav");

    struct WorkFile {
        const fs::path &path;
        bool keep;
        ~WorkFile() {
            if (!keep) {
                std::error_code inner;
                fs::remove(path, inner);
            }
        }
    } work_file{wav, impl_->cfg.keep_work};

    if (!decode_range_to_wav(impl_->ffmpeg, source.path, wav, source.start, source.end, error)) {
        return false;
    }

    std::vector<float> samples;
    if (!read_wav_mono16k(wav, &samples, error)) {
        return false;
    }

    return analyse_samples(samples, opts, out, error);
}

bool Enroller::analyse_samples(const std::vector<float> &samples, const EnrollOptions &opts,
                               EnrollClipResult *out, std::string *error) {
    out->windows = 0;
    out->duration = 0.0;
    out->coherence = 0.0f;
    out->centroid.clear();
    out->usable = false;
    out->file_duration = static_cast<double>(samples.size()) / kSampleRate;

    if (samples.empty()) {
        out->problem = "no audio";
        return true;
    }

    const auto window = static_cast<size_t>(std::max(1.0, opts.window) * kSampleRate);
    const auto hop = static_cast<size_t>(std::max(0.5, opts.hop) * kSampleRate);

    // A clip shorter than one window still gets a single embedding over
    // whatever is there. Coherence is meaningless with one window, so it is
    // reported as 1 and the duration check is what gates the clip.
    std::vector<std::pair<size_t, size_t>> spans;
    if (samples.size() <= window) {
        spans.emplace_back(0, samples.size());
    } else {
        for (size_t begin = 0; begin + window <= samples.size(); begin += hop) {
            spans.emplace_back(begin, begin + window);
        }
    }

    std::vector<float> levels;
    levels.reserve(spans.size());
    for (const auto &[begin, end] : spans) {
        levels.push_back(rms(samples, begin, end));
    }
    const float gate = std::max(loud_level(levels) * kSilenceRatio, kAbsoluteFloor);

    std::vector<std::vector<float>> vectors;
    std::vector<double> weights;
    double kept = 0.0;
    for (size_t i = 0; i < spans.size(); ++i) {
        if (levels[i] < gate) {
            continue;
        }
        const double start = static_cast<double>(spans[i].first) / kSampleRate;
        const double end = static_cast<double>(spans[i].second) / kSampleRate;

        std::vector<float> vec;
        std::string span_error;
        if (!impl_->embedder->embed_span(samples, start, end, &vec, &span_error)) {
            continue;
        }
        vectors.push_back(std::move(vec));
        weights.push_back(end - start);
        kept += end - start;
    }

    if (vectors.empty()) {
        out->problem = "no speech loud enough to embed";
        return true;
    }

    // Overlapping windows count their overlap once, so the reported duration is
    // speech present rather than speech analysed.
    const double overlap_factor =
        spans.size() > 1 ? std::min(1.0, opts.hop / std::max(0.001, opts.window)) : 1.0;
    out->duration = kept * overlap_factor;
    out->windows = static_cast<int>(vectors.size());
    out->centroid = centroid_of(vectors, weights);

    float lowest = 1.0f;
    for (const auto &v : vectors) {
        lowest = std::min(lowest, cosine(v, out->centroid));
    }
    out->coherence = vectors.size() > 1 ? lowest : 1.0f;

    if (out->duration < opts.min_speech) {
        out->problem = "only " + seconds_text(out->duration) + " of speech, needs " +
                       seconds_text(opts.min_speech);
        return true;
    }

    out->usable = true;
    if (out->windows > 1 && out->coherence < opts.coherence_warn) {
        // Almost always a second voice in the clip, occasionally a stretch of
        // music or a hard cut between two recordings. Reported rather than
        // rejected: the human can hear which it is and this cannot.
        out->problem = "windows disagree with each other, check the clip holds one voice only";
    }
    return true;
}

std::int64_t commit_enrollment(Database &db, std::int64_t global_id, const std::string &name,
                               const EnrollClipResult &clip) {
    if (clip.centroid.empty()) {
        return -1;
    }

    if (global_id < 0) {
        global_id = db.create_named_global(name);
    } else if (!name.empty()) {
        auto existing = db.global(global_id);
        if (existing && existing->name.empty()) {
            db.rename_global(global_id, name);
        }
    }

    EnrollmentClip row;
    row.global_id = global_id;
    row.source = clip.source.string();
    row.start = clip.start;
    row.end = clip.end;
    row.duration = clip.duration;
    row.centroid = clip.centroid;
    row.n_windows = clip.windows;
    row.coherence = clip.coherence;
    db.add_enrollment(row);
    return global_id;
}

}  // namespace scribble
