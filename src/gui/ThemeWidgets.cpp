#include "ThemeWidgets.hpp"

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QLinearGradient>
#include <QPainter>

namespace scribble::gui {

using scribble::theme::kRed;

// ---------------------------------------------------------------- TrackedLabel

TrackedLabel::TrackedLabel(const QString &text, int px, int weight, qreal em, QWidget *parent)
    : QLabel(parent) {
    setFont(theme::tracked(px, weight, em));
    TrackedLabel::setText(text);
}

void TrackedLabel::setText(const QString &text) {
    QLabel::setText(text.toUpper());
}

TrackedLabel *eyebrow(const QString &text, int px, QWidget *parent) {
    auto *l = new TrackedLabel(text, px, QFont::Bold, 0.18, parent);
    l->setStyleSheet(QStringLiteral("color: %1;").arg(theme::c(kRed).name()));
    return l;
}

QLabel *caption(const QString &text, scribble::theme::Rgb colour, int px, QWidget *parent) {
    auto *l = new QLabel(text, parent);
    l->setFont(theme::body(px));
    l->setStyleSheet(QStringLiteral("color: %1;").arg(theme::c(colour).name()));
    return l;
}

QLabel *monoCaption(const QString &text, scribble::theme::Rgb colour, int px, QWidget *parent) {
    auto *l = new QLabel(text, parent);
    l->setFont(theme::mono(px));
    l->setStyleSheet(QStringLiteral("color: %1;").arg(theme::c(colour).name()));
    return l;
}

// --------------------------------------------------------------------- Buttons

void styleButton(QAbstractButton *button, bool primary) {
    if (!button) {
        return;
    }
    button->setFont(theme::tracked(11, QFont::Bold, 0.14));
    button->setText(button->text().toUpper());
    if (primary) {
        button->setObjectName(QStringLiteral("Primary"));
    }
}

void styleButtonBox(QDialogButtonBox *box) {
    if (!box) {
        return;
    }
    for (QAbstractButton *b : box->buttons()) {
        const QDialogButtonBox::ButtonRole role = box->buttonRole(b);
        styleButton(b, role == QDialogButtonBox::AcceptRole);
    }
}

// --------------------------------------------------------------------- TopRule

TopRule::TopRule(QWidget *parent) : QWidget(parent) {
    setFixedHeight(1);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void TopRule::paintEvent(QPaintEvent *) {
    QLinearGradient g(0, 0, width(), 0);
    const QColor red = theme::c(kRed);
    g.setColorAt(0.0, QColor(red.red(), red.green(), red.blue(), 0));
    g.setColorAt(0.5, red);
    g.setColorAt(1.0, QColor(red.red(), red.green(), red.blue(), 0));

    QPainter p(this);
    p.fillRect(rect(), g);
}

// ---------------------------------------------------------------- GrainOverlay

GrainOverlay::GrainOverlay(QWidget *parent) : QWidget(parent) {
    // Clicks pass straight through to the widgets beneath.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
}

void GrainOverlay::cover(QWidget *host) {
    if (!host) {
        return;
    }
    setGeometry(host->rect());
    raise();
}

void GrainOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.drawTiledPixmap(rect(), theme::grainTile());
}

}  // namespace scribble::gui
