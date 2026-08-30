#include "SegmentDelegate.hpp"

#include <QApplication>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include "FormatUtil.hpp"
#include "TranscriptModel.hpp"

namespace scribe::gui {

namespace {
constexpr int kGutter = 96;   // timestamp column
constexpr int kHPad = 8;
constexpr int kVPad = 6;
constexpr int kChipGap = 4;

QFont monoFont(const QFont &base) {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSizeF(base.pointSizeF() > 0 ? base.pointSizeF() - 1.0 : f.pointSizeF());
    return f;
}

QColor readableOn(const QColor &bg) {
    // Pastel chips are light, so dark text keeps the label legible on them.
    const double luma = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue();
    return luma > 140 ? QColor(0x1c, 0x1f, 0x2b) : QColor(0xf5, 0xf5, 0xf5);
}
}  // namespace

QRect SegmentDelegate::textRect(const QStyleOptionViewItem &option) const {
    const QRect r = option.rect;
    const int x = r.left() + kGutter + kHPad;
    return QRect(x, r.top() + kVPad, r.right() - kHPad - x, r.height() - 2 * kVPad);
}

void SegmentDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();
    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const bool selected = option.state & QStyle::State_Selected;
    const double start = index.data(TranscriptModel::StartRole).toDouble();
    const QString speaker = index.data(TranscriptModel::SpeakerRole).toString();
    const QColor color = index.data(TranscriptModel::ColorRole).value<QColor>();
    const QString text = index.data(TranscriptModel::TextRole).toString();

    painter->save();

    const QColor normalText = selected ? option.palette.color(QPalette::HighlightedText)
                                       : option.palette.color(QPalette::Text);
    const QColor dimText = selected ? option.palette.color(QPalette::HighlightedText)
                                    : option.palette.color(QPalette::Disabled, QPalette::Text);

    // Timestamp gutter.
    QFont mono = monoFont(option.font);
    painter->setFont(mono);
    painter->setPen(dimText);
    const QRect tsRect(option.rect.left() + kHPad, option.rect.top() + kVPad,
                       kGutter - kHPad, QFontMetrics(mono).height());
    painter->drawText(tsRect, Qt::AlignLeft | Qt::AlignTop, formatTimestamp(start));

    int y = option.rect.top() + kVPad;
    const int contentX = option.rect.left() + kGutter + kHPad;
    const int contentW = option.rect.right() - kHPad - contentX;

    if (!speaker.isEmpty()) {
        QFont chipFont = option.font;
        chipFont.setBold(true);
        chipFont.setPointSizeF(chipFont.pointSizeF() > 0 ? chipFont.pointSizeF() - 1.0
                                                         : chipFont.pointSizeF());
        painter->setFont(chipFont);
        const QFontMetrics cfm(chipFont);
        const int chipH = cfm.height() + 2;
        const int chipW = qMin(cfm.horizontalAdvance(speaker) + 12, contentW);
        const QRect chip(contentX, y, chipW, chipH);
        QPainterPath path;
        path.addRoundedRect(chip, 4, 4);
        painter->fillPath(path, color);
        painter->setPen(readableOn(color));
        painter->drawText(chip, Qt::AlignCenter,
                          cfm.elidedText(speaker, Qt::ElideRight, chipW - 8));
        y += chipH + kChipGap;
    }

    QFont textFont = option.font;
    painter->setFont(textFont);
    painter->setPen(normalText);
    const QRect textR(contentX, y, contentW, option.rect.bottom() - kVPad - y);
    painter->drawText(textR, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text);

    painter->restore();
}

QSize SegmentDelegate::sizeHint(const QStyleOptionViewItem &option,
                                const QModelIndex &index) const {
    int width = viewportWidth_ > 0 ? viewportWidth_ : option.rect.width();
    if (width <= 0) {
        width = 480;  // pre-layout fallback; the view relays out on resize
    }
    const int contentW = qMax(80, width - kGutter - 2 * kHPad);

    const QString speaker = index.data(TranscriptModel::SpeakerRole).toString();
    const QString text = index.data(TranscriptModel::TextRole).toString();

    int height = 2 * kVPad;
    if (!speaker.isEmpty()) {
        QFont chipFont = option.font;
        chipFont.setBold(true);
        height += QFontMetrics(chipFont).height() + 2 + kChipGap;
    }
    const QFontMetrics fm(option.font);
    const QRect br = fm.boundingRect(QRect(0, 0, contentW, 100000),
                                     Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text);
    height += qMax(br.height(), fm.height());
    return QSize(width, qMax(height, fm.height() + 2 * kVPad));
}

void SegmentDelegate::setViewportWidth(int width) {
    viewportWidth_ = width;
}

}  // namespace scribe::gui
