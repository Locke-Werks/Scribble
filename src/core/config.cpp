#include "config.hpp"

#include <algorithm>
#include <sstream>
#include <thread>

#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

#include "paths.hpp"
#include "util.hpp"

namespace scribe {
namespace {

IsolateMode parse_isolate(std::string_view s, bool *ok) {
    *ok = true;
    if (iequals(s, "never"))  return IsolateMode::Never;
    if (iequals(s, "auto"))   return IsolateMode::Auto;
    if (iequals(s, "always")) return IsolateMode::Always;
    *ok = false;
    return IsolateMode::Auto;
}

const char *isolate_name(IsolateMode m) {
    switch (m) {
        case IsolateMode::Never:  return "never";
        case IsolateMode::Auto:   return "auto";
        case IsolateMode::Always: return "always";
    }
    return "auto";
}

AsrBackend parse_backend(std::string_view s, bool *ok) {
    *ok = true;
    if (iequals(s, "whisper"))  return AsrBackend::Whisper;
    if (iequals(s, "parakeet")) return AsrBackend::Parakeet;
    *ok = false;
    return AsrBackend::Whisper;
}

const char *backend_name(AsrBackend b) {
    return b == AsrBackend::Parakeet ? "parakeet" : "whisper";
}

Accel parse_accel(std::string_view s, bool *ok) {
    *ok = true;
    if (iequals(s, "auto")) return Accel::Auto;
    if (iequals(s, "cpu"))  return Accel::Cpu;
    if (iequals(s, "cuda") || iequals(s, "gpu")) return Accel::Cuda;
    *ok = false;
    return Accel::Auto;
}

const char *accel_name(Accel a) {
    switch (a) {
        case Accel::Auto: return "auto";
        case Accel::Cpu:  return "cpu";
        case Accel::Cuda: return "cuda";
    }
    return "auto";
}

GpuRuntimeMode parse_gpu_runtime(std::string_view s, bool *ok) {
    *ok = true;
    if (iequals(s, "prompt")) return GpuRuntimeMode::Prompt;
    if (iequals(s, "auto"))   return GpuRuntimeMode::Auto;
    if (iequals(s, "never"))  return GpuRuntimeMode::Never;
    *ok = false;
    return GpuRuntimeMode::Prompt;
}

const char *gpu_runtime_name(GpuRuntimeMode m) {
    switch (m) {
        case GpuRuntimeMode::Prompt: return "prompt";
        case GpuRuntimeMode::Auto:   return "auto";
        case GpuRuntimeMode::Never:  return "never";
    }
    return "prompt";
}

std::vector<std::string> string_array(const toml::node *node, bool *ok) {
    std::vector<std::string> out;
    *ok = true;
    const auto *arr = node->as_array();
    if (arr == nullptr) {
        *ok = false;
        return out;
    }
    for (const auto &item : *arr) {
        if (const auto *sv = item.as_string()) {
            out.push_back(sv->get());
        } else {
            *ok = false;
        }
    }
    return out;
}

}  // namespace

Config Config::load(const fs::path &file, std::string *error) {
    Config cfg;
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return cfg;
    }

    auto result = toml::parse_file(file.string());
    if (!result) {
        if (error) {
            std::ostringstream ss;
            ss << "config parse error at line " << result.error().source().begin.line << ": "
               << result.error().description();
            *error = ss.str();
        }
        return cfg;
    }

    const toml::table &tbl = result.table();
    std::vector<std::string> problems;

