#include "pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <unordered_map>

#include "asr.hpp"
#include "db.hpp"
#include "diarize.hpp"
#include "embed.hpp"
#include "log.hpp"
#include "media.hpp"
#include "models.hpp"
#include "paths.hpp"
#include "render.hpp"
#include "separate.hpp"
#include "speakers.hpp"
#include "util.hpp"
#include "wavio.hpp"

namespace scribe {
namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

}  // namespace

struct Pipeline::Impl {
    Config cfg;
    Database &db;
    Reporter reporter;
    CancelToken cancel;

    std::vector<MediaJob> jobs;
    fs::path mirror_root;
    fs::path ffmpeg;
    fs::path ffprobe;

    std::unique_ptr<Transcriber> transcriber;
    std::unique_ptr<Diarizer> diarizer;
    std::unique_ptr<Embedder> embedder;
    std::unique_ptr<Separator> separator;
    bool separator_failed = false;

    Impl(Config c, Database &database, EventSink &sink)
        : cfg(std::move(c)), db(database), reporter(sink) {}

    bool ensure_backends(std::string *error);
    bool isolate(const MediaJob &job, const fs::path &target_wav,
                 std::vector<float> *samples, std::string *error);
    bool process(const MediaJob &job, std::string *error);
    void write_outputs(const MediaJob &job, const std::vector<Segment> &segments,
                       const std::string &language, std::vector<std::string> *written);
    std::string speaker_display(std::int64_t global_id, const std::string &local_label);
};

// ---------------------------------------------------------------------------

Pipeline::Pipeline(Config config, Database &db, EventSink &sink)
    : impl_(std::make_unique<Impl>(std::move(config), db, sink)) {
    impl_->ffmpeg = find_ffmpeg();
    impl_->ffprobe = find_ffprobe();
}

Pipeline::~Pipeline() = default;

CancelToken &Pipeline::cancel() noexcept { return impl_->cancel; }

std::string Pipeline::Impl::speaker_display(std::int64_t global_id,
                                            const std::string &local_label) {
    if (global_id < 0) {
        return local_label;
    }
    auto g = db.global(global_id);
    return g ? g->display() : local_label;
}

bool Pipeline::Impl::ensure_backends(std::string *error) {
    auto progress = [this](const std::string &name, std::int64_t done, std::int64_t total) {
        reporter.emit(EvModelDownload{name, done, total, total > 0 && done >= total});
    };

    if (!transcriber) {
        if (cfg.backend == AsrBackend::Parakeet) {
            fs::path model;
            fs::path vad;
            if (!resolve_model(cfg.model == "large-v3" ? "parakeet-tdt-0.6b-v3" : cfg.model,
                               ModelKind::Parakeet, cfg.model_dir, progress, &model, error)) {
                return false;
            }
            if (!resolve_model("silero-vad", ModelKind::Vad, cfg.model_dir, progress, &vad,
                               error)) {
                return false;
            }
            reporter.info("loading " + model.filename().string());
            transcriber = Transcriber::create_parakeet(cfg, model, vad, error);
        } else {
            fs::path model;
            if (!resolve_model(cfg.model, ModelKind::Whisper, cfg.model_dir, progress, &model,
                               error)) {
                return false;
            }
            reporter.info("loading " + model.filename().string());
            transcriber = Transcriber::create(cfg, model, error);
        }
        if (!transcriber) {
            return false;
        }
        reporter.info(std::string("transcription running on ") +
                      (transcriber->using_gpu() ? "GPU" : "CPU"));
    }

    if (cfg.diarize && !diarizer) {
        fs::path segmentation;
        fs::path embedding;
        if (!resolve_model(cfg.segmentation_model, ModelKind::Segmentation, cfg.model_dir,
                           progress, &segmentation, error)) {
            return false;
        }
        if (!resolve_model(cfg.embedding_model, ModelKind::Embedding, cfg.model_dir, progress,
                           &embedding, error)) {
            return false;
        }
        diarizer = Diarizer::create(cfg, segmentation, embedding, error);
        if (!diarizer) {
            return false;
        }
        embedder = Embedder::create(cfg, embedding, error);
        if (!embedder) {
            return false;
        }
    }
    return true;
}

