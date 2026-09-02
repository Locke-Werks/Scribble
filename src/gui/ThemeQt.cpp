#include "ThemeQt.hpp"

#include <QApplication>
#include <QFontDatabase>
#include <QImage>
#include <QPalette>
#include <QRandomGenerator>
#include <QStyleFactory>

namespace scribble::gui::theme {

namespace {

using scribble::theme::kBlack;
using scribble::theme::kBorder;
using scribble::theme::kBorderHi;
using scribble::theme::kElevated;
using scribble::theme::kFg1;
using scribble::theme::kFg2;
using scribble::theme::kFg3;
using scribble::theme::kFg4;
using scribble::theme::kRed;
using scribble::theme::kRedDark;
using scribble::theme::kSurface;

QString hex(scribble::theme::Rgb v) {
    return QString::asprintf("#%02X%02X%02X", v.r, v.g, v.b);
}

QString g_headingFamily = QStringLiteral("Chakra Petch");
QString g_bodyFamily = QStringLiteral("Outfit");

// Fusion rather than the native Windows style. The parts a stylesheet cannot
// reach, combo box arrows, tree branch indicators, spin box marks, are drawn by
// the base style out of the palette, and the native style paints several of
// them with system colours that ignore both.
void applyStyleAndPalette(QApplication &app) {
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette p;
    p.setColor(QPalette::Window, c(kBlack));
    p.setColor(QPalette::WindowText, c(kFg2));
    p.setColor(QPalette::Base, c(kSurface));
    p.setColor(QPalette::AlternateBase, c(kSurface));
    p.setColor(QPalette::Text, c(kFg2));
    p.setColor(QPalette::PlaceholderText, c(kFg4));
    p.setColor(QPalette::Button, c(kSurface));
    p.setColor(QPalette::ButtonText, c(kFg2));
    p.setColor(QPalette::BrightText, c(kFg1));
    p.setColor(QPalette::ToolTipBase, c(kElevated));
    p.setColor(QPalette::ToolTipText, c(kFg2));
    p.setColor(QPalette::Light, c(kBorderHi));
    p.setColor(QPalette::Midlight, c(kBorder));
    p.setColor(QPalette::Mid, c(kBorder));
    p.setColor(QPalette::Dark, c(kBlack));
    p.setColor(QPalette::Shadow, c(kBlack));
    p.setColor(QPalette::Link, c(kRed));
    p.setColor(QPalette::LinkVisited, c(kRedDark));

    // Selection is the elevated surface, not a system highlight. A blue bar
    // would be a second accent.
    p.setColor(QPalette::Highlight, c(kElevated));
    p.setColor(QPalette::HighlightedText, c(kFg1));

    p.setColor(QPalette::Disabled, QPalette::Text, c(kFg4));
    p.setColor(QPalette::Disabled, QPalette::WindowText, c(kFg4));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c(kFg4));
    p.setColor(QPalette::Disabled, QPalette::Highlight, c(kSurface));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, c(kFg4));

    app.setPalette(p);
}

}  // namespace

void loadFonts() {
    const char *faces[] = {
        ":/fonts/ChakraPetch-Regular.ttf",
        ":/fonts/ChakraPetch-SemiBold.ttf",
        ":/fonts/ChakraPetch-Bold.ttf",
        ":/fonts/Outfit-Variable.ttf",
    };

    QStringList heading, bodyFaces;
    for (const char *f : faces) {
        const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(f));
        if (id < 0) {
            continue;
        }
        for (const QString &fam : QFontDatabase::applicationFontFamilies(id)) {
            if (fam.startsWith(QStringLiteral("Chakra"))) {
                heading << fam;
            } else {
                bodyFaces << fam;
            }
        }
    }

    // Fall back rather than fail. A missing face is a build packaging problem,
    // not a reason to refuse to start.
    g_headingFamily = heading.isEmpty() ? QStringLiteral("Segoe UI Semibold") : heading.front();
    g_bodyFamily = bodyFaces.isEmpty() ? QStringLiteral("Segoe UI") : bodyFaces.front();

    QFont appFont(g_bodyFamily);
    appFont.setPixelSize(13);
    QApplication::setFont(appFont);
}

void apply(QApplication &app) {
    loadFonts();
    applyStyleAndPalette(app);
    app.setStyleSheet(styleSheet());
}

