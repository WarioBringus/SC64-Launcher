#include <libdragon.h>

#include "fonts.h"
#include "theme.h"
#include "utils/fs.h"


static rdpq_font_t *default_font = NULL;
static rdpq_font_t *launcher_font = NULL;
static rdpq_font_t *title_font = NULL;


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
}

static void apply_font_styles (void) {
    apply_styles(default_font);
    if (launcher_font) {
        apply_styles(launcher_font);
    }
    if (title_font) {
        apply_styles(title_font);
    }
}

static void load_default_font (char *custom_font_path) {
    char *font_path = "rom:/menu/fonts/Firple-Bold.font64";

    if (custom_font_path != NULL && strlen(custom_font_path) > 0) {
        // Only check file_exists if custom_font_path is a valid filesystem path (not rom:/)
        if (strncmp(custom_font_path, "rom:/", 5) != 0 && file_exists(custom_font_path)) {
            font_path = custom_font_path;
        }
    }

    default_font = rdpq_font_load(font_path);
    launcher_font = rdpq_font_load("rom:/menu/fonts/Inter.font64");
    title_font = rdpq_font_load("rom:/menu/fonts/InterTitle.font64");

    apply_font_styles();

    rdpq_text_register_font(FNT_DEFAULT, default_font);
    rdpq_text_register_font(FNT_LAUNCHER, launcher_font);
    rdpq_text_register_font(FNT_TITLE, title_font);
}


void fonts_init (char *custom_font_path) {
    load_default_font(custom_font_path);
}

void fonts_apply_theme (void) {
    if (default_font) {
        apply_font_styles();
    }
}
