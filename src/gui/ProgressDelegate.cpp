#include "ProgressDelegate.hpp"

#include <algorithm>

#include <QApplication>
#include <QPainter>
#include <QStyleOptionProgressBar>

#include "QueueModel.hpp"

namespace scribe::gui {

void ProgressDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const {
    // Keep row selection highlighting consistent with the rest of the table.
    QStyleOptionViewItem background = option;
    initStyleOption(&background, index);
    background.text.clear();
    QStyle *style = option.widget ? option.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &background, painter, option.widget);

    const int percent = index.data(QueueModel::ProgressRole).toInt();
    const bool failed = index.data(QueueModel::FailedRole).toBool();

    // Laid out here rather than left to the style. Asked to draw its own label,
    // the style splits the rect and puts a thin groove above the text, which
    // wastes the row height and reads as a squashed bar. A fixed-height groove
    // centred in the cell, with the label painted over it, is predictable and
    // does not change shape with the row height.
    const int height = std::min(18, std::max(6, option.rect.height() - 8));
    QRect grooveRect(option.rect.left() + 4,
                     option.rect.center().y() - height / 2,
                     std::max(0, option.rect.width() - 8),
                     height);

    QStyleOptionProgressBar bar;
    bar.rect = grooveRect;
    bar.palette = option.palette;
    bar.fontMetrics = option.fontMetrics;
    bar.minimum = 0;
    bar.maximum = 100;
    bar.progress = failed ? 0 : percent;
    bar.textVisible = false;

    // State_Horizontal is the load-bearing flag. A view item's state never
    // carries it, and without it the style draws the bar vertically: a narrow
    // sliver down the middle of the cell with the text pushed outside it.
    // The item's own flags are not forwarded, because selection and hover mean
    // nothing to a progress bar and only tint it inconsistently.
    bar.state = QStyle::State_Enabled | QStyle::State_Horizontal;
    if (option.state & QStyle::State_Active) {
        bar.state |= QStyle::State_Active;
    }

    if (failed) {
        bar.palette.setColor(QPalette::Highlight, QColor(0xe7, 0x82, 0x84));
    }
    style->drawControl(QStyle::CE_ProgressBar, &bar, painter, nullptr);

    const QString label =
        failed ? QStringLiteral("failed") : QStringLiteral("%1%").arg(percent);
    painter->save();
    painter->setPen(option.palette.color(option.state & QStyle::State_Selected
                                             ? QPalette::HighlightedText
                                             : QPalette::Text));
    painter->drawText(grooveRect, Qt::AlignCenter, label);
    painter->restore();
}

}  // namespace scribe::gui
