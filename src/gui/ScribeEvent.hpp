#pragma once

#include <QMetaType>

#include "events.hpp"

namespace scribe::gui {

/// Qt cannot queue a std::variant across a thread boundary on its own, so the
/// pipeline event is boxed in a registered value type. The worker thread emits
/// one of these; it is copied into the GUI thread's event loop and unpacked
/// there with std::visit.
struct ScribeEvent {
    scribe::Event ev;
};

inline void registerEventMetatype() {
    qRegisterMetaType<scribe::gui::ScribeEvent>("scribe::gui::ScribeEvent");
}

}  // namespace scribe::gui

Q_DECLARE_METATYPE(scribe::gui::ScribeEvent)
