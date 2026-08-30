#include "asr.hpp"

#include <algorithm>
#include <cstring>

#include "media.hpp"
#include "paths.hpp"
#include "sherpa-onnx/c-api/cxx-api.h"
#include "util.hpp"
#include "whisper.h"

namespace scribble {
namespace {

namespace sx = sherpa_onnx::cxx;

const char *onnx_provider_for(Accel accel) {
    if (accel == Accel::Cpu) {
        return "cpu";
    }
    // Asking for CUDA when its provider cannot load aborts the process rather
    // than degrading, so availability is probed instead of assumed.
    return onnx_cuda_available() ? "cuda" : "cpu";
}

/// Finds a model component inside an unpacked transducer directory. Upstream
/// varies the exact filenames between quantised and full releases, so the
/// component is matched by prefix rather than assumed.
fs::path find_component(const fs::path &dir, const std::string &prefix) {
    std::error_code ec;
    fs::path best;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string name = to_lower(entry.path().filename().string());
        if (starts_with(name, prefix) && name.size() > 5 &&
            name.substr(name.size() - 5) == ".onnx") {
            // Prefer the int8 export when both are present: it is the one the
            // archive is built around and the accuracy difference is marginal.
            if (best.empty() || name.find("int8") != std::string::npos) {
                best = entry.path();
            }
        }
    }
    return best;
}

/// NeMo BPE marks a word start with U+2581, the same role a leading space
/// plays in Whisper's vocabulary.
constexpr const char *kWordStart = "\xe2\x96\x81";

bool is_word_start(const std::string &token) {
    return token.rfind(kWordStart, 0) == 0;
}

std::string strip_word_marker(const std::string &token) {
    if (is_word_start(token)) {
        return token.substr(std::strlen(kWordStart));
    }
    return token;
}

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
    AsrBackend backend = AsrBackend::Whisper;

    whisper_context *ctx = nullptr;

    std::unique_ptr<sx::OfflineRecognizer> recognizer;
    std::unique_ptr<sx::VadModelConfig> vad_config;
    fs::path vad_model;

