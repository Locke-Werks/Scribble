#include "ProgressDelegate.hpp"

#include <algorithm>

#include <QApplication>
#include <QPainter>

#include "QueueModel.hpp"
#include "ThemeQt.hpp"

namespace scribble::gui {

void ProgressDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const {
    using namespace scribble::theme;

    // Keep row selection highlighting consistent with the rest of the table.
    // The style is asked for the background only, so it stays whatever the
    // stylesheet says a selected or hovered row is.
    QStyleOptionViewItem background = option;
    initStyleOption(&background, index);
    background.text.clear();
    QStyle *style = option.widget ? option.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &background, painter, option.widget);

    const int percent = std::clamp(index.data(QueueModel::ProgressRole).toInt(), 0, 100);
    const bool failed = index.data(QueueModel::FailedRole).toBool();

    // Painted here rather than handed to QStyle::CE_ProgressBar. The style draws
    // a groove out of the palette with its own metrics, and neither the
    // hairline border nor the 2px corner survives that; a bar this small is
    // three rectangles, so it is drawn directly.
    const int height = std::min(14, std::max(6, option.rect.height() - 10));
    const QRect groove(option.rect.left() + 6, option.rect.center().y() - height / 2,
                       std::max(0, option.rect.width() - 12), height);
    if (groove.width() <= 2) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    painter->setPen(QPen(theme::c(kBorder), 1));
    painter->setBrush(theme::c(kBlack));
    painter->drawRoundedRect(QRectF(groove).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);

    // A failure leaves the groove empty. The red fill means progress, and using
    // it for a dead row would say the file finished.
    if (!failed && percent > 0) {
        const int span = groove.width() - 2;
        const int filled = span * percent / 100;
        if (filled > 0) {
            painter->fillRect(QRect(groove.left() + 1, groove.top() + 1, filled,
                                    groove.height() - 2),
                              theme::c(kRed));
        }
    }

    painter->setFont(theme::mono(10));
    painter->setPen(theme::c(failed ? kRedDark : (percent > 0 ? kFg1 : kFg3)));
    painter->drawText(groove, Qt::AlignCenter,
                      failed ? QStringLiteral("failed") : QStringLiteral("%1%").arg(percent));
    painter->restore();
}

}  // namespace scribble::gui
