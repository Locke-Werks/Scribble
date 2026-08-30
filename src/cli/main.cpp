#include <atomic>
#include <csignal>
#include <cstdio>
#include <io.h>
#include <iostream>
#include <string>
#include <vector>

#include "config.hpp"
#include "db.hpp"
#include "gpu_runtime.hpp"
#include "models.hpp"
#include "paths.hpp"
#include "pipeline.hpp"
#include "scribe/version.hpp"
#include "speakers.hpp"
#include "util.hpp"

using namespace scribe;

namespace {

std::atomic<Pipeline *> g_pipeline{nullptr};

void handle_interrupt(int) {
    if (auto *p = g_pipeline.load()) {
        // Second interrupt kills the process outright. The first asks the
        // pipeline to finish the current file so the database is left clean.
        static std::atomic<bool> asked{false};
        if (asked.exchange(true)) {
            std::_Exit(130);
        }
        std::fprintf(stderr, "\nstopping after the current file, interrupt again to force\n");
        p->cancel().request_stop();
    }
}

/// Prints progress to a terminal. Keeps the transcript itself off stdout so
/// the output of a run can be piped without picking up status noise.
class ConsoleSink : public EventSink {
public:
    explicit ConsoleSink(bool verbose) : verbose_(verbose) {}

    void handle(const Event &event) override {
        if (const auto *e = std::get_if<EvFileStarted>(&event)) {
            clear_line();
            std::fprintf(stderr, "%s\n", fs::path(e->job.source_path).filename().string().c_str());
            return;
        }
        if (const auto *e = std::get_if<EvStage>(&event)) {
            if (e->fraction >= 0.0) {
                status("  %s %3d%%", stage_name(e->stage),
                       static_cast<int>(e->fraction * 100.0));
            } else {
                status("  %s", stage_name(e->stage));
            }
            return;
        }
        if (const auto *e = std::get_if<EvSpeakersResolved>(&event)) {
            clear_line();
            for (const auto &r : e->resolutions) {
                std::fprintf(stderr, "  %s -> %s%s (%.2f)\n", r.local_label.c_str(),
                             r.display.c_str(), r.minted ? " [new]" : "", r.similarity);
            }
            return;
        }
        if (const auto *e = std::get_if<EvFileFinished>(&event)) {
            clear_line();
            if (e->final_stage == Stage::Done) {
                std::fprintf(stderr, "  done in %s\n", format_duration(e->wall_seconds).c_str());
            }
            return;
        }
        if (const auto *e = std::get_if<EvModelDownload>(&event)) {
            if (e->bytes_total > 0) {
                status("  downloading %s %lld/%lld MB", e->name.c_str(),
                       static_cast<long long>(e->bytes_done / 1'000'000),
                       static_cast<long long>(e->bytes_total / 1'000'000));
            }
            return;
        }
        if (const auto *e = std::get_if<EvRunProgress>(&event)) {
            clear_line();
            std::fprintf(stderr, "[%d/%d] %.1fx realtime\n", e->files_done, e->files_total,
                         e->realtime_factor);
            return;
        }
        if (const auto *e = std::get_if<EvRunFinished>(&event)) {
            clear_line();
            std::fprintf(stderr, "\n%d done, %d failed, %d skipped in %s\n", e->files_done,
                         e->files_failed, e->files_skipped,
                         format_duration(e->wall_seconds).c_str());
            return;
        }
        if (const auto *e = std::get_if<EvLog>(&event)) {
            if (e->level == LogLevel::Debug && !verbose_) {
                return;
            }
            clear_line();
            std::fprintf(stderr, "  %s: %s\n", log_level_name(e->level), e->text.c_str());
            return;
        }
    }

private:
    template <class... Args>
    void status(const char *fmt, Args... args) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), fmt, args...);
        clear_line();
        std::fprintf(stderr, "%s", buf);
        std::fflush(stderr);
        width_ = static_cast<int>(std::string(buf).size());
    }

    void clear_line() {
        if (width_ > 0) {
            std::fprintf(stderr, "\r%*s\r", width_, "");
            width_ = 0;
        }
    }

    bool verbose_ = false;
    int width_ = 0;
};