    Config cfg;
    std::string model_name;
    bool gpu = false;
    std::string prompt;
    std::string whisper_vad_model;
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
    self->impl_->backend = AsrBackend::Whisper;
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

void Transcriber::set_vad_model(const fs::path &model_file) {
    impl_->whisper_vad_model = model_file.string();
}

std::unique_ptr<Transcriber> Transcriber::create_parakeet(const Config &cfg,
                                                          const fs::path &model_dir,
                                                          const fs::path &vad_model,
                                                          std::string *error) {
    std::error_code ec;
    if (!fs::is_directory(model_dir, ec)) {
        if (error) {
            *error = "parakeet model directory not found: " + model_dir.string();
        }
        return nullptr;
    }

    const fs::path encoder = find_component(model_dir, "encoder");
    const fs::path decoder = find_component(model_dir, "decoder");
    const fs::path joiner = find_component(model_dir, "joiner");
    const fs::path tokens = model_dir / "tokens.txt";

    if (encoder.empty() || decoder.empty() || joiner.empty() || !fs::exists(tokens, ec)) {
        if (error) {
            *error = "incomplete parakeet model in " + model_dir.string() +
                     " (need encoder, decoder, joiner and tokens.txt)";
        }
        return nullptr;
    }
    if (!fs::exists(vad_model, ec)) {
        if (error) {
            *error = "VAD model not found: " + vad_model.string();
        }
        return nullptr;
    }

    sx::OfflineRecognizerConfig config;
    config.model_config.transducer.encoder = encoder.string();
    config.model_config.transducer.decoder = decoder.string();
    config.model_config.transducer.joiner = joiner.string();
    config.model_config.tokens = tokens.string();
    config.model_config.model_type = "nemo_transducer";
    config.model_config.num_threads = std::max(1, cfg.n_threads / 2);
    config.decoding_method = "greedy_search";

    const bool want_gpu = cfg.asr_accel != Accel::Cpu;
    const char *provider = onnx_provider_for(cfg.asr_accel);
    const bool on_gpu = std::strcmp(provider, "cuda") == 0;

    config.model_config.provider = provider;
    if (!on_gpu) {
        // Parakeet on CPU is slower than realtime, so give it every core rather
        // than the half-share that assumes a GPU is carrying the load.
        config.model_config.num_threads = std::max(1, cfg.n_threads);
    }

    auto recognizer = sx::OfflineRecognizer::Create(config);
    if (!recognizer.Get()) {
        if (error) {
            *error = "cannot initialise the parakeet recognizer";
        }
        return nullptr;
    }

    std::unique_ptr<Transcriber> self(new Transcriber());
    self->impl_->backend = AsrBackend::Parakeet;
    self->impl_->cfg = cfg;
    self->impl_->model_name = model_dir.filename().string();
    self->impl_->gpu = on_gpu;
    self->impl_->recognizer =
        std::make_unique<sx::OfflineRecognizer>(std::move(recognizer));
    self->impl_->vad_model = vad_model;

    if (want_gpu && !on_gpu) {
        // Not fatal, but the reason to pick Parakeet over Whisper is speed, and
        // on CPU it does not have any.
        self->impl_->model_name += " (CPU, cuDNN not installed)";
    }
    return self;
}

namespace {

/// Decodes one VAD-delimited utterance and turns the token stream into a
/// Segment with word timings.
Segment decode_utterance(sx::OfflineRecognizer &recognizer, const std::vector<float> &samples,
                         double offset, int index) {
    auto stream = recognizer.CreateStream();
    stream.AcceptWaveform(kSampleRate, samples.data(),
                          static_cast<std::int32_t>(samples.size()));
    recognizer.Decode(&stream);
    auto result = recognizer.GetResult(&stream);

    Segment seg;
    seg.index = index;
    seg.start = offset;
    seg.end = offset + static_cast<double>(samples.size()) / kSampleRate;
    seg.text = trim(result.text);

    for (size_t i = 0; i < result.tokens.size(); ++i) {
        const std::string &token = result.tokens[i];
        if (token.empty()) {
            continue;
        }
        const double start =
            i < result.timestamps.size() ? offset + result.timestamps[i] : seg.start;
        // TDT models report a duration per token. Without it the only honest
        // end time is the next token's start, filled in below.
        const double end =
            i < result.durations.size() ? start + result.durations[i] : start;

        if (is_word_start(token) || seg.words.empty()) {
            Word w;
            w.text = strip_word_marker(token);
            w.start = start;
            w.end = end;
            w.probability = 1.0f;
            if (!w.text.empty()) {
                seg.words.push_back(std::move(w));
            }
        } else {
            Word &w = seg.words.back();
            w.text += token;
            w.end = std::max(w.end, end);
        }
    }

    for (size_t i = 0; i + 1 < seg.words.size(); ++i) {
        if (seg.words[i].end <= seg.words[i].start) {
            seg.words[i].end = seg.words[i + 1].start;
        }
    }
    if (!seg.words.empty() && seg.words.back().end <= seg.words.back().start) {
        seg.words.back().end = seg.end;
    }

    return seg;
}

}  // namespace

bool Transcriber::transcribe_parakeet(const std::vector<float> &samples,
                                      const std::function<void(const Segment &)> &on_segment,
                                      const std::function<void(double)> &on_progress_fn,
                                      const CancelToken &cancel, Result *result,
                                      std::string *error) {
    sx::VadModelConfig vad_config;
    vad_config.silero_vad.model = impl_->vad_model.string();
    vad_config.silero_vad.threshold = 0.5f;
    // Loose enough not to split a sentence at an ordinary breath. Splitting
    // mid-phrase costs the recognizer the context it needs at both new edges.
    vad_config.silero_vad.min_silence_duration = 0.55f;
    vad_config.silero_vad.min_speech_duration = 0.25f;
    // Utterances longer than this are cut regardless. An unbounded segment on
    // continuous speech would grow until the recognizer ran out of memory.
    vad_config.silero_vad.max_speech_duration = 25.0f;
    vad_config.sample_rate = kSampleRate;
    vad_config.num_threads = 1;
    vad_config.provider = "cpu";

    auto vad = sx::VoiceActivityDetector::Create(vad_config, 60.0f);
    if (!vad.Get()) {
        if (error) {
            *error = "cannot initialise voice activity detection";
        }
        return false;
    }

    const std::int32_t window = kSampleRate / 2;
    // The VAD trims to where speech is confidently present, which clips the
    // onset and tail of an utterance. Feeding the recognizer a padded slice of
    // the original audio instead recovers the words that would otherwise be
    // lost at every boundary.
    const size_t pad = static_cast<size_t>(kSampleRate * 0.3);
    int index = 0;
    size_t offset = 0;

    const auto drain = [&](bool flush) {
        if (flush) {
            vad.Flush();
        }
        while (!vad.IsEmpty()) {
            auto segment = vad.Front();
            vad.Pop();
            if (segment.samples.empty()) {
                continue;
            }

            const auto raw_start = static_cast<size_t>(std::max(0, segment.start));
            const size_t begin = raw_start > pad ? raw_start - pad : 0;
            const size_t finish =
                std::min(samples.size(), raw_start + segment.samples.size() + pad);
            if (finish <= begin) {
                continue;
            }

            std::vector<float> padded(samples.begin() + static_cast<std::ptrdiff_t>(begin),
                                      samples.begin() + static_cast<std::ptrdiff_t>(finish));

            Segment seg = decode_utterance(*impl_->recognizer, padded,
                                           static_cast<double>(begin) / kSampleRate, index);
            if (seg.text.empty()) {
                continue;
            }
            seg.index = index++;
            result->segments.push_back(seg);
            if (on_segment) {
                on_segment(seg);
            }
        }
    };

    while (offset < samples.size()) {
        if (!cancel.wait_if_paused()) {
            result->cancelled = true;
            return true;
        }
        const auto count =
            static_cast<std::int32_t>(std::min<size_t>(window, samples.size() - offset));
        vad.AcceptWaveform(samples.data() + offset, count);
        offset += static_cast<size_t>(count);

        drain(false);

        if (on_progress_fn) {
            on_progress_fn(static_cast<double>(offset) / static_cast<double>(samples.size()));
        }
    }

    drain(true);

    result->language = impl_->cfg.language.empty() ? std::string("en") : impl_->cfg.language;
    if (on_progress_fn) {
        on_progress_fn(1.0);
    }
    return true;
}

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

    if (impl_->backend == AsrBackend::Parakeet) {
        return transcribe_parakeet(samples, on_segment, on_progress_fn, cancel, result, error);
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

    // Whisper is a language model with an audio encoder, and on non-speech it
    // will happily invent a plausible sentence and then repeat it for the
    // length of the recording. The thresholds above only score a window after
    // it has been decoded; VAD stops the window from being decoded at all,
    // which is the difference between a garbage transcript and an empty one.
    if (cfg.vad_filter && !impl_->whisper_vad_model.empty()) {
        params.vad = true;
        params.vad_model_path = impl_->whisper_vad_model.c_str();
        params.vad_params = whisper_vad_default_params();
        params.vad_params.threshold = 0.5f;
        params.vad_params.min_speech_duration_ms = 250;
        params.vad_params.min_silence_duration_ms = 400;
        params.vad_params.max_speech_duration_s = 30.0f;
        // Padding and overlap keep the first and last word of each speech run,
        // which a tight VAD boundary otherwise clips.
        params.vad_params.speech_pad_ms = 300;
        params.vad_params.samples_overlap = 0.2f;
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

}  // namespace scribble
