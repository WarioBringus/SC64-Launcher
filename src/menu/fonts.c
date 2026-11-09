#include <stdio.h>
#include <string.h>
#include <libdragon.h>

#include "fonts.h"
#include "theme.h"
#include "utils/fs.h"


static rdpq_font_t *default_font = NULL;
static rdpq_font_t *launcher_font = NULL;
static rdpq_font_t *title_font = NULL;
static rdpq_font_t *small_font = NULL;
static rdpq_font_t *wide_fonts[3] = { NULL, NULL, NULL };
static rdpq_font_t *large_fonts[2] = { NULL, NULL };   /* Text size: Large, and its 16:9 variant */
static char default_font_path[256] = "";
static bool default_font_loaded = false;


static void apply_styles (rdpq_font_t *font) {
    rdpq_font_style(font, STL_DEFAULT, &((rdpq_fontstyle_t) { .color = THEME_TEXT_DEFAULT }));
    rdpq_font_style(font, STL_GREEN, &((rdpq_fontstyle_t) { .color = THEME_TEXT_GREEN }));
    rdpq_font_style(font, STL_BLUE, &((rdpq_fontstyle_t) { .color = THEME_TEXT_BLUE }));
    rdpq_font_style(font, STL_YELLOW, &((rdpq_fontstyle_t) { .color = THEME_TEXT_YELLOW }));
    rdpq_font_style(font, STL_ORANGE, &((rdpq_fontstyle_t) { .color = THEME_TEXT_ORANGE }));
    rdpq_font_style(font, STL_RED, &((rdpq_fontstyle_t) { .color = THEME_TEXT_RED }));
    rdpq_font_style(font, STL_GRAY, &((rdpq_fontstyle_t) { .color = THEME_TEXT_GRAY }));
    rdpq_font_style(font, STL_ACCENT, &((rdpq_fontstyle_t) { .color = theme_current()->accent }));
    rdpq_font_style(font, STL_ON_ACCENT, &((rdpq_fontstyle_t) { .color = theme_current()->on_accent }));
    rdpq_font_style(font, STL_ON_BUTTON, &((rdpq_fontstyle_t) { .color = RGBA32(0xFF, 0xFF, 0xFF, 0xFF) }));
    rdpq_font_style(font, STL_ON_C_BUTTON, &((rdpq_fontstyle_t) { .color = RGBA32(0x2A, 0x22, 0x00, 0xFF) }));
}

static void apply_font_styles (void) {
    if (default_font) {
        apply_styles(default_font);
    }
    if (launcher_font) {
        apply_styles(launcher_font);
    }
    if (title_font) {
        apply_styles(title_font);
    }
    if (small_font) {
        apply_styles(small_font);
    }
    for (int i = 0; i < 3; i++) {
        if (wide_fonts[i]) {
            apply_styles(wide_fonts[i]);
        }
    }
    for (int i = 0; i < 2; i++) {
        if (large_fonts[i]) {
            apply_styles(large_fonts[i]);
        }
    }
}

/* From ui_components/launcher.c (its header clashes with this file's launcher_font). */
void launcher_text_cache_clear(void);

void fonts_load_default (void) {
    if (default_font_loaded) {
        return;
    }
    default_font_loaded = true;
    char *font_path = "rom:/menu/fonts/Firple-Bold.font64";

    if (strlen(default_font_path) > 0) {
        // Only check file_exists if custom_font_path is a valid filesystem path (not rom:/)
        if (strncmp(default_font_path, "rom:/", 5) != 0 && file_exists(default_font_path)) {
            font_path = default_font_path;
        }
    }

    rdpq_font_t *font = rdpq_font_load(font_path);
    apply_styles(font);
    rdpq_text_unregister_font(FNT_DEFAULT);     // The launcher font stood in until now.
    rdpq_text_register_font(FNT_DEFAULT, font);
    launcher_text_cache_clear();                // Layouts made with the stand-in no longer match.
    default_font = font;
}

static void load_default_font (char *custom_font_path) {
    if (custom_font_path != NULL) {
        snprintf(default_font_path, sizeof(default_font_path), "%s", custom_font_path);
    }

    launcher_font = rdpq_font_load("rom:/menu/fonts/Inter.font64");
    title_font = rdpq_font_load("rom:/menu/fonts/InterTitle.font64");
    small_font = rdpq_font_load("rom:/menu/fonts/InterSmall.font64");
    wide_fonts[0] = rdpq_font_load("rom:/menu/fonts/InterWide.font64");
    wide_fonts[1] = rdpq_font_load("rom:/menu/fonts/InterTitleWide.font64");
    wide_fonts[2] = rdpq_font_load("rom:/menu/fonts/InterSmallWide.font64");
    large_fonts[0] = rdpq_font_load("rom:/menu/fonts/InterLarge.font64");
    large_fonts[1] = rdpq_font_load("rom:/menu/fonts/InterLargeWide.font64");

    apply_font_styles();

    rdpq_text_register_font(FNT_DEFAULT, launcher_font);   // Until fonts_load_default() swaps Firple in.
    rdpq_text_register_font(FNT_LAUNCHER, launcher_font);
    rdpq_text_register_font(FNT_TITLE, title_font);
    rdpq_text_register_font(FNT_SMALL, small_font);
    rdpq_text_register_font(FNT_LAUNCHER_WIDE, wide_fonts[0]);
    rdpq_text_register_font(FNT_TITLE_WIDE, wide_fonts[1]);
    rdpq_text_register_font(FNT_SMALL_WIDE, wide_fonts[2]);
    rdpq_text_register_font(FNT_LARGE, large_fonts[0]);
    rdpq_text_register_font(FNT_LARGE_WIDE, large_fonts[1]);
}


void fonts_init (char *custom_font_path) {
    load_default_font(custom_font_path);
}

void fonts_apply_theme (void) {
    if (default_font) {
        apply_font_styles();
    }
}
