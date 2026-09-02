#include "SegmentDelegate.hpp"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>

#include "FormatUtil.hpp"
#include "ThemeQt.hpp"
#include "TranscriptModel.hpp"

namespace scribble::gui {

namespace {
constexpr int kGutter = 96;   // timestamp column
constexpr int kHPad = 10;
constexpr int kVPad = 7;
constexpr int kChipGap = 5;
constexpr int kChipHPad = 7;

// paint() and sizeHint() have to agree to the pixel, so both take their fonts
// from here rather than from the view's font.
QFont stampFont() {
    return theme::mono(11);
}

QFont chipFont() {
    return theme::mono(11);
}

QFont utteranceFont() {
    return theme::body(13);
}
}  // namespace

QRect SegmentDelegate::textRect(const QStyleOptionViewItem &option) const {
    const QRect r = option.rect;
    const int x = r.left() + kGutter + kHPad;
    return QRect(x, r.top() + kVPad, r.right() - kHPad - x, r.height() - 2 * kVPad);
}

void SegmentDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const {
    using namespace scribble::theme;

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
    painter->setRenderHint(QPainter::Antialiasing, true);

    // The selected line's 2px red edge. This is how "red is the only accent"
    // survives a list: the row fill stays a neutral elevated surface and the
    // accent is a single hairline.
    if (selected) {
        painter->fillRect(QRect(option.rect.left(), option.rect.top(), 2, option.rect.height()),
                          theme::c(kRed));
    }

    const QFont stamp = stampFont();
    painter->setFont(stamp);
    painter->setPen(theme::c(kFg4));
    const QRect tsRect(option.rect.left() + kHPad, option.rect.top() + kVPad, kGutter - kHPad,
                       QFontMetrics(stamp).height());
    painter->drawText(tsRect, Qt::AlignLeft | Qt::AlignTop, formatTimestamp(start));

    int y = option.rect.top() + kVPad;
    const int contentX = option.rect.left() + kGutter + kHPad;
    const int contentW = option.rect.right() - kHPad - contentX;

    if (!speaker.isEmpty()) {
        // A chip is a hairline box on the page colour with the speaker's hue as
        // the text, not a filled pastel block. Filled chips are the one thing
        // that reliably breaks black on black: at this density the page turns
        // into a column of coloured bars.
        const QFont chip = chipFont();
        painter->setFont(chip);
        const QFontMetrics cfm(chip);
        const int chipH = cfm.height() + 4;
        const int chipW = qMin(cfm.horizontalAdvance(speaker) + 2 * kChipHPad, contentW);
        const QRect chipRect(contentX, y, chipW, chipH);

        painter->setPen(QPen(theme::c(kBorder), 1));
        painter->setBrush(theme::c(kBlack));
        painter->drawRoundedRect(QRectF(chipRect).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);

        painter->setPen(color);
        painter->drawText(chipRect, Qt::AlignCenter,
                          cfm.elidedText(speaker, Qt::ElideRight, chipW - 2 * kChipHPad + 2));
        y += chipH + kChipGap;
    }

    painter->setFont(utteranceFont());
    painter->setPen(theme::c(selected ? kFg1 : kFg2));
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
        height += QFontMetrics(chipFont()).height() + 4 + kChipGap;
    }
    const QFontMetrics fm(utteranceFont());
    const QRect br = fm.boundingRect(QRect(0, 0, contentW, 100000),
                                     Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text);
    height += qMax(br.height(), fm.height());
    return QSize(width, qMax(height, fm.height() + 2 * kVPad));
}

void SegmentDelegate::setViewportWidth(int width) {
    viewportWidth_ = width;
}

}  // namespace scribble::gui
