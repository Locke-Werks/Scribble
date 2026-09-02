#pragma once

#include <QColor>
#include <QFont>
#include <QPixmap>
#include <QString>

#include "theme.hpp"

class QApplication;

namespace scribble::gui {

// The ARCHON / Specter Point design language, translated into Qt.
//
// The token values live in core/theme.hpp so the desktop application and
// anything else that grows a front end cannot drift apart. Only the
// translation is here.
namespace theme {

inline QColor c(scribble::theme::Rgb v) { return QColor(v.r, v.g, v.b); }

// Fonts, base style, palette and stylesheet, in the order Qt needs them.
// Must run before any widget exists.
void apply(QApplication &app);

// Loads the bundled faces. Called by apply(); separate because the fonts have
// to be registered before the application font is set.
void loadFonts();

// Tracked all-caps, the design language's headline treatment.
//
// This is a real font rather than a stylesheet rule because QSS has neither
// letter-spacing nor text-transform. Every tracked label, delegate-drawn cell
// and button goes through here.
QFont tracked(int px, int weight, qreal emSpacing);
QFont body(int px, int weight = QFont::Normal);
QFont mono(int px);

// Everything QSS can express, and nothing it cannot.
QString styleSheet();

// A fixed-seed noise tile at 2.5% alpha.
//
// The seed is fixed on purpose: a re-seeded tile makes the grain crawl on
// every resize, which reads as a rendering fault rather than as film.
const QPixmap &grainTile();

}  // namespace theme
}  // namespace scribble::gui