    auto get_string = [&](const char *key, std::string &dest) {
        if (const auto *n = tbl.get(key)) {
            if (const auto *sv = n->as_string()) {
                dest = sv->get();
            } else {
                problems.push_back(std::string(key) + " must be a string");
            }
        }
    };
    auto get_bool = [&](const char *key, bool &dest) {
        if (const auto *n = tbl.get(key)) {
            if (const auto *bv = n->as_boolean()) {
                dest = bv->get();
            } else {
                problems.push_back(std::string(key) + " must be true or false");
            }
        }
    };
    auto get_int = [&](const char *key, int &dest) {
        if (const auto *n = tbl.get(key)) {
            if (const auto *iv = n->as_integer()) {
                dest = static_cast<int>(iv->get());
            } else {
                problems.push_back(std::string(key) + " must be an integer");
            }
        }
    };
    auto get_float = [&](const char *key, float &dest) {
        if (const auto *n = tbl.get(key)) {
            if (const auto *fv = n->as_floating_point()) {
                dest = static_cast<float>(fv->get());
            } else if (const auto *iv = n->as_integer()) {
                dest = static_cast<float>(iv->get());
            } else {
                problems.push_back(std::string(key) + " must be a number");
            }
        }
    };
    auto get_double = [&](const char *key, double &dest) {
        if (const auto *n = tbl.get(key)) {
            if (const auto *fv = n->as_floating_point()) {
                dest = fv->get();
            } else if (const auto *iv = n->as_integer()) {
                dest = static_cast<double>(iv->get());
            } else {
                problems.push_back(std::string(key) + " must be a number");
            }
        }
    };
    auto get_path = [&](const char *key, fs::path &dest) {
        if (const auto *n = tbl.get(key)) {
            if (const auto *sv = n->as_string()) {
                dest = fs::path(sv->get());
            } else {
                problems.push_back(std::string(key) + " must be a string path");
            }
        }
    };
    auto get_strings = [&](const char *key, std::vector<std::string> &dest) {
        if (const auto *n = tbl.get(key)) {
            bool ok = false;
            auto values = string_array(n, &ok);
            if (ok) {
                dest = std::move(values);
            } else {
                problems.push_back(std::string(key) + " must be an array of strings");
            }
        }
    };
    auto get_enum = [&](const char *key, auto parser, auto &dest) {
        if (const auto *n = tbl.get(key)) {
            const auto *sv = n->as_string();
            if (sv == nullptr) {
                problems.push_back(std::string(key) + " must be a string");
                return;
            }
            bool ok = false;
            auto value = parser(sv->get(), &ok);
            if (ok) {
                dest = value;
            } else {
                problems.push_back(std::string(key) + " has an unrecognised value: " + sv->get());
            }
        }
    };

    get_path("out_dir", cfg.out_dir);
    get_path("work_dir", cfg.work_dir);
    get_path("db_path", cfg.db_path);
    get_path("model_dir", cfg.model_dir);
    get_bool("mirror_tree", cfg.mirror_tree);
    get_bool("keep_work", cfg.keep_work);

    get_enum("backend", parse_backend, cfg.backend);
    get_string("model", cfg.model);
    get_enum("asr_accel", parse_accel, cfg.asr_accel);
    get_string("language", cfg.language);
    get_bool("translate", cfg.translate);
    get_int("beam_size", cfg.beam_size);
    get_int("n_threads", cfg.n_threads);
    get_bool("condition_on_previous_text", cfg.condition_on_previous_text);
    get_bool("temperature_fallback", cfg.temperature_fallback);
    get_float("entropy_threshold", cfg.entropy_threshold);
    get_float("logprob_threshold", cfg.logprob_threshold);
    get_float("no_speech_threshold", cfg.no_speech_threshold);
    get_bool("word_timestamps", cfg.word_timestamps);
    get_bool("suppress_non_speech", cfg.suppress_non_speech);
    get_strings("hotwords", cfg.hotwords);
    get_string("initial_prompt", cfg.initial_prompt);

    get_enum("isolate", parse_isolate, cfg.isolate);
    get_string("isolate_model", cfg.isolate_model);
    get_float("isolate_auto_threshold", cfg.isolate_auto_threshold);

    get_bool("diarize", cfg.diarize);
    get_enum("onnx_accel", parse_accel, cfg.onnx_accel);
    get_enum("isolate_accel", parse_accel, cfg.isolate_accel);
    get_enum("gpu_runtime", parse_gpu_runtime, cfg.gpu_runtime);
    get_string("segmentation_model", cfg.segmentation_model);
    get_string("embedding_model", cfg.embedding_model);
    get_int("num_speakers", cfg.num_speakers);
    get_int("min_speakers", cfg.min_speakers);
    get_int("max_speakers", cfg.max_speakers);
    get_float("diar_cluster_threshold", cfg.diar_cluster_threshold);

    get_double("embed_min_segment", cfg.embed_min_segment);
    get_int("embed_max_segments", cfg.embed_max_segments);
    get_float("match_threshold", cfg.match_threshold);
    get_float("review_threshold", cfg.review_threshold);
    get_float("cluster_threshold", cfg.cluster_threshold);

