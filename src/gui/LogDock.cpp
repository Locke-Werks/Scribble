#include "LogDock.hpp"

#include <QAction>
#include <QPlainTextEdit>

#include "ThemeQt.hpp"

namespace scribble::gui {

namespace {

// Status colours, used the one way the design language allows: to mark a line,
// never to accent a section. Debug and info stay on the foreground roles so a
// quiet run is monochrome and only the warnings carry colour.
QString levelColor(scribble::LogLevel level) {
    switch (level) {
        case scribble::LogLevel::Debug:
            return theme::c(scribble::theme::kFg4).name();
        case scribble::LogLevel::Info:
            return theme::c(scribble::theme::kFg2).name();
        case scribble::LogLevel::Warn:
            return theme::c(scribble::theme::kWarning).name();
        case scribble::LogLevel::Error:
            return theme::c(scribble::theme::kRed).name();
    }
    return theme::c(scribble::theme::kFg2).name();
}

}  // namespace

LogDock::LogDock(QWidget *parent) : QDockWidget(QStringLiteral("// LOG"), parent) {
    setObjectName(QStringLiteral("LogDock"));
    setFont(theme::tracked(11, QFont::DemiBold, 0.2));
    // The title bar is a section label, so it is tracked all-caps like the rest
    // of them. The View menu entry is not, and takes the title's place only
    // because Qt seeds it from the window title.
    toggleViewAction()->setText(QStringLiteral("Log"));

    view_ = new QPlainTextEdit(this);
    view_->setReadOnly(true);
    view_->setMaximumBlockCount(5000);  // keeps a long run from exhausting memory
    view_->setWordWrapMode(QTextOption::NoWrap);
    view_->setFont(theme::mono(11));
    view_->setFrameShape(QFrame::NoFrame);
    setWidget(view_);
}

void LogDock::append(scribble::LogLevel level, const QString &text) {
    QString escaped = text.toHtmlEscaped();
    escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    const QString label = QString::fromUtf8(scribble::log_level_name(level));
    view_->appendHtml(QStringLiteral("<span style=\"color:%1\">[%2] %3</span>")
                          .arg(levelColor(level), label, escaped));
}

void LogDock::clearLog() {
    view_->clear();
}

}  // namespace scribble::gui
