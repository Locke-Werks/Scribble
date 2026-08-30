#pragma once

#include <QStyledItemDelegate>

namespace scribe::gui {

/// Paints the queue's progress column as an inline bar. A failed row draws an
/// empty bar tinted to match the row's failed state rather than a misleading
/// partial fill.
class ProgressDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};

}  // namespace scribe::gui