QFont tracked(int px, int weight, qreal emSpacing) {
    QFont f(g_headingFamily);
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    // AbsoluteSpacing takes pixels, so an em figure from the design spec has to
    // be multiplied by the size it applies at.
    f.setLetterSpacing(QFont::AbsoluteSpacing, px * emSpacing);
    return f;
}

QFont body(int px, int weight) {
    QFont f(g_bodyFamily);
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

QFont mono(int px) {
    QFont f(QStringLiteral("Consolas"));
    f.setStyleHint(QFont::Monospace);
    f.setPixelSize(px);
    return f;
}

QString styleSheet() {
    // Rules the design language fixes: 1px hairlines only, 2px corners
    // everywhere, red as the sole accent, and no light mode. Named placeholders
    // rather than positional ones, because this sheet outgrew nine of them.
    QString qss = QStringLiteral(R"(
QWidget              { background: $black; color: $fg2; }
QMainWindow, QDialog { background: $black; }
QLabel, QCheckBox, QRadioButton, QGroupBox, QSplitter, QScrollArea,
QDockWidget          { background: transparent; }

QFrame#Surface       { background: $surface; border: 1px solid $border;
                       border-radius: 2px; }

QMenuBar             { background: $black; border-bottom: 1px solid $border;
                       padding: 2px 6px; }
QMenuBar::item       { background: transparent; color: $fg3; padding: 5px 10px;
                       border-radius: 2px; }
QMenuBar::item:selected { background: $elevated; color: $fg1; }
QMenu                { background: $surface; border: 1px solid $border;
                       border-radius: 2px; padding: 4px; }
QMenu::item          { color: $fg2; padding: 6px 24px 6px 12px; border-radius: 2px; }
QMenu::item:selected { background: $elevated; color: $fg1; }
QMenu::item:disabled { color: $fg4; }
QMenu::separator     { height: 1px; background: $border; margin: 4px 6px; }

QToolBar             { background: $black; border: 0;
                       border-bottom: 1px solid $border; padding: 7px 12px;
                       spacing: 6px; }
QToolBar::separator  { width: 1px; background: $border; margin: 5px 6px; }
QToolButton          { background: $surface; border: 1px solid $border;
                       border-radius: 2px; padding: 7px 13px; color: $fg2; }
QToolButton:hover    { background: $elevated; border-color: $borderHi; color: $fg1; }
QToolButton:pressed,
QToolButton:checked  { background: $elevated; border-color: $red; color: $fg1; }
QToolButton:disabled { background: $black; color: $fg4; border-color: $border; }

QStatusBar           { background: $black; border-top: 1px solid $border;
                       color: $fg3; }
QStatusBar::item     { border: 0; }
QStatusBar QLabel    { color: $fg3; }
QDockWidget::title   { background: $black; border-top: 1px solid $border;
                       border-bottom: 1px solid $border; padding: 6px 12px;
                       text-align: left; }

QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
                       background: $surface; border: 1px solid $border;
                       border-radius: 2px; padding: 6px 8px; color: $fg1;
                       selection-background-color: $elevated;
                       selection-color: $fg1; }
QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover {
                       border-color: $borderHi; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus,
QDoubleSpinBox:focus, QComboBox:focus { border-color: $red; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled,
QComboBox:disabled   { background: $black; color: $fg4; }
QLineEdit#Filter     { padding: 7px 9px; }

QComboBox::drop-down { border: 0; width: 18px; }
QComboBox QAbstractItemView { background: $surface; border: 1px solid $border;
                       border-radius: 2px; outline: none; padding: 2px;
                       selection-background-color: $elevated;
                       selection-color: $fg1; }
QSpinBox::up-button, QDoubleSpinBox::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button {
                       background: $surface; border-left: 1px solid $border;
                       width: 15px; }
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
                       background: $elevated; }

QAbstractItemView    { background: $surface; border: 1px solid $border;
                       border-radius: 2px; outline: none;
                       alternate-background-color: $surface;
                       selection-background-color: $elevated;
                       selection-color: $fg1; }
QTableView           { gridline-color: transparent; }
QTableView::item, QTreeView::item { padding: 4px 10px; }
QTableView::item:hover, QTreeView::item:hover { background: $elevated; }
QHeaderView          { background: $black; border: 0; }
QHeaderView::section { background: $black; color: $fg4; border: 0;
                       border-bottom: 1px solid $border; padding: 7px 10px; }
QHeaderView::section:hover { color: $fg3; }

QPushButton          { background: $surface; border: 1px solid $border;
                       border-radius: 2px; padding: 9px 16px; color: $fg2; }
QPushButton:hover    { background: $elevated; border-color: $borderHi; color: $fg1; }
QPushButton:pressed  { background: $elevated; border-color: $red; }
QPushButton:disabled { background: $black; color: $fg4; border-color: $border; }
QPushButton#Primary  { background: $red; border: 1px solid $red; color: $black; }
QPushButton#Primary:hover { background: $redDark; border-color: $redDark; }
QPushButton#Primary:disabled { background: $surface; border-color: $border;
                       color: $fg4; }

QCheckBox, QRadioButton { color: $fg2; spacing: 8px; }
QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid $border;
                       border-radius: 2px; background: $surface; }
