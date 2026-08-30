#include "LogDock.hpp"

#include <QPlainTextEdit>

namespace scribble::gui {

namespace {

QString levelColor(scribble::LogLevel level) {
    switch (level) {
        case scribble::LogLevel::Debug:
            return QStringLiteral("#7a7f96");
        case scribble::LogLevel::Info:
            return QStringLiteral("#c6ccda");
        case scribble::LogLevel::Warn:
            return QStringLiteral("#e5c890");
        case scribble::LogLevel::Error:
            return QStringLiteral("#e78284");
    }
    return QStringLiteral("#c6ccda");
}

}  // namespace

LogDock::LogDock(QWidget *parent) : QDockWidget(QStringLiteral("Log"), parent) {
    setObjectName(QStringLiteral("LogDock"));
    view_ = new QPlainTextEdit(this);
    view_->setReadOnly(true);
    view_->setMaximumBlockCount(5000);  // keeps a long run from exhausting memory
    view_->setWordWrapMode(QTextOption::NoWrap);
    QFont mono = view_->font();
    mono.setStyleHint(QFont::Monospace);
    mono.setFamily(QStringLiteral("Consolas"));
    view_->setFont(mono);
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