void print_usage() {
    std::printf(
        "ScribeEveryone %s\n"
        "Batch transcription with diarization and corpus-wide speaker identity.\n"
        "\n"
        "Usage:\n"
        "  scribe run <path>...        transcribe files and folders\n"
        "  scribe speakers             list every speaker in the corpus\n"
        "  scribe name <id> <name>     name a speaker\n"
        "  scribe merge <from> <into>  fold one speaker into another\n"
        "  scribe dupes                report speakers that may be the same person\n"
        "  scribe dismiss <id> <id>    rule a pair out of future duplicate reports\n"
        "  scribe recluster            regroup every voiceprint, keeping names\n"
        "  scribe render               rewrite transcripts from the database\n"
        "  scribe models               list downloadable models\n"
        "  scribe gpu [install]        GPU acceleration for isolation and diarization\n"
        "\n"
        "Options:\n"
        "  --config <file>     configuration file (default scribe.toml)\n"
        "  --model <name>      transcription model (default large-v3)\n"
        "  --out <dir>         output directory\n"
        "  --db <file>         speaker and transcript database\n"
        "  --language <code>   force a language instead of detecting\n"
        "  --formats <list>    comma separated: srt,vtt,md,json,txt,tsv\n"
        "  --backend <name>    whisper or parakeet\n"
        "  --isolate <mode>    never, auto or always. Strips music and background\n"
        "  --gpu <mode>        prompt, auto or never. Downloads cuDNN for onnxruntime\n"
        "  --onnx-accel <dev>  auto, cpu or cuda for diarization and isolation\n"
        "  --asr-accel <dev>   auto, cpu or cuda for transcription\n"
        "  --threshold <n>     speaker match threshold, 0 to 1\n"
        "  --no-diarize        transcribe without speaker separation\n"
        "  --overwrite         redo files already marked done\n"
        "  --verbose           include backend debug output\n"
        "  --version           print version\n",
        SCRIBE_VERSION_STRING);
}

struct Args {
    std::string command;
    std::vector<std::string> positional;
    std::string config;
    std::string model;
    std::string out;
    std::string db;
    std::string language;
    std::string formats;
    std::string isolate;
    std::string backend;
    std::string gpu;
    std::string onnx_accel;
    std::string asr_accel;
    float threshold = -1.0f;
    bool no_diarize = false;
    bool overwrite = false;
    bool verbose = false;
    bool help = false;
    bool version = false;
};

bool parse_args(int argc, char **argv, Args *args, std::string *error) {
    auto value_for = [&](int &i, const char *name) -> std::string {
        if (i + 1 >= argc) {
            *error = std::string("missing value for ") + name;
            return {};
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            args->help = true;
        } else if (arg == "--version") {
            args->version = true;
        } else if (arg == "--verbose") {
            args->verbose = true;
        } else if (arg == "--no-diarize") {
            args->no_diarize = true;
        } else if (arg == "--overwrite") {
            args->overwrite = true;
        } else if (arg == "--config") {
            args->config = value_for(i, "--config");
        } else if (arg == "--model") {
            args->model = value_for(i, "--model");
        } else if (arg == "--out") {
            args->out = value_for(i, "--out");
        } else if (arg == "--db") {
            args->db = value_for(i, "--db");
        } else if (arg == "--language") {
            args->language = value_for(i, "--language");
        } else if (arg == "--formats") {
            args->formats = value_for(i, "--formats");
        } else if (arg == "--isolate") {
            args->isolate = value_for(i, "--isolate");
        } else if (arg == "--backend") {
            args->backend = value_for(i, "--backend");
        } else if (arg == "--gpu") {
            args->gpu = value_for(i, "--gpu");
        } else if (arg == "--onnx-accel") {
            args->onnx_accel = value_for(i, "--onnx-accel");
        } else if (arg == "--asr-accel") {
            args->asr_accel = value_for(i, "--asr-accel");
        } else if (arg == "--threshold") {
            args->threshold = std::stof(value_for(i, "--threshold"));
        } else if (!arg.empty() && arg.front() == '-') {
            *error = "unknown option: " + arg;
            return false;
        } else if (args->command.empty()) {
            args->command = arg;
        } else {
            args->positional.push_back(arg);
        }
        if (!error->empty()) {
            return false;
        }
    }
    return true;
}

