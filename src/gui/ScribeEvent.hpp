#pragma once

#include <QMetaType>

#include "events.hpp"

namespace scribble::gui {

/// Qt cannot queue a std::variant across a thread boundary on its own, so the
/// pipeline event is boxed in a registered value type. The worker thread emits
/// one of these; it is copied into the GUI thread's event loop and unpacked
/// there with std::visit.
struct ScribeEvent {
    scribble::Event ev;
};

inline void registerEventMetatype() {
    qRegisterMetaType<scribble::gui::ScribeEvent>("scribble::gui::ScribeEvent");
}

}  // namespace scribble::gui

Q_DECLARE_METATYPE(scribble::gui::ScribeEvent)
