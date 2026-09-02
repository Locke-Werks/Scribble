#pragma once

#include <QLabel>
#include <QWidget>

class QAbstractButton;
class QDialogButtonBox;

#include "ThemeQt.hpp"

namespace scribble::gui {

// A label in tracked all-caps. QSS has neither letter-spacing nor
// text-transform, so both happen here rather than in the stylesheet.
class TrackedLabel : public QLabel {
    Q_OBJECT
public:
    TrackedLabel(const QString &text, int px, int weight, qreal em,
                 QWidget *parent = nullptr);
    void setText(const QString &text);
};

// The section eyebrow: a tracked label in red, prefixed with the double slash
// the design language marks a section with.
TrackedLabel *eyebrow(const QString &text, int px = 15, QWidget *parent = nullptr);

// A caption line: body or mono text in one of the dimmer foreground roles.
QLabel *caption(const QString &text, scribble::theme::Rgb colour, int px = 12,
                QWidget *parent = nullptr);
QLabel *monoCaption(const QString &text, scribble::theme::Rgb colour, int px = 11,
                    QWidget *parent = nullptr);

// Tracked all-caps on a button, and the red fill if it is the one affirmative
// action on the surface. QSS cannot letter-space or upper-case, so both happen
// here; #Primary is the stylesheet's half of it.
void styleButton(QAbstractButton *button, bool primary = false);

// The same, applied to a button box, with the accepting role taken as primary.
// Dialogs get their buttons from Qt rather than constructing them, so this is
// the only hook they have.
void styleButtonBox(QDialogButtonBox *box);

// The single 1px red scanline the design language puts at the top of every
// screen, fading out at both ends.
class TopRule : public QWidget {
    Q_OBJECT
public:
    explicit TopRule(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
};

// The 2.5% film grain, tiled over everything.
//
// Transparent to the mouse, so it is a purely visual layer and clicks reach the
// widgets underneath. This is the literal translation of the web version's
// position:absolute; inset:0; pointer-events:none.
class GrainOverlay : public QWidget {
    Q_OBJECT
public:
    explicit GrainOverlay(QWidget *parent = nullptr);

    // Sizes the overlay to its parent and keeps it on top. Call from the host's
    // resizeEvent: a stacked child does not follow its parent on its own.
    void cover(QWidget *host);

protected:
    void paintEvent(QPaintEvent *) override;
};

}  // namespace scribble::gui