std::vector<MediaJob> Pipeline::enqueue(const std::vector<fs::path> &paths) {
    auto &impl = *impl_;
    impl.jobs.clear();

    if (impl.ffprobe.empty() || impl.ffmpeg.empty()) {
        impl.reporter.error("ffmpeg and ffprobe are required but were not found on PATH");
        return {};
    }

    auto found = discover_media(paths, impl.cfg.recursive);
    if (found.empty()) {
        impl.reporter.warn("no media files found");
        return {};
    }
    impl.mirror_root = common_root(found);

    std::error_code ec;
    for (const auto &path : found) {
        if (impl.cancel.stop_requested()) {
            break;
        }

        MediaInfo info;
        std::string error;
        if (!probe_media(impl.ffprobe, path, &info, &error)) {
            impl.reporter.warn(path.filename().string() + ": " + error);
            continue;
        }

        // Separate microphone tracks are free perfect diarization, and
        // downmixing throws that away permanently. Duplicated mono, which is
        // common in exported video, is not worth splitting.
        int tracks = 1;
        if (impl.cfg.split_channels && info.looks_multitrack() &&
            info.channels <= impl.cfg.max_split_channels) {
            double correlation = channel_correlation(impl.ffmpeg, path, 120.0);
            if (correlation < impl.cfg.channel_dup_correlation) {
                tracks = info.channels;
                impl.reporter.info(path.filename().string() + ": " + std::to_string(tracks) +
                                   " separate tracks (correlation " +
                                   std::to_string(correlation).substr(0, 4) + ")");
            }
        }

        for (int track = 0; track < tracks; ++track) {
            FileRecord rec;
            rec.path = path.string();
            rec.track = track;
            rec.track_count = tracks;
            rec.size = static_cast<std::int64_t>(fs::file_size(path, ec));
            rec.mtime = static_cast<double>(
                fs::last_write_time(path, ec).time_since_epoch().count() / 10'000'000);
            rec.duration = info.duration;
            rec.channels = info.channels;

            bool changed = true;
            std::int64_t id = impl.db.upsert_file(rec, &changed);

            if (!changed && !impl.cfg.overwrite) {
                continue;
            }

            MediaJob job;
            job.file_id = id;
            job.source_path = path.string();
            job.track = track;
            job.track_count = tracks;
            job.duration = info.duration;
            job.channels = info.channels;

            impl.db.update_file_meta(id, info.duration, info.channels, "", "");
            impl.jobs.push_back(job);
            impl.reporter.emit(EvFileDiscovered{job});
        }
    }

    return impl.jobs;
}

bool Pipeline::Impl::isolate(const MediaJob &job, const fs::path &target_wav,
                             std::vector<float> *samples, std::string *error) {
    if (!separator) {
        auto progress = [this](const std::string &name, std::int64_t done,
                               std::int64_t total) {
            reporter.emit(EvModelDownload{name, done, total, total > 0 && done >= total});
        };
        fs::path model;
        if (!resolve_model(cfg.isolate_model, ModelKind::Separation, cfg.model_dir, progress,
                           &model, error)) {
            separator_failed = true;
            return false;
        }
        separator = Separator::create(cfg, model, error);
        if (!separator) {
            separator_failed = true;
            return false;
        }
    }

    const fs::path source(job.source_path);
    const int rate = separator->sample_rate();

    // Decoded again at the model's own rate rather than upsampled from the
    // 16 kHz copy: nothing is recovered by upsampling, and the model was
    // trained on full-bandwidth stereo.
    fs::path wide = cfg.work_dir / (target_wav.stem().string() + ".sep-in.wav");
    if (!decode_audio(ffmpeg, source, wide, job.track_count > 1 ? job.track : -1, rate, 2,
                      error)) {
        return false;
    }

    std::error_code ec;
    WavData input;
    if (!read_wav(wide, &input, error)) {
        fs::remove(wide, ec);
        return false;
    }

    WavData vocals;
    if (!separator->isolate_vocals(input, &vocals, error)) {
        fs::remove(wide, ec);
        return false;
    }
    fs::remove(wide, ec);

    fs::path stem_path = cfg.work_dir / (target_wav.stem().string() + ".vocals.wav");
    if (!write_wav(stem_path, vocals, error)) {
        return false;
    }

    // Back through ffmpeg for the downmix so the 16 kHz mono conversion stays
    // in one place instead of being reimplemented here.
    if (!decode_audio(ffmpeg, stem_path, target_wav, -1, kSampleRate, 1, error)) {
        fs::remove(stem_path, ec);
        return false;
    }
    fs::remove(stem_path, ec);

    return read_wav_mono16k(target_wav, samples, error);
}