int cmd_speakers(Database &db) {
    auto speakers = db.globals();
    if (speakers.empty()) {
        std::printf("no speakers yet\n");
        return 0;
    }
    std::printf("%-8s %-28s %6s %6s  %s\n", "id", "name", "files", "clips", "speech");
    for (const auto &s : speakers) {
        std::printf("%-8lld %-28s %6d %6d  %s\n", static_cast<long long>(s.id),
                    s.display().c_str(), s.n_files, s.n_locals,
                    format_duration(s.total_duration).c_str());
    }
    return 0;
}

int cmd_dupes(Database &db, const Config &cfg) {
    auto pairs = duplicate_candidates(db, cfg.review_threshold, cfg.match_threshold);
    if (pairs.empty()) {
        std::printf("no candidate duplicates between %.2f and %.2f\n", cfg.review_threshold,
                    cfg.match_threshold);
        return 0;
    }
    std::printf("Speakers close enough to be worth checking. The usual cause is one\n"
                "person recorded on different equipment.\n\n");
    for (const auto &p : pairs) {
        std::printf("  %.3f  %-8lld %-24s  <->  %-8lld %s\n", p.similarity,
                    static_cast<long long>(p.left), p.left_display.c_str(),
                    static_cast<long long>(p.right), p.right_display.c_str());
    }
    std::printf("\nMerge with: scribe merge <from> <into>\n");
    return 0;
}

int cmd_models() {
    std::printf("%-26s %-14s %8s  %s\n", "name", "kind", "size", "description");
    for (const auto &m : model_catalogue()) {
        const char *kind = m.kind == ModelKind::Whisper       ? "transcription"
                           : m.kind == ModelKind::Segmentation ? "segmentation"
                           : m.kind == ModelKind::Separation   ? "isolation"
                           : m.kind == ModelKind::Parakeet     ? "transcription"
                           : m.kind == ModelKind::Vad          ? "vad"
                                                               : "voiceprint";
        std::printf("%-26s %-14s %6lld MB  %s\n", m.name.c_str(), kind,
                    static_cast<long long>(m.approx_bytes / 1'000'000), m.description.c_str());
    }
    return 0;
}

/// Asks before starting a download measured in gigabytes. Returns false when
/// there is no terminal to ask, so an unattended run never blocks on stdin.
bool confirm(const std::string &question) {
    if (!_isatty(_fileno(stdin))) {
        return false;
    }
    std::fprintf(stderr, "%s [y/N] ", question.c_str());
    std::fflush(stderr);
    std::string answer;
    if (!std::getline(std::cin, answer)) {
        return false;
    }
    answer = to_lower(trim(answer));
    return answer == "y" || answer == "yes";
}

