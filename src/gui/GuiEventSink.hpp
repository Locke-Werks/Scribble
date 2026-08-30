#pragma once

#include <QObject>

#include "ScribeEvent.hpp"
#include "events.hpp"

namespace scribe::gui {

/// The bridge required by the threading contract. `handle` runs on the pipeline
/// worker thread and must not block, so it does the one cheap thing it can: copy
/// the event and emit it. The connection into the GUI is queued, so the copy is
/// handed to the GUI thread's event loop and every widget touch happens there.
class GuiEventSink : public QObject, public scribe::EventSink {
    Q_OBJECT
public:
    using QObject::QObject;

    void handle(const scribe::Event &e) override { emit event(ScribeEvent{e}); }

signals:
    void event(const scribe::gui::ScribeEvent &e);
};

}  // namespace scribe::gui
