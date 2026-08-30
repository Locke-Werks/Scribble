#pragma once

#include <QString>
#include <cmath>

namespace scribble::gui {

/// Compact clock for durations shown in the queue: minutes:seconds, growing to
/// hours only when the file is long enough to need it.
inline QString formatDuration(double seconds) {
    if (!(seconds > 0.0) || std::isnan(seconds)) {
        return QStringLiteral("--:--");
    }
    const qint64 total = static_cast<qint64>(seconds + 0.5);
    const qint64 h = total / 3600;
    const qint64 m = (total % 3600) / 60;
    const qint64 s = total % 60;
    if (h > 0) {
        return QString::asprintf("%lld:%02lld:%02lld", h, m, s);
    }
    return QString::asprintf("%lld:%02lld", m, s);
}

/// Timestamp for a transcript line, always hours:minutes:seconds.milliseconds so
/// lines stay aligned regardless of file length.
inline QString formatTimestamp(double seconds) {
    if (!(seconds > 0.0) || std::isnan(seconds)) {
        seconds = 0.0;
    }
    const qint64 whole = static_cast<qint64>(seconds);
    qint64 ms = static_cast<qint64>((seconds - static_cast<double>(whole)) * 1000.0 + 0.5);
    qint64 s = whole % 60;
    qint64 m = (whole % 3600) / 60;
    qint64 h = whole / 3600;
    if (ms >= 1000) {  // rounding can spill over into the next second
        ms -= 1000;
        if (++s >= 60) { s -= 60; if (++m >= 60) { m -= 60; ++h; } }
    }
    return QString::asprintf("%02lld:%02lld:%02lld.%03lld", h, m, s, ms);
}

}  // namespace scribble::gui