    get_bool("split_channels", cfg.split_channels);
    get_int("max_split_channels", cfg.max_split_channels);
    get_float("channel_dup_correlation", cfg.channel_dup_correlation);

    get_strings("formats", cfg.formats);
    get_bool("overwrite", cfg.overwrite);
    get_bool("recursive", cfg.recursive);
    get_int("decode_lookahead", cfg.decode_lookahead);

    // An unknown key is almost always a typo in a tuning parameter, and a
    // silently ignored setting produces a confusing run rather than an error.
    static const std::vector<std::string> known = {
        "out_dir", "work_dir", "db_path", "model_dir", "mirror_tree", "keep_work",
        "backend", "model", "asr_accel", "language", "translate", "beam_size",
        "n_threads", "condition_on_previous_text", "temperature_fallback",
        "entropy_threshold", "logprob_threshold", "no_speech_threshold",
        "word_timestamps", "suppress_non_speech", "hotwords", "initial_prompt",
        "isolate", "isolate_model", "isolate_auto_threshold",
        "diarize", "onnx_accel", "isolate_accel", "gpu_runtime", "segmentation_model",
        "embedding_model",
        "num_speakers", "min_speakers", "max_speakers", "diar_cluster_threshold",
        "embed_min_segment", "embed_max_segments", "match_threshold",
        "review_threshold", "cluster_threshold",
        "split_channels", "max_split_channels", "channel_dup_correlation",
        "formats", "overwrite", "recursive", "decode_lookahead",
    };
    for (const auto &[key, _] : tbl) {
        std::string name(key.str());
        if (std::find(known.begin(), known.end(), name) == known.end()) {
            problems.push_back("unknown key: " + name);
        }
    }

    if (!problems.empty() && error) {
        *error = join(problems, "; ");
    }
    return cfg;
}

void Config::save(const fs::path &file) const {
    std::ostringstream out;
    out << "# ScribeEveryone configuration\n\n";

    auto quote = [](const std::string &s) {
        return "\"" + replace_all(replace_all(s, "\\", "\\\\"), "\"", "\\\"") + "\"";
    };
    auto path_str = [&](const fs::path &p) { return quote(p.generic_string()); };
    auto array = [&](const std::vector<std::string> &v) {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) s += ", ";
            s += quote(v[i]);
        }
        return s + "]";
    };

    out << "out_dir      = " << path_str(out_dir) << "\n";
    out << "work_dir     = " << path_str(work_dir) << "\n";
    out << "db_path      = " << path_str(db_path) << "\n";
    out << "model_dir    = " << path_str(model_dir) << "\n";
    out << "mirror_tree  = " << (mirror_tree ? "true" : "false") << "\n";
    out << "keep_work    = " << (keep_work ? "true" : "false") << "\n\n";

    out << "backend                    = " << quote(backend_name(backend)) << "\n";
    out << "model                      = " << quote(model) << "\n";
    out << "asr_accel                  = " << quote(accel_name(asr_accel)) << "\n";
    out << "language                   = " << quote(language) << "\n";
    out << "translate                  = " << (translate ? "true" : "false") << "\n";
    out << "beam_size                  = " << beam_size << "\n";
    out << "n_threads                  = " << n_threads << "\n";
    out << "condition_on_previous_text = " << (condition_on_previous_text ? "true" : "false")
        << "\n";
    out << "temperature_fallback       = " << (temperature_fallback ? "true" : "false") << "\n";
    out << "entropy_threshold          = " << entropy_threshold << "\n";
    out << "logprob_threshold          = " << logprob_threshold << "\n";
    out << "no_speech_threshold        = " << no_speech_threshold << "\n";
    out << "word_timestamps            = " << (word_timestamps ? "true" : "false") << "\n";
    out << "suppress_non_speech        = " << (suppress_non_speech ? "true" : "false") << "\n";
    out << "hotwords                   = " << array(hotwords) << "\n";
    out << "initial_prompt             = " << quote(initial_prompt) << "\n\n";

    out << "isolate                = " << quote(isolate_name(isolate)) << "\n";
    out << "isolate_model          = " << quote(isolate_model) << "\n";
    out << "isolate_accel          = " << quote(accel_name(isolate_accel)) << "\n";
    out << "isolate_auto_threshold = " << isolate_auto_threshold << "\n\n";

    out << "diarize                = " << (diarize ? "true" : "false") << "\n";
    out << "onnx_accel             = " << quote(accel_name(onnx_accel)) << "\n";
    out << "gpu_runtime            = " << quote(gpu_runtime_name(gpu_runtime)) << "\n";
    out << "segmentation_model     = " << quote(segmentation_model) << "\n";
    out << "embedding_model        = " << quote(embedding_model) << "\n";
    out << "num_speakers           = " << num_speakers << "\n";
    out << "min_speakers           = " << min_speakers << "\n";
    out << "max_speakers           = " << max_speakers << "\n";
    out << "diar_cluster_threshold = " << diar_cluster_threshold << "\n\n";

    out << "embed_min_segment  = " << embed_min_segment << "\n";
    out << "embed_max_segments = " << embed_max_segments << "\n";
    out << "match_threshold    = " << match_threshold << "\n";
    out << "review_threshold   = " << review_threshold << "\n";
    out << "cluster_threshold  = " << cluster_threshold << "\n\n";

    out << "split_channels          = " << (split_channels ? "true" : "false") << "\n";
    out << "max_split_channels      = " << max_split_channels << "\n";
    out << "channel_dup_correlation = " << channel_dup_correlation << "\n\n";

    out << "formats          = " << array(formats) << "\n";
    out << "overwrite        = " << (overwrite ? "true" : "false") << "\n";
    out << "recursive        = " << (recursive ? "true" : "false") << "\n";
    out << "decode_lookahead = " << decode_lookahead << "\n";

    std::string error;
    write_file_atomic(file, out.str(), &error);
}

