#include "types.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#include "events.hpp"

namespace scribble {

bool CancelToken::wait_if_paused() const {
    while (paused_.load(std::memory_order_relaxed)) {
        if (stop_.load(std::memory_order_relaxed)) {
            return false;
        }
        // Polled rather than condition-variable driven because the callers are
        // C callbacks from whisper.cpp that cannot hold a lock across a wait.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return !stop_.load(std::memory_order_relaxed);
}

const char *stage_name(Stage s) {
    switch (s) {
        case Stage::Queued:       return "queued";
        case Stage::Probing:      return "probing";
        case Stage::Extracting:   return "extracting";
        case Stage::Isolating:    return "isolating";
        case Stage::Transcribing: return "transcribing";
        case Stage::Diarizing:    return "diarizing";
        case Stage::Embedding:    return "embedding";
        case Stage::Resolving:    return "resolving";
        case Stage::Writing:      return "writing";
        case Stage::Done:         return "done";
        case Stage::Failed:       return "failed";
        case Stage::Skipped:      return "skipped";
    }
    return "unknown";
}

const char *log_level_name(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info:  return "info";
        case LogLevel::Warn:  return "warn";
        case LogLevel::Error: return "error";
    }
    return "info";
}

std::string GlobalSpeaker::display() const {
    if (!name.empty()) {
        return name;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "SPEAKER_%04lld", static_cast<long long>(id));
    return buf;
}

}  // namespace scribble
