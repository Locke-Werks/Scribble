#include "separate.hpp"

#include <algorithm>

#include "paths.hpp"
#include "sherpa-onnx/c-api/cxx-api.h"

namespace scribble {
namespace {

namespace sx = sherpa_onnx::cxx;


/// UVR returns the vocal stem first and the accompaniment second.
constexpr int kVocalStem = 0;

}  // namespace

struct Separator::Impl {
    std::unique_ptr<sx::OfflineSourceSeparation> engine;
    int sample_rate = 44100;
};

Separator::Separator() : impl_(std::make_unique<Impl>()) {}
Separator::~Separator() = default;

std::unique_ptr<Separator> Separator::create(const Config &cfg, const fs::path &model_file,
                                             std::string *error) {
    std::error_code ec;
    if (!fs::exists(model_file, ec)) {
        if (error) {
            *error = "separation model not found: " + model_file.string();
        }
        return nullptr;
    }

    sx::OfflineSourceSeparationConfig config;
    config.model.uvr.model = model_file.string();
    config.model.num_threads = std::max(1, cfg.n_threads / 2);
    config.model.provider = onnx_large_model_provider(cfg.isolate_accel);

    auto engine = sx::OfflineSourceSeparation::Create(config);
    if (!engine.Get()) {
        if (error) {
            *error = "cannot initialise source separation";
        }
        return nullptr;
    }

    std::unique_ptr<Separator> self(new Separator());
    self->impl_->sample_rate = engine.GetOutputSampleRate();
    if (self->impl_->sample_rate <= 0) {
        self->impl_->sample_rate = 44100;
    }
    self->impl_->engine =
        std::make_unique<sx::OfflineSourceSeparation>(std::move(engine));
    return self;
}

int Separator::sample_rate() const { return impl_->sample_rate; }

bool Separator::isolate_vocals(const WavData &input, WavData *vocals, std::string *error) {
    if (!impl_->engine) {
        if (error) {
            *error = "separator not initialised";
        }
        return false;
    }
    if (input.planar.empty() || input.frames() == 0) {
        if (error) {
            *error = "no audio to separate";
        }
        return false;
    }

    // The models are trained on stereo. A mono source is duplicated rather than
    // passed through as one channel, which the model does not expect.
    std::vector<const float *> channels;
    if (input.planar.size() == 1) {
        channels = {input.planar[0].data(), input.planar[0].data()};
    } else {
        channels = {input.planar[0].data(), input.planar[1].data()};
    }

    auto output = impl_->engine->Process(channels.data(), 2,
                                         static_cast<std::int32_t>(input.frames()),
                                         input.sample_rate);

    if (output.stems.size() <= kVocalStem || output.stems[kVocalStem].samples.empty()) {
        if (error) {
            *error = "separation produced no vocal stem";
        }
        return false;
    }

    vocals->sample_rate = output.sample_rate > 0 ? output.sample_rate : impl_->sample_rate;
    vocals->planar = output.stems[kVocalStem].samples;
    vocals->channels = static_cast<int>(vocals->planar.size());
    return true;
}

}  // namespace scribble
