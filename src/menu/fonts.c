#include <libdragon.h>

#include "fonts.h"
#include "theme.h"
#include "utils/fs.h"


static rdpq_font_t *default_font = NULL;


static void apply_font_styles (void) {
    rdpq_font_style(default_font, STL_DEFAULT, &((rdpq_fontstyle_t) { .color = THEME_TEXT_DEFAULT }));
    rdpq_font_style(default_font, STL_GREEN, &((rdpq_fontstyle_t) { .color = THEME_TEXT_GREEN }));
    rdpq_font_style(default_font, STL_BLUE, &((rdpq_fontstyle_t) { .color = THEME_TEXT_BLUE }));
    rdpq_font_style(default_font, STL_YELLOW, &((rdpq_fontstyle_t) { .color = THEME_TEXT_YELLOW }));
    rdpq_font_style(default_font, STL_ORANGE, &((rdpq_fontstyle_t) { .color = THEME_TEXT_ORANGE }));
    rdpq_font_style(default_font, STL_RED, &((rdpq_fontstyle_t) { .color = THEME_TEXT_RED }));
    rdpq_font_style(default_font, STL_GRAY, &((rdpq_fontstyle_t) { .color = THEME_TEXT_GRAY }));
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

    apply_font_styles();

    rdpq_text_register_font(FNT_DEFAULT, default_font);
}


void fonts_init (char *custom_font_path) {
    load_default_font(custom_font_path);
}

void fonts_apply_theme (void) {
    if (default_font) {
        apply_font_styles();
    }
}