void report_gpu_progress(const std::string &component, std::int64_t done,
                         std::int64_t total) {
    if (total <= 0) {
        return;
    }
    std::fprintf(stderr, "\r  %-30s %5.1f / %.1f GB", component.c_str(),
                 static_cast<double>(done) / 1'000'000'000.0,
                 static_cast<double>(total) / 1'000'000'000.0);
    std::fflush(stderr);
}

int cmd_gpu(const Config &cfg, const std::vector<std::string> &args) {
    const bool install = !args.empty() && iequals(args[0], "install");

    auto status = gpu_runtime_status();
    std::printf("%s\n", status.summary().c_str());

    if (status.ready) {
        std::printf("Installed in %s\n", gpu_runtime_dir().string().c_str());
        return 0;
    }
    if (!install) {
        std::printf("\nWhisper already uses the GPU. This covers diarization, voiceprints,\n"
                    "isolation and Parakeet, which run through onnxruntime.\n"
                    "\n  scribe gpu install\n");
        return 0;
    }

    if (cfg.gpu_runtime == GpuRuntimeMode::Never) {
        std::fprintf(stderr, "gpu_runtime is set to never in the configuration.\n");
        return 1;
    }
    if (cfg.gpu_runtime == GpuRuntimeMode::Prompt &&
        !confirm("Download it from NVIDIA and Microsoft now?")) {
        std::printf("Nothing downloaded.\n");
        return 0;
    }

    CancelToken cancel;
    std::string error;
    if (!install_gpu_runtime(status, report_gpu_progress, cancel, &error)) {
        std::fprintf(stderr, "\n%s\n", error.c_str());
        return 1;
    }
    std::fprintf(stderr, "\n");
    std::printf("Installed in %s\n", gpu_runtime_dir().string().c_str());
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    Args args;
    std::string error;
    if (!parse_args(argc, argv, &args, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }
    if (args.version) {
        std::printf("ScribeEveryone %s\n", SCRIBE_VERSION_STRING);
        return 0;
    }
    if (args.help || args.command.empty()) {
        print_usage();
        return args.command.empty() && !args.help ? 2 : 0;
    }

    const fs::path root = fs::current_path();
    fs::path config_path = args.config.empty() ? root / "scribe.toml" : fs::path(args.config);
    if (args.config.empty() && !fs::exists(config_path)) {
        config_path = default_config_path();
    }

    std::string config_error;
    Config cfg = Config::load(config_path, &config_error);
    if (!config_error.empty()) {
        std::fprintf(stderr, "configuration problem: %s\n", config_error.c_str());
        return 2;
    }

    if (!args.model.empty())    cfg.model = args.model;
    if (!args.out.empty())      cfg.out_dir = args.out;
    if (!args.db.empty())       cfg.db_path = args.db;
    if (!args.language.empty()) cfg.language = args.language;
    if (!args.formats.empty())  cfg.formats = split(args.formats, ',');
    if (!args.isolate.empty()) {
        if (iequals(args.isolate, "never"))       cfg.isolate = IsolateMode::Never;
        else if (iequals(args.isolate, "auto"))   cfg.isolate = IsolateMode::Auto;
        else if (iequals(args.isolate, "always")) cfg.isolate = IsolateMode::Always;
        else {
            std::fprintf(stderr, "--isolate must be never, auto or always\n");
            return 2;
        }
    }
    if (!args.backend.empty()) {
        if (iequals(args.backend, "whisper")) {
            cfg.backend = AsrBackend::Whisper;
        } else if (iequals(args.backend, "parakeet")) {
            cfg.backend = AsrBackend::Parakeet;
        } else {
            std::fprintf(stderr, "--backend must be whisper or parakeet\n");
            return 2;
        }
    }
    if (!args.gpu.empty()) {
        if (iequals(args.gpu, "prompt"))     cfg.gpu_runtime = GpuRuntimeMode::Prompt;
        else if (iequals(args.gpu, "auto"))  cfg.gpu_runtime = GpuRuntimeMode::Auto;
        else if (iequals(args.gpu, "never")) cfg.gpu_runtime = GpuRuntimeMode::Never;
        else {
            std::fprintf(stderr, "--gpu must be prompt, auto or never\n");
            return 2;
        }
    }
    if (!args.onnx_accel.empty()) {
        if (iequals(args.onnx_accel, "auto"))      cfg.onnx_accel = Accel::Auto;
        else if (iequals(args.onnx_accel, "cpu"))  cfg.onnx_accel = Accel::Cpu;
        else if (iequals(args.onnx_accel, "cuda") || iequals(args.onnx_accel, "gpu"))
            cfg.onnx_accel = Accel::Cuda;
        else {
            std::fprintf(stderr, "--onnx-accel must be auto, cpu or cuda\n");
            return 2;
        }
    }
    if (!args.asr_accel.empty()) {
        if (iequals(args.asr_accel, "auto"))      cfg.asr_accel = Accel::Auto;
        else if (iequals(args.asr_accel, "cpu"))  cfg.asr_accel = Accel::Cpu;
        else if (iequals(args.asr_accel, "cuda") || iequals(args.asr_accel, "gpu"))
            cfg.asr_accel = Accel::Cuda;
        else {
            std::fprintf(stderr, "--asr-accel must be auto, cpu or cuda\n");
            return 2;
        }
    }
    if (args.threshold >= 0.0f) cfg.match_threshold = args.threshold;
    if (args.no_diarize)        cfg.diarize = false;
    if (args.overwrite)         cfg.overwrite = true;

    cfg.resolve(root);
    std::string invalid = cfg.validate();
    if (!invalid.empty()) {
        std::fprintf(stderr, "configuration problem: %s\n", invalid.c_str());
        return 2;
    }

    if (args.command == "models") {
        return cmd_models();
    }
    if (args.command == "gpu") {
        return cmd_gpu(cfg, args.positional);
    }

    // Offered before any model is loaded, so accepting does not mean waiting
    // through a download with a half-started batch behind it. Isolation is the
    // stage that actually benefits, so its setting drives the offer.
    if (args.command == "run" && cfg.gpu_runtime != GpuRuntimeMode::Never &&
        (cfg.isolate != IsolateMode::Never || cfg.onnx_accel == Accel::Cuda ||
         cfg.backend == AsrBackend::Parakeet)) {
        auto status = gpu_runtime_status();
        if (!status.ready) {
            std::fprintf(stderr, "%s\n", status.summary().c_str());
            const bool go = cfg.gpu_runtime == GpuRuntimeMode::Auto ||
                            confirm("Download it now? Declining runs these stages on CPU.");
            if (go) {
                CancelToken token;
                std::string gpu_error;
                if (install_gpu_runtime(status, report_gpu_progress, token, &gpu_error)) {
                    std::fprintf(stderr, "\nGPU runtime installed.\n");
                } else {
                    std::fprintf(stderr, "\n%s\nContinuing on CPU.\n", gpu_error.c_str());
                }
            }
        }
    }

    try {
        Database db(cfg.db_path);

        if (args.command == "speakers") {
            return cmd_speakers(db);
        }
        if (args.command == "dupes") {
            return cmd_dupes(db, cfg);
        }
        if (args.command == "name") {
            if (args.positional.size() < 2) {
                std::fprintf(stderr, "usage: scribe name <id> <name>\n");
                return 2;
            }
            db.rename_global(std::stoll(args.positional[0]),
                             join({args.positional.begin() + 1, args.positional.end()}, " "));
            std::printf("renamed\n");
            return 0;
        }
        if (args.command == "merge") {
            if (args.positional.size() != 2) {
                std::fprintf(stderr, "usage: scribe merge <from> <into>\n");
                return 2;
            }
            int moved = db.merge_globals(std::stoll(args.positional[0]),
                                         std::stoll(args.positional[1]));
            std::printf("moved %d voiceprints\n", moved);
            return 0;
        }
        if (args.command == "dismiss") {
            if (args.positional.size() != 2) {
                std::fprintf(stderr, "usage: scribe dismiss <id> <id>\n");
                return 2;
            }
            db.dismiss_pair(std::stoll(args.positional[0]), std::stoll(args.positional[1]));
            std::printf("pair will not be offered again\n");
            return 0;
        }

        ConsoleSink sink(args.verbose);
        Pipeline pipeline(cfg, db, sink);
        g_pipeline.store(&pipeline);
        std::signal(SIGINT, handle_interrupt);

        if (args.command == "recluster") {
            auto r = pipeline.recluster();
            std::printf("%d voiceprints, %d speakers before, %d after, %d reassigned\n",
                        r.locals, r.globals_before, r.globals_after, r.merged);
            return 0;
        }
        if (args.command == "render") {
            pipeline.rerender(true);
            return 0;
        }
        if (args.command == "run") {
            if (args.positional.empty()) {
                std::fprintf(stderr, "usage: scribe run <path>...\n");
                return 2;
            }
            std::vector<fs::path> paths;
            for (const auto &p : args.positional) {
                paths.emplace_back(p);
            }
            auto jobs = pipeline.enqueue(paths);
            if (jobs.empty()) {
                std::fprintf(stderr, "nothing to do\n");
                return 0;
            }
            std::fprintf(stderr, "%zu file%s queued\n", jobs.size(),
                         jobs.size() == 1 ? "" : "s");
            pipeline.run();
            return 0;
        }

        std::fprintf(stderr, "unknown command: %s\n", args.command.c_str());
        print_usage();
        return 2;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
