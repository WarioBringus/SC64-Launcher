/**
 * @file launcher.c
 * @brief Drawing and navigation helpers shared by the launcher views.
 * @ingroup ui_components
 *
 * Launcher views lay out in logical coordinates. In 16:9 mode the logical screen is 853 pixels
 * wide and every helper here squeezes horizontal positions by 3/4 into the real 640-pixel frame;
 * the TV (or the Analogue 3D's stretch setting) widens it back, so shapes and text keep their
 * proportions. In 4:3 mode logical and real coordinates are the same.
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../launcher_ui.h"
#include "../library.h"
#include "../settings.h"
#include "../sound.h"
#include "../theme.h"
#include "../ui_components.h"

#define HEADER_TAB_SPACING      (24)
#define HEADER_UNDERLINE_Y      (LAUNCHER_HEADER_Y + 21)
#define TEXT_HEIGHT             (24)
#define TITLE_HEIGHT            (30)
#define TILE_RADIUS             (6)
#define PILL_HEIGHT             (24)
#define PILL_PADDING            (12)
#define HINT_BUTTON_SIZE        (16)
/* rdpq drops a line taller than its text box, so text boxes get headroom over the font's line height. */
#define HINT_TEXT_HEIGHT        (22)
#define HINT_ICON_GAP           (5)
#define HINT_GAP                (14)
#define WIDTH_CACHE_SIZE        (48)

#define SCREEN_WIDTH_4_3        (640)
#define SCREEN_WIDTH_16_9       (853)   /* 640 * 4 / 3 */
#define WIDE_SQUEEZE            (0.75f)

/* Button colors match the N64 controller. */
#define BUTTON_A_COLOR          RGBA32(0x4C, 0x7B, 0xE0, 0xFF)
#define BUTTON_B_COLOR          RGBA32(0x3F, 0xA3, 0x4D, 0xFF)
#define BUTTON_START_COLOR      RGBA32(0xE0, 0x30, 0x2A, 0xFF)
#define BUTTON_C_COLOR          RGBA32(0xF5, 0xC5, 0x18, 0xFF)
#define BUTTON_C_INK            RGBA32(0x2A, 0x22, 0x00, 0xFF)
#define C_ARROWS_WIDTH          (9)     /* Room for the drawn arrows beside the "C" */

static bool widescreen = false;


void launcher_set_widescreen (bool enabled) {
    widescreen = enabled;
}

bool launcher_is_widescreen (void) {
    return widescreen;
}

int launcher_screen_width (void) {
    return widescreen ? SCREEN_WIDTH_16_9 : SCREEN_WIDTH_4_3;
}

int launcher_x0 (void) {
    // The same TV-safe margin as 4:3 (32 px), in logical units.
    return widescreen ? 43 : 32;
}

int launcher_x1 (void) {
    return launcher_screen_width() - launcher_x0();
}

/** @brief Logical x to real framebuffer x. */
static int px (int x) {
    return widescreen ? (x * 3 + 2) / 4 : x;
}

/** @brief Real pixel width to logical width. */
static int logical_width (int width) {
    return widescreen ? (width * 4 + 2) / 3 : width;
}

uint8_t launcher_font (uint8_t font) {
    if (!widescreen) {
        return font;
    }
    switch (font) {
        case FNT_LAUNCHER: return FNT_LAUNCHER_WIDE;
        case FNT_TITLE: return FNT_TITLE_WIDE;
        case FNT_SMALL: return FNT_SMALL_WIDE;
        default: return font;   // The stock font has no squeezed variant.
    }
}


/**
 * @brief Copy text, doubling '$' and '^' so rdpq prints them instead of reading them as font/style codes.
 */
static const char *escape_text_codes (const char *text, char *buffer, size_t size) {
    size_t o = 0;
    for (size_t i = 0; text[i] != '\0' && o + 2 < size; i++) {
        if (text[i] == '$' || text[i] == '^') {
            buffer[o++] = text[i];
        }
        buffer[o++] = text[i];
    }
    buffer[o] = '\0';
    return buffer;
}

/**
 * @brief Draw already-escaped text inside a box given in logical coordinates.
 *
 * @return Logical width of the drawn text.
 */
