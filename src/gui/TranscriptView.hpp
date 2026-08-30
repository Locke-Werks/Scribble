#pragma once

#include <QHash>
#include <QVector>
#include <QWidget>

#include "types.hpp"

class QLabel;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;

namespace scribe::gui {

class TranscriptModel;
class SegmentDelegate;

/// Centre pane. Shows one file's transcript, updating live during transcription
/// and re-rendering in place as diarization and identity resolution arrive. Auto
/// scrolls to the newest line only while the user is already at the bottom, and a
/// filter box narrows the visible lines.
class TranscriptView : public QWidget {
    Q_OBJECT
public:
    explicit TranscriptView(QWidget *parent = nullptr);

    /// Replaces the view with a file's segments. Used when a queue row is picked.
    void showFile(std::int64_t fileId, const QVector<scribe::Segment> &segments);
    void clearFile();
    std::int64_t currentFile() const { return fileId_; }

    void appendLiveSegment(std::int64_t fileId, const scribe::Segment &segment);
    void applyLabelled(std::int64_t fileId, const QVector<scribe::Segment> &segments);
    void applyResolutions(std::int64_t fileId, const QHash<QString, std::int64_t> &labelToGlobal);

    /// Updates the id-to-display map and recolours every line. Called after a
    /// rename, a merge or a resolution so names and colours stay current without
    /// reloading the transcript.
    void setSpeakerNames(const QHash<std::int64_t, QString> &names);

private:
    void applyFilter(const QString &text);
    bool atBottom() const;
    void scrollToBottomIfFollowing(bool wasAtBottom);

    std::int64_t fileId_ = -1;
    TranscriptModel *model_ = nullptr;
    QSortFilterProxyModel *proxy_ = nullptr;
    SegmentDelegate *delegate_ = nullptr;
    QListView *list_ = nullptr;
    QLineEdit *search_ = nullptr;
    QLabel *heading_ = nullptr;
};

}  // namespace scribe::gui