bool Pipeline::Impl::process(const MediaJob &job, std::string *error) {
    const fs::path source(job.source_path);
    reporter.emit(EvFileStarted{job});

    // -- decode --------------------------------------------------------------
    reporter.stage(job.file_id, Stage::Extracting);
    fs::path wav = cfg.work_dir / (sanitise_filename(source.stem().string()) + "." +
                                   std::to_string(job.file_id) + ".wav");
    if (!decode_to_wav(ffmpeg, source, wav, job.track_count > 1 ? job.track : -1, error)) {
        return false;
    }

    std::vector<float> samples;
    if (!read_wav_mono16k(wav, &samples, error)) {
        return false;
    }
    if (!cancel.wait_if_paused()) {
        return false;
    }

    // -- optional isolation --------------------------------------------------
    if (cfg.isolate != IsolateMode::Never && !separator_failed) {
        const float floor = estimate_noise_floor(samples);
        const bool wanted =
            cfg.isolate == IsolateMode::Always || floor >= cfg.isolate_auto_threshold;
        if (wanted) {
            reporter.stage(job.file_id, Stage::Isolating, -1.0,
                           "noise floor " + std::to_string(floor).substr(0, 4));
            std::string isolate_error;
            if (isolate(job, wav, &samples, &isolate_error)) {
                reporter.info("isolated vocals", job.file_id);
            } else {
                // Isolation is an optimisation. Losing it costs accuracy on a
                // noisy file, but failing the job would cost the transcript.
                reporter.warn("isolation skipped: " + isolate_error, job.file_id);
            }
        }
    }

    // -- transcribe ----------------------------------------------------------
    reporter.stage(job.file_id, Stage::Transcribing, 0.0);
    Transcriber::Result asr;
    auto on_segment = [&](const Segment &seg) {
        reporter.emit(EvSegment{job.file_id, seg});
    };
    auto on_progress = [&](double fraction) {
        reporter.stage(job.file_id, Stage::Transcribing, fraction);
    };
    if (!transcriber->transcribe(samples, on_segment, on_progress, cancel, &asr, error)) {
        return false;
    }
    if (asr.cancelled) {
        if (error) {
            *error = "cancelled";
        }
        return false;
    }

    // -- diarize -------------------------------------------------------------
    std::vector<LocalSpeaker> locals;
    if (cfg.diarize && diarizer && !asr.segments.empty()) {
        reporter.stage(job.file_id, Stage::Diarizing, 0.0);
        std::vector<DiarizedTurn> turns;
        std::string diar_error;
        if (diarizer->diarize(samples, [&](double f) {
                reporter.stage(job.file_id, Stage::Diarizing, f);
            }, cancel, &turns, &diar_error)) {
            assign_speakers(asr.segments, turns);
            reporter.emit(EvSegmentsLabelled{job.file_id, asr.segments});

            reporter.stage(job.file_id, Stage::Embedding);
            std::string embed_error;
            if (!embedder->build_voiceprints(samples, turns, job.file_id, &locals,
                                             &embed_error) ||
                locals.empty()) {
                reporter.warn("no voiceprints extracted: " + embed_error, job.file_id);
            }
        } else {
            // A file that cannot be diarized is still worth its transcript, so
            // this degrades to unattributed text rather than failing the job.
            reporter.warn("diarization failed: " + diar_error, job.file_id);
        }
    }

    // -- resolve identities --------------------------------------------------
    if (!locals.empty()) {
        reporter.stage(job.file_id, Stage::Resolving);
        db.begin();
        db.clear_local_speakers(job.file_id);
        for (auto &local : locals) {
            local.id = db.insert_local_speaker(local);
        }

        MatchInput input;
        input.file_id = job.file_id;
        input.locals = locals;
        input.threshold = cfg.match_threshold;
        auto resolutions = resolve_against_store(db, input);
        db.commit();

        std::unordered_map<std::string, std::int64_t> by_label;
        for (const auto &r : resolutions) {
            by_label[r.local_label] = r.global_id;
        }
        for (auto &seg : asr.segments) {
            auto it = by_label.find(seg.local_label);
            if (it != by_label.end()) {
                seg.global_id = it->second;
            }
        }

        reporter.emit(EvSpeakersResolved{job.file_id, resolutions});
        reporter.emit(EvSegmentsLabelled{job.file_id, asr.segments});
    }

    // -- persist and render --------------------------------------------------
    reporter.stage(job.file_id, Stage::Writing);
    db.begin();
    db.clear_segments(job.file_id);
    db.insert_segments(job.file_id, asr.segments);
    db.update_file_meta(job.file_id, job.duration, job.channels, asr.language,
                        transcriber->model_name());
    db.commit();

    std::vector<std::string> written;
    write_outputs(job, asr.segments, asr.language, &written);

    if (!cfg.keep_work) {
        std::error_code ec;
        fs::remove(wav, ec);
    }
    return true;
}

