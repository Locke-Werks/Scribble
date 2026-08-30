#pragma once

#include <QDockWidget>

#include "types.hpp"

class QPlainTextEdit;

namespace scribble::gui {

/// Bottom dock that tails the run log. Lines are colour-coded by level and the
/// buffer is capped so a long batch cannot grow it without bound.
class LogDock : public QDockWidget {
    Q_OBJECT
public:
    explicit LogDock(QWidget *parent = nullptr);

    void append(scribble::LogLevel level, const QString &text);
    void clearLog();

private:
    QPlainTextEdit *view_ = nullptr;
};

}  // namespace scribble::gui
