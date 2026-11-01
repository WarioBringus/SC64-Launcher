/**
 * @file theme.c
 * @brief Built-in color themes.
 * @ingroup menu
 */

#include <string.h>

#include "theme.h"

#define HEX(rgb) RGBA32(((rgb) >> 16) & 0xFF, ((rgb) >> 8) & 0xFF, (rgb) & 0xFF, 0xFF)

/* Text style colors for dark backgrounds (the stock menu's values). */
#define BRIGHT_TEXT_STYLES \
    .text_green = HEX(0x70FF70), .text_blue = HEX(0x70BCFF), .text_yellow = HEX(0xFFFF70), \
    .text_orange = HEX(0xFF9900), .text_red = HEX(0xFF4040)

/* Text style colors that stay readable on light backgrounds. */
#define DEEP_TEXT_STYLES \
    .text_green = HEX(0x1E7B34), .text_blue = HEX(0x1F5FBF), .text_yellow = HEX(0x8A6A00), \
    .text_orange = HEX(0xB85C00), .text_red = HEX(0xC0262D)

static const theme_t themes[] = {
    {
        .name = "Castle Sky",
        .bg = HEX(0x0E1A3A), .panel = HEX(0x172A57), .line = HEX(0x24386A), .highlight = HEX(0x2A4380),
        .accent = HEX(0xF8C81C), .accent2 = HEX(0xE0302A), .on_accent = HEX(0x0E1A3A),
        .text = HEX(0xF4F2EA), .text_muted = HEX(0xA9B4D0), BRIGHT_TEXT_STYLES,
    },
    {
        .name = "Midnight",
        .bg = HEX(0x111113), .panel = HEX(0x1D1D20), .line = HEX(0x2C2C30), .highlight = HEX(0x3A3A40),
        .accent = HEX(0xFFFFFF), .accent2 = HEX(0x4C7BE0), .on_accent = HEX(0x111113),
        .text = HEX(0xEDEDED), .text_muted = HEX(0x8E8E93), BRIGHT_TEXT_STYLES,
    },
    {
        .name = "Daylight",
        .bg = HEX(0xEFEDE6), .panel = HEX(0xFFFFFF), .line = HEX(0xD6D2C6), .highlight = HEX(0xDCE6F7),
        .accent = HEX(0x2F6FD6), .accent2 = HEX(0xE0302A), .on_accent = HEX(0xFFFFFF),
        .text = HEX(0x1E1E1E), .text_muted = HEX(0x6B6B6B), DEEP_TEXT_STYLES,
    },
    {
        .name = "Console Grey",
        .bg = HEX(0xA6A8AB), .panel = HEX(0xC3C5C7), .line = HEX(0x8C8F94), .highlight = HEX(0xE2E4E6),
        .accent = HEX(0x1E5BC6), .accent2 = HEX(0xD7262E), .on_accent = HEX(0xFFFFFF),
        .text = HEX(0x1C1D20), .text_muted = HEX(0x45484F), DEEP_TEXT_STYLES,
    },
    {
        .name = "Atomic Purple",
        .bg = HEX(0x24143A), .panel = HEX(0x36205A), .line = HEX(0x4C3178), .highlight = HEX(0x5A3C8C),
        .accent = HEX(0x6FE3D6), .accent2 = HEX(0xB98CFF), .on_accent = HEX(0x24143A),
        .text = HEX(0xF3ECFA), .text_muted = HEX(0xB9A6D1), BRIGHT_TEXT_STYLES,
    },
    {
        .name = "Jungle",
        .bg = HEX(0x10291A), .panel = HEX(0x1A3D27), .line = HEX(0x295536), .highlight = HEX(0x2F6040),
        .accent = HEX(0xF2B705), .accent2 = HEX(0xD2232A), .on_accent = HEX(0x10291A),
        .text = HEX(0xF2F0E0), .text_muted = HEX(0xA7C0A8), BRIGHT_TEXT_STYLES,
    },
    {
        .name = "Lava",
        .bg = HEX(0x1E0E0C), .panel = HEX(0x33160F), .line = HEX(0x4A2216), .highlight = HEX(0x5C2A1A),
        .accent = HEX(0xFF8A1F), .accent2 = HEX(0xF2D14B), .on_accent = HEX(0x1E0E0C),
        .text = HEX(0xFCEBD7), .text_muted = HEX(0xC9A08A), BRIGHT_TEXT_STYLES,
    },
    {
        .name = "Glacier",
        .bg = HEX(0x0F2735), .panel = HEX(0x173A4E), .line = HEX(0x22506A), .highlight = HEX(0x2A6283),
        .accent = HEX(0x7FE7FF), .accent2 = HEX(0x4FA3E0), .on_accent = HEX(0x0F2735),
        .text = HEX(0xEEF7FB), .text_muted = HEX(0x9FC3D4), BRIGHT_TEXT_STYLES,
    },
    {
        .name = "Pocket Green",
        .bg = HEX(0xA4B07E), .panel = HEX(0xB8C392), .line = HEX(0x6F7D4C), .highlight = HEX(0xCBD4A6),
        .accent = HEX(0x2F4020), .accent2 = HEX(0x4F6235), .on_accent = HEX(0xD6DDB8),
        .text = HEX(0x26331A), .text_muted = HEX(0x536140), DEEP_TEXT_STYLES,
    },
    {
        .name = "Virtual Red",
        .bg = HEX(0x000000), .panel = HEX(0x1A0000), .line = HEX(0x3D0000), .highlight = HEX(0x4A0000),
        .accent = HEX(0xFF2A2A), .accent2 = HEX(0xFF2A2A), .on_accent = HEX(0x000000),
        .text = HEX(0xFF2A2A), .text_muted = HEX(0xA31C1C), BRIGHT_TEXT_STYLES,
    },
};

#define THEME_COUNT ((int)(sizeof(themes) / sizeof(themes[0])))

static int current = 0;


int theme_count(void) {
    return THEME_COUNT;
}

const theme_t *theme_at(int index) {
    if (index < 0 || index >= THEME_COUNT) {
        return NULL;
    }
    return &themes[index];
}

int theme_find(const char *name) {
    if (name) {
        for (int i = 0; i < THEME_COUNT; i++) {
            if (strcasecmp(themes[i].name, name) == 0) {
                return i;
            }
        }
    }
    return 0;
}

void theme_set(int index) {
    current = (index >= 0 && index < THEME_COUNT) ? index : 0;
}

int theme_current_index(void) {
    return current;
}

const theme_t *theme_current(void) {
    return &themes[current];
}
