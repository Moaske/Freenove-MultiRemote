#include "theme.h"
#include "settings.h"

namespace {

const Palette DARK = {
    rgb(0x0E1116), rgb(0x161B23), rgb(0x1B2029), rgb(0x2A3240),
    rgb(0xE8ECF2), rgb(0xB3BCC9), rgb(0x9AA4B2),
    rgb(0x4C9AFF), rgb(0x1E3A5F), rgb(0x8AB8FF),
    rgb(0xF59E0B), rgb(0x1A1206),
    rgb(0x3A2A12), rgb(0xFBBF24), rgb(0x161B23), rgb(0x9AA4B2),
    rgb(0x3A2A12), rgb(0xFBBF24),
    rgb(0x1B2029), rgb(0x1F7A3A), rgb(0x9A6700), rgb(0xB42318), rgb(0xFFFFFF),
    rgb(0xE5484D),
};

const Palette LIGHT = {
    rgb(0xF2F4F7), rgb(0xFFFFFF), rgb(0xE4E7EC), rgb(0xD0D5DD),
    rgb(0x101828), rgb(0x344054), rgb(0x667085),
    rgb(0x1D4ED8), rgb(0xDBEAFE), rgb(0x1D4ED8),
    rgb(0xF59E0B), rgb(0x1A1206),
    rgb(0xFEF3C7), rgb(0x92400E), rgb(0xFFFFFF), rgb(0x475467),
    rgb(0xFEF3C7), rgb(0x92400E),
    rgb(0xD0D5DD), rgb(0x1F7A3A), rgb(0x9A6700), rgb(0xB42318), rgb(0xFFFFFF),
    rgb(0xD92D20),
};

// An old-amp front panel: amber on near-black.
const Palette AMBER = {
    rgb(0x0F0A02), rgb(0x1C1405), rgb(0x2A1E08), rgb(0x3D2C0C),
    rgb(0xFFB000), rgb(0xD99600), rgb(0xA87400),
    rgb(0xFFB000), rgb(0x3A2A0A), rgb(0xFFC940),
    rgb(0xFFB000), rgb(0x1A1206),
    rgb(0x4A3510), rgb(0xFFC940), rgb(0x1C1405), rgb(0xA87400),
    rgb(0x4A3510), rgb(0xFFC940),
    rgb(0x2A1E08), rgb(0x8A6200), rgb(0x8A6200), rgb(0xB42318), rgb(0xFFE0A0),
    rgb(0xFF5A1F),
};

}  // namespace

const Palette& palette() {
    switch (Store::s.theme) {
    case THEME_LIGHT: return LIGHT;
    case THEME_AMBER: return AMBER;
    default:          return DARK;
    }
}