static int text_box_font (uint8_t font, int x, int y, int w, int h, rdpq_align_t align, rdpq_valign_t valign, menu_font_style_t style, const char *text) {
    int real_w = (w > 0) ? px(x + w) - px(x) : 0;
    rdpq_textmetrics_t metrics = rdpq_text_printn(
        &(rdpq_textparms_t) {
            .style_id = style,
            .width = real_w,
            .height = h,
            .align = align,
            .valign = valign,
            .wrap = (real_w > 0) ? WRAP_ELLIPSES : WRAP_NONE,
        },
        launcher_font(font),
        px(x),
        y,
        text,
        strlen(text)
    );
    return logical_width((int)(metrics.advance_x));
}

/**
 * @brief Inter has no CJK glyphs; text with characters from U+0800 up (3-byte UTF-8) uses the stock font.
 */
static uint8_t font_for (const char *text, uint8_t preferred) {
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p >= 0xE0) {
            return FNT_DEFAULT;
        }
    }
    return preferred;
}

static int text_box (int x, int y, int w, int h, rdpq_align_t align, rdpq_valign_t valign, menu_font_style_t style, const char *text) {
    return text_box_font(font_for(text, FNT_LAUNCHER), x, y, w, h, align, valign, style, text);
}

typedef struct {
    const char *text;
    size_t length;
    uint8_t font;
    int width;
} width_entry_t;

static width_entry_t width_cache[WIDTH_CACHE_SIZE];
static int width_cache_next = 0;

/**
 * @brief Logical width of already-escaped text, remembered for strings that stay in memory
 * (hint labels, category names) so they aren't laid out again every frame.
 */
static int text_width_font (uint8_t font, const char *text) {
    uint8_t real_font = launcher_font(font);
    size_t length = strlen(text);
    for (int i = 0; i < WIDTH_CACHE_SIZE; i++) {
        width_entry_t *e = &width_cache[i];
        if (e->text == text && e->length == length && e->font == real_font) {
            return logical_width(e->width);
        }
    }
    int nbytes = (int)length;
    rdpq_paragraph_t *layout = rdpq_paragraph_build(&(rdpq_textparms_t) { 0 }, real_font, text, &nbytes);
    int width = (int)(layout->bbox.x1 - layout->bbox.x0);
    rdpq_paragraph_free(layout);

    width_entry_t *e = &width_cache[width_cache_next];
    width_cache_next = (width_cache_next + 1) % WIDTH_CACHE_SIZE;
    e->text = text;
    e->length = length;
    e->font = real_font;
    e->width = width;
    return logical_width(width);
}

static int text_width (const char *text) {
    return text_width_font(font_for(text, FNT_LAUNCHER), text);
}

int launcher_text (int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *fmt, ...) {
    char buffer[256];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, va);
    va_end(va);
    return text_box(x, y, width, TEXT_HEIGHT, align, VALIGN_TOP, style, buffer);
}

int launcher_name (int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *name) {
    char escaped[256];
    escape_text_codes(name, escaped, sizeof(escaped));
    return text_box(x, y, width, TEXT_HEIGHT, align, VALIGN_TOP, style, escaped);
}

int launcher_small (int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *text) {
    char escaped[256];
    escape_text_codes(text, escaped, sizeof(escaped));
    return text_box_font(font_for(escaped, FNT_SMALL), x, y, width, HINT_TEXT_HEIGHT, align, VALIGN_TOP, style, escaped);
}

int launcher_title (int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *name) {
    char escaped[256];
    escape_text_codes(name, escaped, sizeof(escaped));
    return text_box_font(font_for(escaped, FNT_TITLE), x, y, width, TITLE_HEIGHT, align, VALIGN_TOP, style, escaped);
}

/** @brief Horizontal inset of row i (from the top or bottom edge) of a rounded corner. */
static int corner_inset (int radius, int i) {
    float dy = radius - i - 0.5f;
    return (int)(radius - sqrtf((float)(radius * radius) - dy * dy) + 0.5f);
}

/* Small shapes are drawn in 1-cycle mode with a flat color, which handles any pixel edge. */
static void flat_begin (color_t color) {
    rdpq_mode_push();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_set_prim_color(color);
}

static void flat_end (void) {
    rdpq_mode_pop();
}

/**
 * @brief Rounded rectangle in real pixels; corner insets are multiplied by squeeze (1 or 3/4).
 */
