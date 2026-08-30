#include "diarize.hpp"

#include <algorithm>
#include <cstdio>
#include <map>

#include "paths.hpp"
#include "sherpa-onnx/c-api/cxx-api.h"

namespace scribe {
namespace {

namespace sx = sherpa_onnx::cxx;

const char *provider_for(Accel accel) {
    if (accel == Accel::Cpu) {
        return "cpu";
    }
    // Asking for CUDA when its provider cannot load aborts the process rather
    // than degrading, so availability is probed instead of assumed.
    return onnx_cuda_available() ? "cuda" : "cpu";
}

/// Overlap in seconds between a word and a diarized turn.
double overlap(double a0, double a1, double b0, double b1) {
    return std::max(0.0, std::min(a1, b1) - std::max(a0, b0));
}

}  // namespace

std::string local_label_for(int speaker) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "speaker_%02d", speaker);
    return buf;
}

struct Diarizer::Impl {
    std::unique_ptr<sx::OfflineSpeakerDiarization> engine;
    Config cfg;
};

Diarizer::Diarizer() : impl_(std::make_unique<Impl>()) {}
Diarizer::~Diarizer() = default;

std::unique_ptr<Diarizer> Diarizer::create(const Config &cfg,
                                           const fs::path &segmentation_model,
                                           const fs::path &embedding_model,
                                           std::string *error) {
    std::error_code ec;
    if (!fs::exists(segmentation_model, ec)) {
        if (error) {
            *error = "segmentation model not found: " + segmentation_model.string();
        }
        return nullptr;
    }
    if (!fs::exists(embedding_model, ec)) {
        if (error) {
            *error = "speaker embedding model not found: " + embedding_model.string();
        }
        return nullptr;
    }

    sx::OfflineSpeakerDiarizationConfig config;
    config.segmentation.pyannote.model = segmentation_model.string();
    config.segmentation.num_threads = std::max(1, cfg.n_threads / 2);
    config.segmentation.provider = provider_for(cfg.onnx_accel);

    config.embedding.model = embedding_model.string();
    config.embedding.num_threads = std::max(1, cfg.n_threads / 2);
    config.embedding.provider = provider_for(cfg.onnx_accel);

    // A known speaker count bypasses thresholding entirely, which is far more
    // reliable than any threshold when the count is actually known.
    if (cfg.num_speakers > 0) {
        config.clustering.num_clusters = cfg.num_speakers;
    } else {
        config.clustering.num_clusters = -1;
        config.clustering.threshold = cfg.diar_cluster_threshold;
    }

    // Sub-300ms turns are almost always a backchannel or a segmentation
    // artefact, and they contribute nothing but noise to a voiceprint.
    config.min_duration_on = 0.3f;
    config.min_duration_off = 0.5f;

    auto engine = sx::OfflineSpeakerDiarization::Create(config);
    if (!engine.Get()) {
        if (error) {
            *error = "cannot initialise speaker diarization";
        }
        return nullptr;
    }

    std::unique_ptr<Diarizer> self(new Diarizer());
    self->impl_->engine = std::make_unique<sx::OfflineSpeakerDiarization>(std::move(engine));
    self->impl_->cfg = cfg;
    return self;
}

bool Diarizer::diarize(const std::vector<float> &samples,
                       const std::function<void(double)> &on_progress,
                       const CancelToken &cancel, std::vector<DiarizedTurn> *turns,
                       std::string *error) {
    if (samples.empty()) {
        if (error) {
            *error = "no audio samples";
        }
        return false;
    }
    if (!impl_->engine) {
        if (error) {
            *error = "diarizer not initialised";
        }
        return false;
    }

    bool stopped = false;
    auto callback = [&](std::int32_t done, std::int32_t total) {
        if (!cancel.wait_if_paused()) {
            stopped = true;
        }
        if (on_progress && total > 0) {
            on_progress(static_cast<double>(done) / static_cast<double>(total));
        }
    };

    auto raw = impl_->engine->Process(samples.data(), static_cast<std::int32_t>(samples.size()),
                                      callback);
    if (stopped) {
        if (error) {
            *error = "cancelled";
        }
        return false;
    }

    turns->clear();
    turns->reserve(raw.size());
    for (const auto &s : raw) {
        DiarizedTurn t;
        t.start = s.start;
        t.end = s.end;
        t.speaker = s.speaker;
        turns->push_back(t);
    }
    std::sort(turns->begin(), turns->end(),
              [](const DiarizedTurn &a, const DiarizedTurn &b) { return a.start < b.start; });
    return true;
}

void assign_speakers(std::vector<Segment> &segments, const std::vector<DiarizedTurn> &turns) {
    if (turns.empty()) {
        return;
    }

    for (auto &seg : segments) {
        std::map<int, double> weight;

        if (!seg.words.empty()) {
            for (const auto &w : seg.words) {
                for (const auto &t : turns) {
                    if (t.start > w.end) {
                        break;
                    }
                    double o = overlap(w.start, w.end, t.start, t.end);
                    if (o > 0.0) {
                        weight[t.speaker] += o;
                    }
                }
            }
        }

        // Falling back to the whole span keeps a segment attributable when
        // word timestamps were disabled or the decoder produced none.
        if (weight.empty()) {
            for (const auto &t : turns) {
                if (t.start > seg.end) {
                    break;
                }
                double o = overlap(seg.start, seg.end, t.start, t.end);
                if (o > 0.0) {
                    weight[t.speaker] += o;
                }
            }
        }

        if (weight.empty()) {
            continue;
        }

        auto best = std::max_element(
            weight.begin(), weight.end(),
            [](const auto &a, const auto &b) { return a.second < b.second; });
        seg.local_label = local_label_for(best->first);
    }
}

}  // namespace scribe
