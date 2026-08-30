#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include "events.hpp"
#include "types.hpp"

namespace scribble {

/// Thin wrapper over an EventSink so the pipeline can emit without repeating
/// the variant construction at every call site.
class Reporter {
public:
    explicit Reporter(EventSink &sink) : sink_(sink) {}

    void emit(const Event &e) { sink_.handle(e); }

    void log(LogLevel level, std::string text, std::int64_t file_id = -1) {
        sink_.handle(EvLog{level, std::move(text), file_id});
    }

    void debug(std::string text, std::int64_t file_id = -1) {
        log(LogLevel::Debug, std::move(text), file_id);
    }
    void info(std::string text, std::int64_t file_id = -1) {
        log(LogLevel::Info, std::move(text), file_id);
    }
    void warn(std::string text, std::int64_t file_id = -1) {
        log(LogLevel::Warn, std::move(text), file_id);
    }
    void error(std::string text, std::int64_t file_id = -1) {
        log(LogLevel::Error, std::move(text), file_id);
    }

    template <class... Args>
    void infof(std::format_string<Args...> fmt, Args &&...args) {
        info(std::format(fmt, std::forward<Args>(args)...));
    }
    template <class... Args>
    void warnf(std::format_string<Args...> fmt, Args &&...args) {
        warn(std::format(fmt, std::forward<Args>(args)...));
    }
    template <class... Args>
    void errorf(std::format_string<Args...> fmt, Args &&...args) {
        error(std::format(fmt, std::forward<Args>(args)...));
    }
    template <class... Args>
    void debugf(std::format_string<Args...> fmt, Args &&...args) {
        debug(std::format(fmt, std::forward<Args>(args)...));
    }

    void stage(std::int64_t file_id, Stage s, double fraction = -1.0,
               std::string detail = {}) {
        sink_.handle(EvStage{file_id, s, fraction, std::move(detail)});
    }

    EventSink &sink() noexcept { return sink_; }

private:
    EventSink &sink_;
};

/// Routes the noisy per-token chatter from whisper.cpp and onnxruntime into
/// the same event stream instead of letting it reach stdout, where it would
/// corrupt CLI output and never reach the GUI at all.
void install_backend_log_capture(Reporter *reporter);
void remove_backend_log_capture();

}  // namespace scribble