static void fill_rounded_real (int x0, int y0, int x1, int y1, int radius, float squeeze, color_t color) {
    if (radius * 2 > y1 - y0) {
        radius = (y1 - y0) / 2;
    }
    if ((int)(radius * squeeze) * 2 > x1 - x0) {
        radius = (int)((x1 - x0) / (2 * squeeze));
    }
    flat_begin(color);
    rdpq_fill_rectangle(x0, y0 + radius, x1, y1 - radius);
    for (int i = 0; i < radius; i++) {
        int inset = (int)(corner_inset(radius, i) * squeeze + 0.5f);
        rdpq_fill_rectangle(x0 + inset, y0 + i, x1 - inset, y0 + i + 1);
        rdpq_fill_rectangle(x0 + inset, y1 - 1 - i, x1 - inset, y1 - i);
    }
    flat_end();
}

void launcher_fill_rounded (int x0, int y0, int x1, int y1, int radius, color_t color) {
    fill_rounded_real(px(x0), y0, px(x1), y1, radius, widescreen ? WIDE_SQUEEZE : 1.0f, color);
}

void launcher_fill_rounded_px (int x0, int y0, int x1, int y1, int radius, color_t color) {
    fill_rounded_real(x0, y0, x1, y1, radius, 1.0f, color);
}

/**
 * @brief Round off the corners of something already drawn by painting them in the color behind it.
 * Coordinates are logical.
 */
static void mask_corners (int x0, int y0, int x1, int y1, int radius, color_t behind) {
    float squeeze = widescreen ? WIDE_SQUEEZE : 1.0f;
    int rx0 = px(x0);
    int rx1 = px(x1);
    flat_begin(behind);
    for (int i = 0; i < radius; i++) {
        int inset = (int)(corner_inset(radius, i) * squeeze + 0.5f);
        if (inset <= 0) {
            continue;
        }
        rdpq_fill_rectangle(rx0, y0 + i, rx0 + inset, y0 + i + 1);
        rdpq_fill_rectangle(rx1 - inset, y0 + i, rx1, y0 + i + 1);
        rdpq_fill_rectangle(rx0, y1 - 1 - i, rx0 + inset, y1 - i);
        rdpq_fill_rectangle(rx1 - inset, y1 - 1 - i, rx1, y1 - i);
    }
    flat_end();
}

void launcher_draw_page_title (const char *title) {
    int w = launcher_text(LAUNCHER_X0, LAUNCHER_HEADER_Y, 0, ALIGN_LEFT, STL_DEFAULT, "%s", title);
    launcher_fill_rounded(LAUNCHER_X0, HEADER_UNDERLINE_Y, LAUNCHER_X0 + w, HEADER_UNDERLINE_Y + 3, 1, theme_current()->accent2);
}

void launcher_draw_header (menu_t *menu, launcher_tab_t active) {
    ui_components_background_draw();

    static const char *tabs[] = { "Home", "Library" };
    int x = LAUNCHER_X0;
    for (int i = 0; i < 2; i++) {
        bool on = (i == (int)active);
        int w = launcher_text(x, LAUNCHER_HEADER_Y, 0, ALIGN_LEFT, on ? STL_DEFAULT : STL_GRAY, "%s", tabs[i]);
        if (on) {
            launcher_fill_rounded(x, HEADER_UNDERLINE_Y, x + w, HEADER_UNDERLINE_Y + 3, 1, theme_current()->accent2);
        }
        x += w + HEADER_TAB_SPACING;
    }

    if (menu->current_time >= 0) {
        char clock[16];
        struct tm *t = localtime(&menu->current_time);
        strftime(clock, sizeof(clock), "%I:%M %p", t);
        launcher_text(LAUNCHER_X0, LAUNCHER_HEADER_Y, LAUNCHER_WIDTH, ALIGN_RIGHT, STL_GRAY, "%s", clock[0] == '0' ? clock + 1 : clock);
    }
}