void Config::resolve(const fs::path &root) {
    auto absolutise = [&](fs::path &p) {
        if (!p.empty() && p.is_relative()) {
            p = fs::absolute(root / p);
        }
        p = p.lexically_normal();
    };

    // A path the user actually asked for stays relative to where they asked,
    // which keeps `--out out` meaning what it says from a shell. An unset path
    // becomes a per-user location that is writable no matter where the program
    // was started from.
    if (db_path.empty()) {
        db_path = scribe::app_data_dir() / "scribe.db";
    } else {
        absolutise(db_path);
    }
    if (work_dir.empty()) {
        work_dir = scribe::app_data_dir() / "work";
    } else {
        absolutise(work_dir);
    }
    if (out_dir.empty()) {
        out_dir = scribe::documents_dir() / "ScribeEveryone";
    } else {
        absolutise(out_dir);
    }

    if (model_dir.empty()) {
        model_dir = scribe::default_model_dir();
    } else {
        absolutise(model_dir);
    }

    if (n_threads <= 0) {
        auto hw = static_cast<int>(std::thread::hardware_concurrency());
        // Leaving a couple of cores free keeps the UI responsive and the
        // ffmpeg prefetch from starving while the encoder runs.
        n_threads = std::max(1, hw > 4 ? hw - 2 : hw);
    }
}

std::string Config::validate() const {
    std::vector<std::string> problems;

    if (beam_size < 1 || beam_size > 32) {
        problems.push_back("beam_size must be between 1 and 32");
    }
    if (match_threshold <= 0.0f || match_threshold >= 1.0f) {
        problems.push_back("match_threshold must be between 0 and 1");
    }
    if (review_threshold >= match_threshold) {
        problems.push_back("review_threshold must be below match_threshold, otherwise the "
                           "uncertain band is empty and no duplicates are ever reported");
    }
    if (cluster_threshold <= 0.0f || cluster_threshold >= 1.0f) {
        problems.push_back("cluster_threshold must be between 0 and 1");
    }
    if (embed_min_segment < 0.5) {
        problems.push_back("embed_min_segment below 0.5s produces voiceprints too noisy to "
                           "match reliably");
    }
    if (embed_max_segments < 1) {
        problems.push_back("embed_max_segments must be at least 1");
    }
    if (max_split_channels < 1) {
        problems.push_back("max_split_channels must be at least 1");
    }
    if (formats.empty()) {
        problems.push_back("at least one output format is required");
    }
    for (const auto &f : formats) {
        if (!iequals(f, "srt") && !iequals(f, "vtt") && !iequals(f, "md") &&
            !iequals(f, "json") && !iequals(f, "txt") && !iequals(f, "tsv")) {
            problems.push_back("unsupported output format: " + f);
        }
    }
    if (model.empty()) {
        problems.push_back("model must be set");
    }

    return join(problems, "; ");
}

}  // namespace scribe