void Pipeline::Impl::write_outputs(const MediaJob &job, const std::vector<Segment> &segments,
                                   const std::string &language,
                                   std::vector<std::string> *written) {
    const fs::path source(job.source_path);

    RenderContext ctx;
    ctx.source_path = job.source_path;
    ctx.source_name = source.stem().string();
    ctx.track = job.track;
    ctx.track_count = job.track_count;
    ctx.duration = job.duration;
    ctx.language = language;
    ctx.model = transcriber ? transcriber->model_name() : std::string{};
    ctx.speaker_name = [this](std::int64_t id, const std::string &label) {
        return speaker_display(id, label);
    };

    for (const auto &format : cfg.formats) {
        fs::path path = output_path(cfg.out_dir, source, job.track, job.track_count, format,
                                    cfg.mirror_tree, mirror_root);
        std::string error;
        if (write_transcript(format, segments, ctx, path, &error)) {
            db.record_output(job.file_id, to_lower(format), path.string());
            written->push_back(path.string());
        } else {
            reporter.warn("cannot write " + format + ": " + error, job.file_id);
        }
    }
}

void Pipeline::run() {
    auto &impl = *impl_;
    const auto run_start = Clock::now();

    install_backend_log_capture(&impl.reporter);

    int done = 0;
    int failed = 0;
    int skipped = 0;
    double audio_done = 0.0;
    double audio_total = 0.0;
    for (const auto &job : impl.jobs) {
        audio_total += job.duration;
    }

    std::string error;
    if (!impl.ensure_backends(&error)) {
        impl.reporter.error(error);
        remove_backend_log_capture();
        impl.reporter.emit(EvRunFinished{0, 0, 0, false, seconds_since(run_start)});
        return;
    }

    std::error_code ec;
    fs::create_directories(impl.cfg.work_dir, ec);
    fs::create_directories(impl.cfg.out_dir, ec);

    for (const auto &job : impl.jobs) {
        if (impl.cancel.stop_requested()) {
            break;
        }
        if (!impl.cancel.wait_if_paused()) {
            break;
        }

        const auto file_start = Clock::now();
        impl.db.set_file_status(job.file_id, "running");

        std::string file_error;
        bool ok = false;
        try {
            ok = impl.process(job, &file_error);
        } catch (const std::exception &e) {
            // One malformed file must not take the batch down with it.
            file_error = e.what();
            ok = false;
        }

        const double wall = seconds_since(file_start);

        if (ok) {
            ++done;
            audio_done += job.duration;
            impl.db.set_file_status(job.file_id, "done");
            auto outputs = impl.db.outputs(job.file_id);
            impl.reporter.emit(
                EvFileFinished{job.file_id, Stage::Done, {}, wall, job.duration, outputs});
        } else if (impl.cancel.stop_requested() || file_error == "cancelled") {
            ++skipped;
            impl.db.set_file_status(job.file_id, "pending");
            impl.reporter.emit(EvFileFinished{job.file_id, Stage::Skipped, {}, wall, 0.0, {}});
            break;
        } else {
            ++failed;
            impl.db.set_file_status(job.file_id, "failed", file_error);
            impl.reporter.error(fs::path(job.source_path).filename().string() + ": " +
                                    file_error,
                                job.file_id);
            impl.reporter.emit(
                EvFileFinished{job.file_id, Stage::Failed, file_error, wall, 0.0, {}});
        }

        const double elapsed = seconds_since(run_start);
        impl.reporter.emit(EvRunProgress{done + failed + skipped,
                                         static_cast<int>(impl.jobs.size()), audio_done,
                                         audio_total, elapsed > 0 ? audio_done / elapsed : 0.0});
    }

    remove_backend_log_capture();
    impl.reporter.emit(EvRunFinished{done, failed, skipped, impl.cancel.stop_requested(),
                                     seconds_since(run_start)});
}

