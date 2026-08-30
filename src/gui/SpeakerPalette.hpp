#pragma once

#include <QColor>
#include <QString>

namespace scribble::gui {

/// Stable colours for speakers. The same global identity always draws the same
/// colour, in the transcript and in the speaker panel swatch, which is what lets
/// the eye track a voice across files. Unresolved file-local speakers borrow a
/// provisional colour keyed on their diarization label until identity resolves.
class SpeakerPalette {
public:
    static QColor colorForGlobal(qint64 globalId);
    static QColor colorForKey(const QString &key);
    static QColor unknown();
};

}  // namespace scribble::gui