int launcher_draw_pill (int x, int y, const char *name, bool active) {
    char escaped[128];
    escape_text_codes(name, escaped, sizeof(escaped));
    // '$' and '^' are rare in folder names; measuring the original keeps the width cached.
    int w = text_width(strpbrk(name, "$^") ? escaped : name) + PILL_PADDING * 2;
    launcher_fill_rounded(x, y, x + w, y + PILL_HEIGHT, PILL_HEIGHT / 2, active ? theme_current()->accent : theme_current()->panel);
    text_box(x, y, w, PILL_HEIGHT, ALIGN_CENTER, VALIGN_CENTER, active ? STL_ON_ACCENT : STL_GRAY, escaped);
    return w;
}

/**
 * @brief Small solid triangle pointing up, down, left or right, centered on logical (cx, cy).
 */
static void draw_arrow (int cx, int cy, int dx, int dy) {
    int rcx = px(cx);
    flat_begin(BUTTON_C_INK);
    for (int i = 0; i < 3; i++) {
        int half = i;       // rows widen by one pixel each side
        if (dy != 0) {
            int y = (dy < 0) ? cy - 1 + i : cy + 1 - i;
            rdpq_fill_rectangle(rcx - half, y, rcx + half + 1, y + 1);
        } else {
            int x = (dx < 0) ? rcx - 1 + i : rcx + 1 - i;
            rdpq_fill_rectangle(x, cy - half, x + 1, cy + half + 1);
        }
    }
    flat_end();
}

static bool is_c_button (launcher_button_t kind) {
    return kind == LAUNCHER_BUTTON_C_UP_DOWN || kind == LAUNCHER_BUTTON_C_LEFT_RIGHT;
}

void launcher_draw_footer (const launcher_hint_t *hints, int count) {
    ui_components_box_draw(px(LAUNCHER_X0), LAUNCHER_FOOTER_Y, px(LAUNCHER_X1), LAUNCHER_FOOTER_Y + 1, theme_current()->line);

    // Lay the hints out right-aligned: [button] label   [button] label ...
    int button_w[8];
    int label_w[8];
    int total = 0;
    if (count > 8) {
        count = 8;
    }
    // Measured widths are glyph extents; a little slack keeps the last letter from touching the next icon.
    for (int i = 0; i < count; i++) {
        int bw = text_width_font(FNT_SMALL, hints[i].button) + 9 + (is_c_button(hints[i].kind) ? C_ARROWS_WIDTH : 0);
        button_w[i] = (bw < HINT_BUTTON_SIZE) ? HINT_BUTTON_SIZE : bw;
        label_w[i] = text_width_font(FNT_SMALL, hints[i].label) + 2;
        total += button_w[i] + HINT_ICON_GAP + label_w[i] + (i > 0 ? HINT_GAP : 0);
    }

    int x = LAUNCHER_X1 - total;
    int y = LAUNCHER_FOOTER_Y + 12;
    int text_y = y - (HINT_TEXT_HEIGHT - HINT_BUTTON_SIZE) / 2;
    for (int i = 0; i < count; i++) {
        color_t color;
        switch (hints[i].kind) {
            case LAUNCHER_BUTTON_A: color = BUTTON_A_COLOR; break;
            case LAUNCHER_BUTTON_B: color = BUTTON_B_COLOR; break;
            case LAUNCHER_BUTTON_START: color = BUTTON_START_COLOR; break;
            case LAUNCHER_BUTTON_C_UP_DOWN:
            case LAUNCHER_BUTTON_C_LEFT_RIGHT: color = BUTTON_C_COLOR; break;
            default: color = theme_current()->text_muted; break;
        }
        launcher_fill_rounded(x, y, x + button_w[i], y + HINT_BUTTON_SIZE, HINT_BUTTON_SIZE / 2, color);
        if (is_c_button(hints[i].kind)) {
            // "C" on the left, a pair of arrows drawn on the right.
            int letter_w = button_w[i] - C_ARROWS_WIDTH;
            text_box_font(FNT_SMALL, x + 1, text_y, letter_w, HINT_TEXT_HEIGHT, ALIGN_CENTER, VALIGN_CENTER, STL_ON_C_BUTTON, hints[i].button);
            int ax = x + letter_w + 1;
            int cy = y + HINT_BUTTON_SIZE / 2;
            if (hints[i].kind == LAUNCHER_BUTTON_C_UP_DOWN) {
                draw_arrow(ax, cy - 3, 0, -1);
                draw_arrow(ax, cy + 3, 0, 1);
            } else {
                draw_arrow(ax - 2, cy, -1, 0);
                draw_arrow(ax + 3, cy, 1, 0);
            }
        } else {
            text_box_font(FNT_SMALL, x, text_y, button_w[i], HINT_TEXT_HEIGHT, ALIGN_CENTER, VALIGN_CENTER, STL_ON_BUTTON, hints[i].button);
        }
        x += button_w[i] + HINT_ICON_GAP;
        // No width: the label is never cut short, whatever the measurement said.
        text_box_font(FNT_SMALL, x, text_y, 0, HINT_TEXT_HEIGHT, ALIGN_LEFT, VALIGN_CENTER, STL_DEFAULT, hints[i].label);
        x += label_w[i] + HINT_GAP;
    }
}