Pipeline::ReclusterResult Pipeline::recluster() {
    auto &impl = *impl_;
    ReclusterResult result;

    auto locals = impl.db.all_local_speakers();
    locals.erase(std::remove_if(locals.begin(), locals.end(),
                                [](const LocalSpeaker &s) { return s.centroid.empty(); }),
                 locals.end());
    result.locals = static_cast<int>(locals.size());
    if (locals.empty()) {
        return result;
    }

    auto before = impl.db.globals();
    result.globals_before = static_cast<int>(before.size());

    std::vector<std::vector<float>> vectors;
    std::vector<std::int64_t> file_ids;
    vectors.reserve(locals.size());
    file_ids.reserve(locals.size());
    for (const auto &s : locals) {
        vectors.push_back(s.centroid);
        file_ids.push_back(s.file_id);
    }

    ClusterOptions opts;
    opts.threshold = impl.cfg.cluster_threshold;
    auto assignment = constrained_agglomerative(vectors, file_ids, opts);
    result.globals_after = assignment.n_clusters;

    std::map<int, std::vector<size_t>> clusters;
    for (size_t i = 0; i < locals.size(); ++i) {
        clusters[assignment.cluster_of[i]].push_back(i);
    }

    // Each cluster reclaims an identity its members already carried, preferring
    // one that has been named. Minting fresh ids on every recluster would throw
    // away every name the user has entered.
    std::map<int, std::int64_t> chosen;
    std::map<std::int64_t, int> claimed_by;

    for (const auto &[cluster, members] : clusters) {
        std::map<std::int64_t, int> votes;
        std::int64_t named = -1;
        double named_weight = 0.0;

        for (size_t idx : members) {
            std::int64_t gid = locals[idx].global_id;
            if (gid < 0) {
                continue;
            }
            votes[gid]++;
            auto g = impl.db.global(gid);
            if (g && !g->name.empty() && locals[idx].total_duration > named_weight) {
                named = gid;
                named_weight = locals[idx].total_duration;
            }
        }

        std::int64_t pick = named;
        if (pick < 0 && !votes.empty()) {
            pick = std::max_element(votes.begin(), votes.end(),
                                    [](const auto &a, const auto &b) {
                                        return a.second < b.second;
                                    })
                       ->first;
        }

        // Two clusters wanting the same identity means the previous grouping
        // merged people who are now separated. The larger cluster keeps it.
        if (pick >= 0) {
            auto it = claimed_by.find(pick);
            if (it != claimed_by.end()) {
                if (clusters[it->second].size() >= members.size()) {
                    pick = -1;
                } else {
                    chosen[it->second] = -1;
                    claimed_by[pick] = cluster;
                }
            } else {
                claimed_by[pick] = cluster;
            }
        }
        chosen[cluster] = pick;
    }

    impl.db.begin();
    for (const auto &[cluster, members] : clusters) {
        std::vector<std::vector<float>> parts;
        std::vector<double> weights;
        double duration = 0.0;
        for (size_t idx : members) {
            parts.push_back(locals[idx].centroid);
            weights.push_back(std::max(1.0, locals[idx].total_duration));
            duration += locals[idx].total_duration;
        }
        auto centroid = centroid_of(parts, weights);

        std::int64_t gid = chosen[cluster];
        if (gid < 0) {
            gid = impl.db.create_global(centroid, duration);
        }
        impl.db.update_global_centroid(gid, centroid, static_cast<int>(members.size()),
                                       duration);

        for (size_t idx : members) {
            float similarity = cosine(locals[idx].centroid, centroid);
            if (locals[idx].global_id != gid) {
                ++result.merged;
            }
            impl.db.assign_local(locals[idx].id, gid, similarity);
        }
    }
    impl.db.delete_empty_globals();
    impl.db.commit();

    impl.reporter.info("reclustered " + std::to_string(result.locals) + " voiceprints into " +
                       std::to_string(result.globals_after) + " speakers");
    return result;
}

int Pipeline::rerender(bool all) {
    auto &impl = *impl_;
    int count = 0;

    auto files = impl.db.files(all ? std::string{} : std::string("done"));
    for (const auto &rec : files) {
        if (impl.cancel.stop_requested()) {
            break;
        }
        auto segments = impl.db.segments(rec.id);
        if (segments.empty()) {
            continue;
        }

        MediaJob job;
        job.file_id = rec.id;
        job.source_path = rec.path;
        job.track = rec.track;
        job.track_count = rec.track_count;
        job.duration = rec.duration;
        job.channels = rec.channels;

        std::vector<std::string> written;
        impl.write_outputs(job, segments, rec.language, &written);
        if (!written.empty()) {
            ++count;
        }
    }

    impl.reporter.info("re-rendered " + std::to_string(count) + " transcripts");
    return count;
}

}  // namespace scribe