QRadioButton::indicator { width: 13px; height: 13px; border: 1px solid $border;
                       border-radius: 7px; background: $surface; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: $borderHi; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
                       background: $red; border-color: $red; }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled {
                       background: $black; border-color: $border; }

QGroupBox            { border: 1px solid $border; border-radius: 2px;
                       margin-top: 15px; padding: 12px 10px 10px 10px; }
QGroupBox::title     { subcontrol-origin: margin; subcontrol-position: top left;
                       left: 8px; padding: 0 6px; color: $fg3; }

QTabWidget::pane     { border: 1px solid $border; border-radius: 2px;
                       background: $surface; top: -1px; }
QTabBar::tab         { background: $black; color: $fg4; padding: 7px 14px;
                       margin-right: 2px; border: 1px solid $border;
                       border-bottom: 0; border-top-left-radius: 2px;
                       border-top-right-radius: 2px; }
QTabBar::tab:hover   { color: $fg2; }
QTabBar::tab:selected { background: $surface; color: $fg1;
                       border-top: 1px solid $red; }

QProgressBar         { background: $surface; border: 1px solid $border;
                       border-radius: 2px; text-align: center; color: $fg3;
                       padding: 0; }
QProgressBar::chunk  { background: $red; margin: 0; }

QSplitter::handle    { background: $border; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical   { height: 1px; }

QScrollBar:vertical  { background: $black; width: 10px; border: 0; margin: 0; }
QScrollBar::handle:vertical { background: $border; border-radius: 2px;
                       min-height: 24px; }
QScrollBar::handle:vertical:hover { background: $borderHi; }
QScrollBar:horizontal { background: $black; height: 10px; border: 0; margin: 0; }
QScrollBar::handle:horizontal { background: $border; border-radius: 2px;
                       min-width: 24px; }
QScrollBar::handle:horizontal:hover { background: $borderHi; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QToolTip             { background: $elevated; color: $fg2;
                       border: 1px solid $border; border-radius: 2px;
                       padding: 4px 6px; }
)");

    // Longest name first where one is a prefix of another: replacing $border
    // before $borderHi would leave a dangling "Hi" after a colour.
    const struct {
        const char *name;
        scribble::theme::Rgb value;
    } tokens[] = {
        {"$black", kBlack},       {"$surface", kSurface}, {"$elevated", kElevated},
        {"$borderHi", kBorderHi}, {"$border", kBorder},   {"$redDark", kRedDark},
        {"$red", kRed},           {"$fg1", kFg1},         {"$fg2", kFg2},
        {"$fg3", kFg3},           {"$fg4", kFg4},
    };
    for (const auto &t : tokens) {
        qss.replace(QLatin1String(t.name), hex(t.value));
    }
    return qss;
}

const QPixmap &grainTile() {
    static const QPixmap tile = [] {
        QImage img(128, 128, QImage::Format_ARGB32_Premultiplied);
        QRandomGenerator rng(0xA2C7F1u);  // fixed seed; see the header
        for (int y = 0; y < img.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const int v = static_cast<int>(rng.bounded(256));
                line[x] = qPremultiply(qRgba(v, v, v, 6));  // 6/255 is about 2.5%
            }
        }
        return QPixmap::fromImage(img);
    }();
    return tile;
}

}  // namespace scribble::gui::theme
