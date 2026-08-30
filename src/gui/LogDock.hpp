#pragma once

#include <QDockWidget>

#include "types.hpp"

class QPlainTextEdit;

namespace scribe::gui {

/// Bottom dock that tails the run log. Lines are colour-coded by level and the
/// buffer is capped so a long batch cannot grow it without bound.
class LogDock : public QDockWidget {
    Q_OBJECT
public:
    explicit LogDock(QWidget *parent = nullptr);

    void append(scribe::LogLevel level, const QString &text);
    void clearLog();

private:
    QPlainTextEdit *view_ = nullptr;
};

}  // namespace scribe::gui