void launcher_draw_ring (int x0, int y0, int x1, int y1, int radius) {
    launcher_fill_rounded(x0 - LAUNCHER_RING, y0 - LAUNCHER_RING, x1 + LAUNCHER_RING, y1 + LAUNCHER_RING,
        radius + LAUNCHER_RING, theme_current()->accent);
}

/**
 * @brief Draw a cover at logical (x, y) at its own size: copied 1:1 in 4:3, squeezed to 3/4 width in 16:9.
 */
static void blit_cover (surface_t *image, int x, int y) {
    rdpq_mode_push();
    if (widescreen) {
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_tex_blit(image, px(x), y, &(rdpq_blitparms_t) { .scale_x = WIDE_SQUEEZE, .scale_y = 1.0f });
    } else {
        rdpq_set_mode_copy(false);
        rdpq_tex_blit(image, x, y, NULL);
    }
    rdpq_mode_pop();
}

void launcher_draw_tile (int x, int y, int w, int h, surface_t *image, bool pending, const char *title, bool selected, color_t behind) {
    color_t edge = behind;
    if (selected) {
        launcher_draw_ring(x, y, x + w, y + h, TILE_RADIUS);
        edge = theme_current()->accent;
    }

    // Covers that nearly fill the tile are drawn edge to edge; others sit on a rounded panel.
    bool fills_tile = image && image->width >= w - 2 && image->height >= h - 2;
    if (!fills_tile) {
        launcher_fill_rounded(x, y, x + w, y + h, TILE_RADIUS, theme_current()->panel);
    }

    if (image) {
        int ix = x + (w - image->width) / 2;
        int iy = y + (h - image->height) / 2;
        blit_cover(image, ix, iy);
        if (fills_tile) {
            mask_corners(ix, iy, ix + image->width, iy + image->height, TILE_RADIUS, edge);
        }
    } else if (!pending && title) {
        char escaped[256];
        escape_text_codes(title, escaped, sizeof(escaped));
        rdpq_text_printn(
            &(rdpq_textparms_t) {
                .style_id = STL_GRAY,
                .width = px(x + w - 4) - px(x + 4),
                .height = h - 8,
                .align = ALIGN_CENTER,
                .valign = VALIGN_CENTER,
                .wrap = WRAP_WORD,
            },
            launcher_font(font_for(escaped, FNT_LAUNCHER)),
            px(x + 4),
            y + 4,
            escaped,
            strlen(escaped)
        );
    }
}

void launcher_draw_cover_scaled (surface_t *image, int x, int y, int w, int h, color_t behind) {
    float scale = fminf((float)w / image->width, (float)h / image->height);
    int dw = (int)(image->width * scale);
    int dh = (int)(image->height * scale);
    int dx = x + (w - dw) / 2;
    int dy = y + (h - dh) / 2;
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_tex_blit(image, px(dx), dy, &(rdpq_blitparms_t) {
            .scale_x = scale * (widescreen ? WIDE_SQUEEZE : 1.0f),
            .scale_y = scale,
        });
    rdpq_mode_pop();
    mask_corners(dx, dy, dx + dw, dy + dh, (int)(TILE_RADIUS * scale + 0.5f), behind);
}

void launcher_leave (menu_t *menu, menu_mode_t next_mode) {
    // Category, tile size and layout changes made with the controller are saved here, in one write,
    // rather than on every button press (an SD card write is a visible stall).
    settings_save(&menu->settings);
    menu->launcher_return_mode = menu->mode;
    menu->next_mode = next_mode;
}

