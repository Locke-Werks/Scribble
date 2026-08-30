#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "types.hpp"

namespace scribe {

/// A file entered the queue. Emitted during discovery, before any work.
struct EvFileDiscovered {
    MediaJob job;
};

/// Work began on a file. Duration and channel count are known by now.
struct EvFileStarted {
    MediaJob job;
};

/// Stage transition or intra-stage progress. `fraction` is -1 when the stage
/// cannot report a meaningful proportion.
struct EvStage {
    std::int64_t file_id = -1;
    Stage stage = Stage::Queued;
    double fraction = -1.0;
    std::string detail;
};

/// A transcript line, emitted live from the Whisper segment callback. Speaker
/// fields are still empty at this point.
struct EvSegment {
    std::int64_t file_id = -1;
    Segment segment;
};

/// Diarization finished and every segment now carries a speaker. Carries the
/// full labelled set so a view can replace what it drew during transcription.
struct EvSegmentsLabelled {
    std::int64_t file_id = -1;
    std::vector<Segment> segments;
};

/// File-local speakers were matched into the corpus-wide store.
struct EvSpeakersResolved {
    std::int64_t file_id = -1;
    std::vector<SpeakerResolution> resolutions;
};

struct EvFileFinished {
    std::int64_t file_id = -1;
    Stage final_stage = Stage::Done;  ///< Done, Failed or Skipped
    std::string error;
    double wall_seconds = 0.0;
    double audio_seconds = 0.0;
    std::vector<std::string> outputs;  ///< paths written
};

/// Whole-run counters, emitted after each file finishes.
struct EvRunProgress {
    int files_done = 0;
    int files_total = 0;
    double audio_done = 0.0;
    double audio_total = 0.0;
    double realtime_factor = 0.0;  ///< audio seconds per wall second
};

struct EvLog {
    LogLevel level = LogLevel::Info;
    std::string text;
    std::int64_t file_id = -1;  ///< -1 when not tied to a file
};

/// A model file is being fetched. First run only.
struct EvModelDownload {
    std::string name;
    std::int64_t bytes_done = 0;
    std::int64_t bytes_total = 0;
    bool finished = false;
};

struct EvRunFinished {
    int files_done = 0;
    int files_failed = 0;
    int files_skipped = 0;
    bool cancelled = false;
    double wall_seconds = 0.0;
};

using Event = std::variant<
    EvFileDiscovered,
    EvFileStarted,
    EvStage,
    EvSegment,
    EvSegmentsLabelled,
    EvSpeakersResolved,
    EvFileFinished,
    EvRunProgress,
    EvLog,
    EvModelDownload,
    EvRunFinished>;

/// Implemented by the CLI reporter and by the GUI bridge.
///
/// `handle` is called from the pipeline worker thread, never from the caller's
/// thread. Implementations must not block: the Qt bridge copies the event and
/// posts it across a queued connection.
class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void handle(const Event &event) = 0;
};

/// Cooperative stop and pause, polled between stages and between chunks.
class CancelToken {
public:
    void request_stop() noexcept { stop_.store(true, std::memory_order_relaxed); }
    void set_paused(bool paused) noexcept { paused_.store(paused, std::memory_order_relaxed); }
    void reset() noexcept {
        stop_.store(false, std::memory_order_relaxed);
        paused_.store(false, std::memory_order_relaxed);
    }

    bool stop_requested() const noexcept { return stop_.load(std::memory_order_relaxed); }
    bool paused() const noexcept { return paused_.load(std::memory_order_relaxed); }

    /// Blocks while paused. Returns false when a stop was requested.
    bool wait_if_paused() const;

private:
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_{false};
};

}  // namespace scribe
