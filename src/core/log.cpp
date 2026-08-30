#include "log.hpp"

#include <mutex>

#include "util.hpp"

extern "C" {
#include "ggml.h"
}
#include "whisper.h"

namespace scribe {
namespace {

std::mutex g_mutex;
Reporter *g_reporter = nullptr;

/// whisper.cpp and ggml both emit partial lines, so buffer until a newline
/// rather than producing one log event per fragment.
std::string g_pending;

LogLevel map_level(ggml_log_level level) {
    switch (level) {
        case GGML_LOG_LEVEL_DEBUG: return LogLevel::Debug;
        case GGML_LOG_LEVEL_INFO:  return LogLevel::Debug;
        case GGML_LOG_LEVEL_WARN:  return LogLevel::Warn;
        case GGML_LOG_LEVEL_ERROR: return LogLevel::Error;
        default:                   return LogLevel::Debug;
    }
}

void backend_log(ggml_log_level level, const char *text, void * /*user*/) {
    if (text == nullptr) {
        return;
    }
    std::lock_guard lock(g_mutex);
    if (g_reporter == nullptr) {
        return;
    }
    g_pending += text;
    size_t pos;
    while ((pos = g_pending.find('\n')) != std::string::npos) {
        std::string line = trim(g_pending.substr(0, pos));
        g_pending.erase(0, pos + 1);
        if (!line.empty()) {
            g_reporter->log(map_level(level), std::move(line));
        }
    }
}

}  // namespace

void install_backend_log_capture(Reporter *reporter) {
    {
        std::lock_guard lock(g_mutex);
        g_reporter = reporter;
        g_pending.clear();
    }
    whisper_log_set(backend_log, nullptr);
    ggml_log_set(backend_log, nullptr);
}

void remove_backend_log_capture() {
    whisper_log_set(nullptr, nullptr);
    ggml_log_set(nullptr, nullptr);
    std::lock_guard lock(g_mutex);
    g_reporter = nullptr;
    g_pending.clear();
}

}  // namespace scribe