static void open_screen (menu_t *menu, void *arg) {
    launcher_leave(menu, (menu_mode_t)(arg));
}

static void open_file_browser (menu_t *menu, void *arg) {
    (void)arg;
    // The file browser is a destination of its own; screens opened from it return to it.
    menu->launcher_return_mode = MENU_MODE_NONE;
    menu->next_mode = MENU_MODE_BROWSER;
}

void launcher_apply_theme (menu_t *menu, int index) {
    theme_set(index);
    fonts_apply_theme();
    if (ui_components_background_get_image()) {
        // The background is tinted with the theme when loaded, so reload it from cache.
        ui_components_background_image_free_only();
        ui_components_background_reload();
    }
    free(menu->settings.theme);
    menu->settings.theme = strdup(theme_current()->name);
    settings_save(&menu->settings);
}

static void set_theme (menu_t *menu, void *arg) {
    launcher_apply_theme(menu, (int)(uintptr_t)(arg));
}

static int get_theme_selection (menu_t *menu) {
    return theme_current_index();
}

// Labels are filled from theme.c in launcher_menu_init.
static component_context_menu_t theme_menu = {
    .get_default_selection = get_theme_selection,
    .list = {
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(0) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(1) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(2) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(3) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(4) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(5) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(6) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(7) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(8) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(9) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static void set_tile_size (menu_t *menu, void *arg) {
    menu->settings.launcher_tile_size = (int)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static int get_tile_size_selection (menu_t *menu) {
    return menu->settings.launcher_tile_size;
}

static component_context_menu_t tile_size_menu = {
    .get_default_selection = get_tile_size_selection,
    .list = {
        { .text = "Small", .action = set_tile_size, .arg = (void *)(uintptr_t)(LAUNCHER_TILE_SMALL) },
        { .text = "Medium", .action = set_tile_size, .arg = (void *)(uintptr_t)(LAUNCHER_TILE_MEDIUM) },
        { .text = "Large", .action = set_tile_size, .arg = (void *)(uintptr_t)(LAUNCHER_TILE_LARGE) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static void set_layout (menu_t *menu, void *arg) {
    menu->settings.launcher_layout = (int)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static int get_layout_selection (menu_t *menu) {
    return menu->settings.launcher_layout;
}

static component_context_menu_t layout_menu = {
    .get_default_selection = get_layout_selection,
    .list = {
        { .text = "Grid", .action = set_layout, .arg = (void *)(uintptr_t)(LAUNCHER_LAYOUT_GRID) },
        { .text = "Carousel", .action = set_layout, .arg = (void *)(uintptr_t)(LAUNCHER_LAYOUT_CAROUSEL) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static void set_sort (menu_t *menu, void *arg) {
    menu->settings.launcher_sort = (int)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static int get_sort_selection (menu_t *menu) {
    return menu->settings.launcher_sort;
}

static component_context_menu_t sort_menu = {
    .get_default_selection = get_sort_selection,
    .list = {
        { .text = "Title", .action = set_sort, .arg = (void *)(uintptr_t)(LAUNCHER_SORT_TITLE) },
        { .text = "Genre", .action = set_sort, .arg = (void *)(uintptr_t)(LAUNCHER_SORT_GENRE) },
        { .text = "Developer", .action = set_sort, .arg = (void *)(uintptr_t)(LAUNCHER_SORT_DEVELOPER) },
        { .text = "Year", .action = set_sort, .arg = (void *)(uintptr_t)(LAUNCHER_SORT_YEAR) },
        { .text = "Players", .action = set_sort, .arg = (void *)(uintptr_t)(LAUNCHER_SORT_PLAYERS) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static void set_titles (menu_t *menu, void *arg) {
    menu->settings.launcher_titles_highlight_only = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static int get_titles_selection (menu_t *menu) {
    return menu->settings.launcher_titles_highlight_only ? 0 : 1;
}

static component_context_menu_t titles_menu = {
    .get_default_selection = get_titles_selection,
    .list = {
        { .text = "Highlighted only", .action = set_titles, .arg = (void *)(uintptr_t)(true) },
        { .text = "Always", .action = set_titles, .arg = (void *)(uintptr_t)(false) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

/* Start menu state: the highlighted game, and requests for the library view. */
static path_t *selected_rom = NULL;
static launcher_request_t pending_request = LAUNCHER_REQUEST_NONE;
static bool favorites_changed = false;
static char favorite_label[32] = "Add to favorites";

static int favorite_index (menu_t *menu, path_t *rom) {
    for (int i = 0; rom && i < FAVORITES_COUNT; i++) {
        bookkeeping_item_t *item = &menu->bookkeeping.favorite_items[i];
        if (item->bookkeeping_type == BOOKKEEPING_TYPE_ROM && path_are_match(item->primary_path, rom)) {
            return i;
        }
    }
    return -1;
}

static void toggle_favorite (menu_t *menu, void *arg) {
    (void)arg;
    if (!selected_rom) {
        return;
    }
    int index = favorite_index(menu, selected_rom);
    if (index >= 0) {
        bookkeeping_favorite_remove(&menu->bookkeeping, index);
    } else {
        bookkeeping_favorite_add(&menu->bookkeeping, selected_rom, NULL, BOOKKEEPING_TYPE_ROM);
    }
    favorites_changed = true;
}

static void request (menu_t *menu, launcher_request_t r) {
    pending_request = r;
    // These act on the library; from Home, go there first.
    if (menu->mode == MENU_MODE_HOME) {
        menu->next_mode = MENU_MODE_LIBRARY;
    }
}

static void surprise_me (menu_t *menu, void *arg) {
    (void)arg;
    request(menu, LAUNCHER_REQUEST_SURPRISE);
}

static void open_filter (menu_t *menu, void *arg) {
    (void)arg;
    request(menu, LAUNCHER_REQUEST_FILTER);
}

launcher_request_t launcher_take_request (void) {
    launcher_request_t r = pending_request;
    pending_request = LAUNCHER_REQUEST_NONE;
    return r;
}

bool launcher_take_favorites_changed (void) {
    bool changed = favorites_changed;
    favorites_changed = false;
    return changed;
}

static void rescan_folders (menu_t *menu, void *arg) {
    (void)arg;
    launcher_covers_reset();
    library_rescan(menu);
}

static component_context_menu_t start_menu = {
    .list = {
        { .text = favorite_label, .action = toggle_favorite },
        { .text = "Sort by", .submenu = &sort_menu },
        { .text = "Filter", .action = open_filter },
        { .text = "Surprise me", .action = surprise_me },
        { .text = "Theme", .submenu = &theme_menu },
        { .text = "Tile size", .submenu = &tile_size_menu },
        { .text = "Layout", .submenu = &layout_menu },
        { .text = "Game titles", .submenu = &titles_menu },
        { .text = "Rescan folders", .action = rescan_folders },
        { .text = "File browser", .action = open_file_browser },
        { .text = "Controller Pak manager", .action = open_screen, .arg = (void *)(MENU_MODE_CONTROLLER_PAKFS) },
        { .text = "Menu settings", .action = open_screen, .arg = (void *)(MENU_MODE_SETTINGS_EDITOR) },
        { .text = "Time (RTC) settings", .action = open_screen, .arg = (void *)(MENU_MODE_RTC) },
        { .text = "Menu information", .action = open_screen, .arg = (void *)(MENU_MODE_CREDITS) },
        { .text = "Flashcart information", .action = open_screen, .arg = (void *)(MENU_MODE_FLASHCART) },
        { .text = "N64 information", .action = open_screen, .arg = (void *)(MENU_MODE_SYSTEM_INFO) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

void launcher_menu_init (void) {
    for (int i = 0; i < 10; i++) {
        const theme_t *theme = theme_at(i);
        theme_menu.list[i].text = theme ? theme->name : "-";
    }
    ui_components_context_menu_init(&start_menu);
}

void launcher_menu_show (menu_t *menu, path_t *rom) {
    path_free(selected_rom);
    selected_rom = rom;
    snprintf(favorite_label, sizeof(favorite_label), "%s",
        favorite_index(menu, rom) >= 0 ? "Remove from favorites" : "Add to favorites");
    ui_components_context_menu_show(&start_menu);
    sound_play_effect(SFX_SETTING);
}

bool launcher_menu_process (menu_t *menu) {
    return ui_components_context_menu_process(menu, &start_menu);
}

void launcher_menu_draw (void) {
    ui_components_context_menu_draw(&start_menu);
}
