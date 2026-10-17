/*
 * The built-in themes (docs/themes.md). Midnight is the original look. To
 * add one, add an entry to s_themes:
 * every role needs a color, and text must stay readable on every surface
 * (4.5:1 for small text).
 */
#include <string.h>
#include "ui_internal.h"

#define C(hex) LV_COLOR_MAKE(((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, (hex) & 0xFF)

static const ui_theme_t s_themes[] = {
    {
        .id = "midnight", .name = "Midnight", .light = false, .square = false,
        .bg = C(0x0B0C0E), .surface1 = C(0x15171B), .surface2 = C(0x1D2026), .surface3 = C(0x262A31),
        .line = C(0x2C3038), .line2 = C(0x3A3F48),
        .text = C(0xECEEF2), .text2 = C(0xA3AAB6), .text3 = C(0x7D8592),
        .accent = C(0xF2A93B), .on_accent = C(0x1A1204), .accent_text = C(0xF2A93B),
        .ir = C(0x3FC1AE), .on_ir = C(0x06201C), .ir_line = C(0x2E6E64),
        .good = C(0x5BD46A), .bad = C(0xE5484D),
        .sun = C(0xF2A93B), .sun_soft = C(0xF2C46B), .rain = C(0x6FA8FF), .storm = C(0xB79CFF),
        .cool = C(0x6FA8FF), .warm = C(0xF2A93B), .moon = C(0xC9CFE0),
    },
    {
        /* Light: the bright amber stays for fills, a darker one for text. */
        .id = "daylight", .name = "Daylight", .light = true, .square = false,
        .bg = C(0xF2F0EB), .surface1 = C(0xFFFFFF), .surface2 = C(0xEDEAE3), .surface3 = C(0xDDD8CE),
        .line = C(0xE3DED4), .line2 = C(0xC9C3B6),
        .text = C(0x16181C), .text2 = C(0x4C5058), .text3 = C(0x6E727A),
        .accent = C(0xF2A93B), .on_accent = C(0x1A1204), .accent_text = C(0x9C5A00),
        .ir = C(0x0E7466), .on_ir = C(0xFFFFFF), .ir_line = C(0x7FB8AE),
        .good = C(0x1E8E3E), .bad = C(0xC62F35),
        .sun = C(0xD98A00), .sun_soft = C(0xE0A030), .rain = C(0x2F6FD6), .storm = C(0x7B5CD6),
        .cool = C(0x2F6FD6), .warm = C(0xD98A00), .moon = C(0x6E7690),
    },
    {
        /* High contrast: pure black, brighter text and accents. */
        .id = "black", .name = "Black", .light = false, .square = false,
        .bg = C(0x000000), .surface1 = C(0x0C0C0C), .surface2 = C(0x161616), .surface3 = C(0x232323),
        .line = C(0x262626), .line2 = C(0x333333),
        .text = C(0xFFFFFF), .text2 = C(0xC4C4C4), .text3 = C(0x9A9A9A),
        .accent = C(0xFFB43B), .on_accent = C(0x1A1204), .accent_text = C(0xFFB43B),
        .ir = C(0x36E2C6), .on_ir = C(0x001F1A), .ir_line = C(0x1F7A6C),
        .good = C(0x5BE06C), .bad = C(0xFF5A5F),
        .sun = C(0xFFB43B), .sun_soft = C(0xFFCF70), .rain = C(0x7DB4FF), .storm = C(0xC2A8FF),
        .cool = C(0x7DB4FF), .warm = C(0xFFB43B), .moon = C(0xD6DBE8),
    },
    {
        /* Indigo with coral. */
        .id = "dusk", .name = "Dusk", .light = false, .square = false,
        .bg = C(0x100D1C), .surface1 = C(0x19152B), .surface2 = C(0x221D3A), .surface3 = C(0x2E2850),
        .line = C(0x332C55), .line2 = C(0x433B6B),
        .text = C(0xF0ECFB), .text2 = C(0xB7AFD6), .text3 = C(0x8C84AE),
        .accent = C(0xFF9466), .on_accent = C(0x2A0E04), .accent_text = C(0xFF9466),
        .ir = C(0x74D0FF), .on_ir = C(0x06202E), .ir_line = C(0x2F5F80),
        .good = C(0x6FDCA0), .bad = C(0xFF6B7A),
        .sun = C(0xFFB86B), .sun_soft = C(0xFFD19A), .rain = C(0x74D0FF), .storm = C(0xC9A6FF),
        .cool = C(0x74D0FF), .warm = C(0xFF9466), .moon = C(0xD2CCF0),
    },
    {
        /* Green phosphor, square corners. */
        .id = "terminal", .name = "Terminal", .light = false, .square = true,
        .bg = C(0x050B06), .surface1 = C(0x0A160C), .surface2 = C(0x102012), .surface3 = C(0x183019),
        .line = C(0x1F3A23), .line2 = C(0x2A4D2F),
        .text = C(0xC7F7CF), .text2 = C(0x86C793), .text3 = C(0x5E9A6A),
        .accent = C(0xF4D35E), .on_accent = C(0x1A1600), .accent_text = C(0xF4D35E),
        .ir = C(0x5EF2A0), .on_ir = C(0x03200F), .ir_line = C(0x2A7A4E),
        .good = C(0x5EF2A0), .bad = C(0xFF6E6E),
        .sun = C(0xF4D35E), .sun_soft = C(0xF7E08E), .rain = C(0x7FD4F0), .storm = C(0xC4A8FF),
        .cool = C(0x7FD4F0), .warm = C(0xF4D35E), .moon = C(0xC7F7CF),
    },
};

#define THEME_COUNT (sizeof(s_themes) / sizeof(s_themes[0]))

static const ui_theme_t *s_active = &s_themes[0];

const ui_theme_t *ui_theme(void)
{
    return s_active;
}

size_t ui_theme_count(void)
{
    return THEME_COUNT;
}

const ui_theme_t *ui_theme_at(size_t index)
{
    return index < THEME_COUNT ? &s_themes[index] : NULL;
}

void ui_set_theme(const char *id)
{
    for (size_t i = 0; id && i < THEME_COUNT; i++) {
        if (strcmp(s_themes[i].id, id) == 0) {
            s_active = &s_themes[i];
            return;
        }
    }
}

int32_t ui_radius(int32_t r)
{
    if (r >= LV_RADIUS_CIRCLE) {
        return r;
    }
    if (s_active->square) {
        r = r >= 18 ? 4 : r >= 12 ? 3 : r > 2 ? 2 : r;
    }
    return UI_S(r);
}

lv_color_t ui_pressed(lv_color_t bg)
{
    return s_active->light ? lv_color_darken(bg, 24) : lv_color_lighten(bg, 28);
}
