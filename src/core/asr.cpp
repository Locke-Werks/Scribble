#include "asr.hpp"

#include <algorithm>
#include <cstring>

#include "util.hpp"
#include "whisper.h"

namespace scribe {
namespace {

/// State shared with the whisper.cpp callbacks, which are plain C function
/// pointers and so cannot capture.
struct CallbackState {
    const std::function<void(const Segment &)> *on_segment = nullptr;
    const std::function<void(double)> *on_progress = nullptr;
    const CancelToken *cancel = nullptr;
    std::vector<Segment> *segments = nullptr;
    int next_index = 0;
    bool cancelled = false;
};

/// Rebuilds words from whisper's sub-word tokens. A token that does not begin
/// with a space continues the previous word, which is how byte-pair pieces
/// like "un" plus "likely" become one timed word rather than two.
std::vector<Word> words_from_tokens(whisper_context *ctx, int segment) {
    std::vector<Word> words;
    const int n = whisper_full_n_tokens(ctx, segment);

    for (int i = 0; i < n; ++i) {
        const whisper_token id = whisper_full_get_token_id(ctx, segment, i);
        if (id >= whisper_token_eot(ctx)) {
            continue;
        }
        const char *raw = whisper_full_get_token_text(ctx, segment, i);
        if (raw == nullptr || *raw == '\0') {
            continue;
        }
        const whisper_token_data data = whisper_full_get_token_data(ctx, segment, i);

        std::string piece(raw);
        const bool starts_word = piece.front() == ' ' || words.empty();

        if (starts_word) {
            Word w;
            w.text = trim(piece);
            w.start = static_cast<double>(data.t0) / 100.0;
            w.end = static_cast<double>(data.t1) / 100.0;
            w.probability = data.p;
            if (!w.text.empty()) {
                words.push_back(std::move(w));
            }
        } else {
            Word &w = words.back();
            w.text += piece;
            w.end = static_cast<double>(data.t1) / 100.0;
            w.probability = std::min(w.probability, data.p);
        }
    }
    return words;
}

void on_new_segment(whisper_context *ctx, whisper_state * /*state*/, int n_new, void *user) {
    auto *st = static_cast<CallbackState *>(user);
    const int total = whisper_full_n_segments(ctx);

    for (int i = total - n_new; i < total; ++i) {
        Segment seg;
        seg.index = st->next_index++;
        seg.start = static_cast<double>(whisper_full_get_segment_t0(ctx, i)) / 100.0;
        seg.end = static_cast<double>(whisper_full_get_segment_t1(ctx, i)) / 100.0;
        const char *text = whisper_full_get_segment_text(ctx, i);
        seg.text = trim(text ? text : "");
        seg.no_speech = whisper_full_get_segment_no_speech_prob(ctx, i);
        seg.words = words_from_tokens(ctx, i);

        if (!seg.words.empty()) {
            double sum = 0.0;
            for (const auto &w : seg.words) {
                sum += w.probability;
            }
            seg.avg_logprob = static_cast<float>(sum / static_cast<double>(seg.words.size()));
        }

        if (seg.text.empty()) {
            continue;
        }
        st->segments->push_back(seg);
        if (st->on_segment && *st->on_segment) {
            (*st->on_segment)(seg);
        }
    }
}

void on_progress(whisper_context * /*ctx*/, whisper_state * /*state*/, int progress,
                 void *user) {
    auto *st = static_cast<CallbackState *>(user);
    if (st->on_progress && *st->on_progress) {
        (*st->on_progress)(static_cast<double>(progress) / 100.0);
    }
}

bool on_abort(void *user) {
    auto *st = static_cast<CallbackState *>(user);
    if (st->cancel == nullptr) {
        return false;
    }
    if (!st->cancel->wait_if_paused()) {
        st->cancelled = true;
        return true;
    }
    return false;
}

}  // namespace

struct Transcriber::Impl {
    whisper_context *ctx = nullptr;
    Config cfg;
    std::string model_name;
    bool gpu = false;
    std::string prompt;
};

Transcriber::Transcriber() : impl_(std::make_unique<Impl>()) {}

Transcriber::~Transcriber() {
    if (impl_ && impl_->ctx) {
        whisper_free(impl_->ctx);
    }
}

std::unique_ptr<Transcriber> Transcriber::create(const Config &cfg, const fs::path &model_file,
                                                 std::string *error) {
    std::error_code ec;
    if (!fs::exists(model_file, ec)) {
        if (error) {
            *error = "model not found: " + model_file.string();
        }
        return nullptr;
    }

    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = cfg.asr_accel != Accel::Cpu;
    cparams.flash_attn = true;

    whisper_context *ctx =
        whisper_init_from_file_with_params(model_file.string().c_str(), cparams);
    if (ctx == nullptr) {
        if (error) {
            *error = "cannot load whisper model " + model_file.string();
        }
        return nullptr;
    }

    std::unique_ptr<Transcriber> self(new Transcriber());
    self->impl_->ctx = ctx;
    self->impl_->cfg = cfg;
    self->impl_->model_name = model_file.stem().string();
    self->impl_->gpu = cparams.use_gpu;

    // Whisper has no hotword parameter, so domain vocabulary rides in on the
    // initial prompt. It is the cheapest large accuracy gain available on
    // proper nouns, which are exactly the words a transcript is read for.
    std::string prompt = cfg.initial_prompt;
    if (!cfg.hotwords.empty()) {
        if (!prompt.empty()) {
            prompt += " ";
        }
        prompt += join(cfg.hotwords, ", ") + ".";
    }
    self->impl_->prompt = std::move(prompt);

    return self;
}

std::string Transcriber::model_name() const { return impl_->model_name; }

bool Transcriber::using_gpu() const { return impl_->gpu; }

bool Transcriber::transcribe(const std::vector<float> &samples,
                             const std::function<void(const Segment &)> &on_segment,
                             const std::function<void(double)> &on_progress_fn,
                             const CancelToken &cancel, Result *result, std::string *error) {
    if (samples.empty()) {
        if (error) {
            *error = "no audio samples";
        }
        return false;
    }

    const Config &cfg = impl_->cfg;

    whisper_full_params params = whisper_full_default_params(
        cfg.beam_size > 1 ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);

    params.n_threads = cfg.n_threads > 0 ? cfg.n_threads : 4;
    params.beam_search.beam_size = std::max(1, cfg.beam_size);
    params.translate = cfg.translate;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_special = false;
    params.print_timestamps = false;
    params.single_segment = false;
    params.token_timestamps = cfg.word_timestamps;
    params.suppress_nst = cfg.suppress_non_speech;

    // The repetition failure mode lives here. With context carried between
    // windows, one hallucinated loop propagates through the rest of a long
    // recording, so the batch default is to cut the chain.
    params.no_context = !cfg.condition_on_previous_text;

    params.entropy_thold = cfg.entropy_threshold;
    params.logprob_thold = cfg.logprob_threshold;
    params.no_speech_thold = cfg.no_speech_threshold;
    // A zero increment disables the fallback ladder entirely, so decoding a
    // bad window never gets a second attempt at a higher temperature.
    params.temperature_inc = cfg.temperature_fallback ? 0.2f : 0.0f;

    if (!cfg.language.empty()) {
        params.language = cfg.language.c_str();
        params.detect_language = false;
    } else {
        params.language = "auto";
        params.detect_language = false;
    }

    if (!impl_->prompt.empty()) {
        params.initial_prompt = impl_->prompt.c_str();
    }

    CallbackState state;
    state.on_segment = &on_segment;
    state.on_progress = &on_progress_fn;
    state.cancel = &cancel;
    state.segments = &result->segments;

    params.new_segment_callback = on_new_segment;
    params.new_segment_callback_user_data = &state;
    params.progress_callback = on_progress;
    params.progress_callback_user_data = &state;
    params.abort_callback = on_abort;
    params.abort_callback_user_data = &state;

    const int rc = whisper_full(impl_->ctx, params, samples.data(),
                                static_cast<int>(samples.size()));

    result->cancelled = state.cancelled;
    if (state.cancelled) {
        return true;
    }
    if (rc != 0) {
        if (error) {
            *error = "whisper_full failed with code " + std::to_string(rc);
        }
        return false;
    }

    const int lang_id = whisper_full_lang_id(impl_->ctx);
    if (lang_id >= 0) {
        const char *lang = whisper_lang_str(lang_id);
        result->language = lang ? lang : "";
    }
    return true;
}

}  // namespace scribe
