#pragma once

#include <QStyledItemDelegate>

namespace scribble::gui {

/// Draws a transcript line: a dim monospaced timestamp, an optional speaker chip
/// in the speaker's colour, and the wrapped utterance text. The chip is absent
/// until diarization labels the line, which is the visible signal that speakers
/// have not resolved yet.
class SegmentDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /// The view feeds its viewport width here on resize, because a QListView does
    /// not hand a wrapping delegate a reliable width in sizeHint.
    void setViewportWidth(int width);

private:
    QRect textRect(const QStyleOptionViewItem &option) const;
    int viewportWidth_ = 0;
};

}  // namespace scribble::gui
