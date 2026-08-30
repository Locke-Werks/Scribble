#include "SpeakerPalette.hpp"

#include <QHash>
#include <array>

namespace scribble::gui {

namespace {

// A pastel set that reads on both light and dark palettes. Size is coprime with
// nothing in particular; consecutive speaker ids simply land on different hues.
const std::array<QColor, 16> kPalette = {
    QColor(0x8c, 0xaa, 0xee), QColor(0xe7, 0x82, 0x84), QColor(0xa6, 0xd1, 0x89),
    QColor(0xe5, 0xc8, 0x90), QColor(0xca, 0x9e, 0xe6), QColor(0x81, 0xc8, 0xbe),
    QColor(0xef, 0x9f, 0x76), QColor(0x85, 0xc1, 0xdc), QColor(0xf4, 0xb8, 0xe4),
    QColor(0xba, 0xbb, 0xf1), QColor(0xee, 0xbe, 0xbe), QColor(0x99, 0xd1, 0xdb),
    QColor(0xe8, 0xa2, 0xaf), QColor(0xc6, 0xd0, 0xf5), QColor(0xa5, 0xad, 0xce),
    QColor(0xf0, 0xa8, 0x68),
};

}  // namespace

QColor SpeakerPalette::colorForGlobal(qint64 globalId) {
    if (globalId < 0) {
        return unknown();
    }
    return kPalette[static_cast<std::size_t>(globalId % kPalette.size())];
}

QColor SpeakerPalette::colorForKey(const QString &key) {
    const std::size_t idx = static_cast<std::size_t>(qHash(key)) % kPalette.size();
    QColor c = kPalette[idx];
    // Provisional colours read as slightly desaturated so a labelled-but-not-yet
    // resolved speaker looks visibly tentative next to a resolved one.
    return c.lighter(112);
}

QColor SpeakerPalette::unknown() {
    return QColor(0x94, 0x9c, 0xbb);
}

}  // namespace scribble::gui
