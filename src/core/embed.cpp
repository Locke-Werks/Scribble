#include "embed.hpp"

#include <algorithm>
#include <map>

#include "media.hpp"
#include "sherpa-onnx/c-api/cxx-api.h"
#include "speakers.hpp"

namespace scribe {
namespace {

namespace sx = sherpa_onnx::cxx;

const char *provider_for(Accel accel) {
#ifdef SCRIBE_HAVE_CUDA
    return accel == Accel::Cuda ? "cuda" : "cpu";
#else
    (void)accel;
    return "cpu";
#endif
}

/// Below this a voiceprint is dominated by whatever phoneme happened to be in
/// the clip rather than by the speaker, and matching becomes a coin flip.
constexpr double kAbsoluteMinimumSpan = 0.6;

}  // namespace

struct Embedder::Impl {
    std::unique_ptr<sx::SpeakerEmbeddingExtractor> extractor;
    Config cfg;
    int dim = 0;
};

Embedder::Embedder() : impl_(std::make_unique<Impl>()) {}
Embedder::~Embedder() = default;

std::unique_ptr<Embedder> Embedder::create(const Config &cfg, const fs::path &model_file,
                                           std::string *error) {
    std::error_code ec;
    if (!fs::exists(model_file, ec)) {
        if (error) {
            *error = "speaker embedding model not found: " + model_file.string();
        }
        return nullptr;
    }

    sx::SpeakerEmbeddingExtractorConfig config;
    config.model = model_file.string();
    config.num_threads = std::max(1, cfg.n_threads / 2);
    config.provider = provider_for(cfg.onnx_accel);

    auto extractor = sx::SpeakerEmbeddingExtractor::Create(config);
    if (!extractor.Get()) {
        if (error) {
            *error = "cannot initialise speaker embedding extractor";
        }
        return nullptr;
    }

    std::unique_ptr<Embedder> self(new Embedder());
    self->impl_->dim = extractor.Dim();
    self->impl_->extractor =
        std::make_unique<sx::SpeakerEmbeddingExtractor>(std::move(extractor));
    self->impl_->cfg = cfg;
    return self;
}

int Embedder::dim() const { return impl_->dim; }

bool Embedder::embed_span(const std::vector<float> &samples, double start, double end,
                          std::vector<float> *out, std::string *error) {
    if (!impl_->extractor) {
        if (error) {
            *error = "embedder not initialised";
        }
        return false;
    }

    auto begin = static_cast<size_t>(std::max(0.0, start) * kSampleRate);
    auto finish = static_cast<size_t>(std::max(0.0, end) * kSampleRate);
    begin = std::min(begin, samples.size());
    finish = std::min(finish, samples.size());
    if (finish <= begin) {
        if (error) {
            *error = "empty span";
        }
        return false;
    }

    auto stream = impl_->extractor->CreateStream();
    stream.AcceptWaveform(kSampleRate, samples.data() + begin,
                          static_cast<std::int32_t>(finish - begin));
    stream.InputFinished();

    if (!impl_->extractor->IsReady(&stream)) {
        if (error) {
            *error = "not enough audio for an embedding";
        }
        return false;
    }

    *out = impl_->extractor->ComputeEmbedding(&stream);
    if (out->empty()) {
        if (error) {
            *error = "embedding extraction returned nothing";
        }
        return false;
    }
    l2_normalise(*out);
    return true;
}

bool Embedder::build_voiceprints(const std::vector<float> &samples,
                                 const std::vector<DiarizedTurn> &turns, std::int64_t file_id,
                                 std::vector<LocalSpeaker> *out, std::string *error) {
    out->clear();
    if (turns.empty()) {
        return true;
    }

    std::map<int, std::vector<DiarizedTurn>> by_speaker;
    for (const auto &t : turns) {
        by_speaker[t.speaker].push_back(t);
    }

    for (auto &[speaker, spans] : by_speaker) {
        double total = 0.0;
        for (const auto &s : spans) {
            total += s.duration();
        }

        std::sort(spans.begin(), spans.end(),
                  [](const DiarizedTurn &a, const DiarizedTurn &b) {
                      return a.duration() > b.duration();
                  });

        std::vector<DiarizedTurn> chosen;
        for (const auto &s : spans) {
            if (s.duration() >= impl_->cfg.embed_min_segment) {
                chosen.push_back(s);
            }
            if (static_cast<int>(chosen.size()) >= impl_->cfg.embed_max_segments) {
                break;
            }
        }

        // A speaker with only short turns still needs an identity, otherwise
        // every brief contributor in the corpus is silently unattributable.
        // The voiceprint is weaker, which the similarity score then reflects.
        if (chosen.empty()) {
            for (const auto &s : spans) {
                if (s.duration() >= kAbsoluteMinimumSpan) {
                    chosen.push_back(s);
                }
                if (static_cast<int>(chosen.size()) >= impl_->cfg.embed_max_segments) {
                    break;
                }
            }
        }
        if (chosen.empty()) {
            continue;
        }

        std::vector<std::vector<float>> vectors;
        std::vector<double> weights;
        for (const auto &s : chosen) {
            std::vector<float> vec;
            std::string span_error;
            if (!embed_span(samples, s.start, s.end, &vec, &span_error)) {
                continue;
            }
            vectors.push_back(std::move(vec));
            weights.push_back(s.duration());
        }
        if (vectors.empty()) {
            continue;
        }

        LocalSpeaker sp;
        sp.file_id = file_id;
        sp.label = local_label_for(speaker);
        sp.centroid = centroid_of(vectors, weights);
        sp.n_segments = static_cast<int>(vectors.size());
        sp.total_duration = total;
        out->push_back(std::move(sp));
    }

    if (out->empty() && error) {
        *error = "no usable speech for speaker embedding";
    }
    return true;
}

}  // namespace scribe
