#include "ProgressDelegate.hpp"

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

    QStyleOptionProgressBar bar;
    bar.rect = option.rect.adjusted(3, 3, -3, -3);
    bar.minimum = 0;
    bar.maximum = 100;
    bar.progress = failed ? 0 : percent;
    bar.textVisible = true;
    bar.text = failed ? QStringLiteral("failed") : QStringLiteral("%1%").arg(percent);
    bar.textAlignment = Qt::AlignCenter;
    bar.state = option.state;
    if (failed) {
        bar.palette.setColor(QPalette::Highlight, QColor(0xe7, 0x82, 0x84));
    }
    style->drawControl(QStyle::CE_ProgressBar, &bar, painter, nullptr);
}

}  // namespace scribe::gui
