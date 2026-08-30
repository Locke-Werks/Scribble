#include "LogDock.hpp"

#include <QPlainTextEdit>

namespace scribe::gui {

namespace {

QString levelColor(scribe::LogLevel level) {
    switch (level) {
        case scribe::LogLevel::Debug:
            return QStringLiteral("#7a7f96");
        case scribe::LogLevel::Info:
            return QStringLiteral("#c6ccda");
        case scribe::LogLevel::Warn:
            return QStringLiteral("#e5c890");
        case scribe::LogLevel::Error:
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

void LogDock::append(scribe::LogLevel level, const QString &text) {
    QString escaped = text.toHtmlEscaped();
    escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    const QString label = QString::fromUtf8(scribe::log_level_name(level));
    view_->appendHtml(QStringLiteral("<span style=\"color:%1\">[%2] %3</span>")
                          .arg(levelColor(level), label, escaped));
}

void LogDock::clearLog() {
    view_->clear();
}

}  // namespace scribe::gui
