#pragma once

#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "events.hpp"
#include "types.hpp"

namespace scribble {

class Database;

/// Runs the batch. Construct on any thread, call `run` on a worker thread.
///
/// Ordering inside a file is transcribe, then diarize, then resolve. Whisper
/// streams segments as it goes so text appears immediately; diarization needs
/// the whole file, so speaker labels arrive afterwards and the view updates in
/// place. Doing it the other way round leaves the user staring at nothing.
class Pipeline {
public:
    Pipeline(Config config, Database &db, EventSink &sink);
    ~Pipeline();

    Pipeline(const Pipeline &) = delete;
    Pipeline &operator=(const Pipeline &) = delete;

    /// Walks paths, expands multi-track files into per-track jobs and records
    /// them. Files already marked done are skipped unless `overwrite` is set.
    /// Returns the jobs that will actually be processed.
    std::vector<MediaJob> enqueue(const std::vector<fs::path> &paths);

    /// Processes everything enqueued. Blocking. Safe to call once per instance.
    void run();

    CancelToken &cancel() noexcept;

    /// Re-runs global clustering over every stored voiceprint and rewrites the
    /// identity assignments, preserving names. Incremental matching is
    /// order-dependent, so this is what makes IDs stable as the corpus grows.
    struct ReclusterResult {
        int locals = 0;
        int globals_before = 0;
        int globals_after = 0;
        int merged = 0;
        int split = 0;
    };
    ReclusterResult recluster();

    /// Rewrites transcripts from the database without re-running inference.
    /// Used after a rename or a merge.
    int rerender(bool all);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace scribble
