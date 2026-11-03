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
#include "utils/fs.h"
#include "constants.h"

#define HEADER_TAB_SPACING      (24)
#define HEADER_UNDERLINE_Y      (LAUNCHER_HEADER_Y + 21)
#define HEADER_NOTE_X           (150)   /* Header notes start right of the Home / Library tabs... */
#define HEADER_CLOCK_W          (90)    /* ...and stop short of the clock */
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

#define COVER_ALPHA_THRESHOLD   (128)   /* Scaled covers: filtered alpha below this is transparent */

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

static int slide_dx = 0;     /* Framebuffer offset while a screen transition slides the content in */

/** @brief Logical x to real framebuffer x. */
static int px (int x) {
    return (widescreen ? (x * 3 + 2) / 4 : x) + slide_dx;
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

void launcher_draw_background (void) {
    ui_components_background_draw();
}

struct launcher_paragraph_s {
    rdpq_paragraph_t *layout;
};

launcher_paragraph_t *launcher_paragraph_build (const char *text, int width) {
    size_t size = strlen(text) * 2 + 1;
    char *escaped = malloc(size);
    if (!escaped) {
        return NULL;
    }
    escape_text_codes(text, escaped, size);
    int nbytes = strlen(escaped);
    launcher_paragraph_t *p = malloc(sizeof(launcher_paragraph_t));
    p->layout = rdpq_paragraph_build(&(rdpq_textparms_t) {
        .style_id = STL_DEFAULT,
        .width = px(width),
        .wrap = WRAP_WORD,
    }, launcher_font(font_for(escaped, FNT_LAUNCHER)), escaped, &nbytes);
    free(escaped);
    return p;
}

int launcher_paragraph_height (launcher_paragraph_t *p) {
    return p ? (int)(p->layout->bbox.y1 - p->layout->bbox.y0) : 0;
}

void launcher_paragraph_draw (launcher_paragraph_t *p, int x, int y, int clip_y0, int clip_y1) {
    if (!p) {
        return;
    }
    rdpq_set_scissor(0, clip_y0, display_get_width(), clip_y1);
    // Layouts built without a height put the first baseline at 0; shift so the text's top sits at y.
    rdpq_paragraph_render(p->layout, px(x), y - p->layout->bbox.y0);
    rdpq_set_scissor(0, 0, display_get_width(), display_get_height());
}

void launcher_paragraph_free (launcher_paragraph_t *p) {
    if (p) {
        rdpq_paragraph_free(p->layout);
        free(p);
    }
}

void launcher_draw_new_games_notice (void) {
    int count = library_new_game_count();
    if (count <= 0) {
        return;
    }
    char text[128];
    snprintf(text, sizeof(text), "%d new game%s: run the cover tool", count, count == 1 ? "" : "s");
    launcher_small(LAUNCHER_X0 + HEADER_NOTE_X, LAUNCHER_HEADER_Y + 2, LAUNCHER_WIDTH - HEADER_NOTE_X - HEADER_CLOCK_W,
        ALIGN_RIGHT, STL_ACCENT, text);
}

#define LOADING_TITLE_Y     (186)
#define LOADING_BAR_Y       (228)
#define LOADING_BAR_W       (320)
#define LOADING_BAR_H       (8)
#define LOADING_STATUS_Y    (246)
#define LOADING_DETAILS_DY  (34)    /* Details line below the title */
#define LOADING_DETAILS_GAP (22)    /* Room the details line adds above the bar */

#define LAUNCH_COVER_MAX_W  (240)
#define LAUNCH_COVER_MAX_H  (180)
#define LAUNCH_COVER_ZOOM   (1.5f)  /* A tile-sized thumbnail (no Large cover in the pack) grows no more than this */
#define LAUNCH_LARGE_W      (158)   /* The largest cover-pack size (pc-tools/coverpack BOXES) */
#define LAUNCH_LARGE_H      (112)
#define LAUNCH_GROW_MS      (300)

static surface_t *focused_image = NULL;     /* Cover in the focused tile this frame (owned by the cover cache) */
static int focused_x, focused_y;
static surface_t launch_cover;              /* Our own copy: the cover cache is freed when the launcher is left */
static bool launch_cover_valid = false;
static bool launch_cover_large = false;     /* The copy is the Large pack cover: shown at its own size */
static int launch_from_x, launch_from_y, launch_from_w, launch_from_h;
static uint64_t launch_grow_start_ms = 0;

void launcher_note_focused_cover (surface_t *image, int x, int y, int w, int h) {
    if (!image) {
        return;
    }
    focused_image = image;
    focused_x = x + (w - image->width) / 2;
    focused_y = y + (h - image->height) / 2;
}

void launcher_clear_focused_cover (void) {
    focused_image = NULL;
}

void launcher_launch_cover_free (void) {
    if (launch_cover_valid) {
        surface_free(&launch_cover);
        launch_cover_valid = false;
    }
}

static void launch_cover_take (void) {
    launcher_launch_cover_free();
    if (!focused_image || !focused_image->buffer) {
        return;
    }
    launch_from_x = focused_x;
    launch_from_y = focused_y;
    launch_from_w = focused_image->width;
    launch_from_h = focused_image->height;
    launch_grow_start_ms = 0;   // The clock starts with the loading card's first frame.

    // The Large cover from the pack ends the animation sharp; a thumbnail would have to be enlarged.
    bool landscape = focused_image->width >= focused_image->height;
    surface_t *large = launcher_cover_larger(focused_image, landscape ? LAUNCH_LARGE_W : LAUNCH_LARGE_H,
                                             landscape ? LAUNCH_LARGE_H : LAUNCH_LARGE_W);
    if (large && large->width > focused_image->width) {
        launch_cover = *large;
        free(large);
        launch_cover_valid = true;
        launch_cover_large = true;
        return;
    }
    if (large) {
        surface_free(large);
        free(large);
    }
    if (launcher_cover_physical(focused_image)) {
        return;     // A 16:9 cover's pixels are squeezed: no copy, the loading card shows without it.
    }

    tex_format_t format = surface_get_format(focused_image);
    if (format != FMT_RGBA16 && format != FMT_RGBA32) {
        return;     // Palette formats would need their palette too; just skip the animation.
    }
    launch_cover = surface_alloc(format, focused_image->width, focused_image->height);
    if (!launch_cover.buffer) {
        return;
    }
    int row_bytes = TEX_FORMAT_PIX2BYTES(format, focused_image->width);
    for (int row = 0; row < focused_image->height; row++) {
        memcpy((uint8_t *)launch_cover.buffer + row * launch_cover.stride,
               (uint8_t *)focused_image->buffer + row * focused_image->stride, row_bytes);
    }
    launch_cover_valid = true;
    launch_cover_large = false;
}

void launcher_draw_loading (const char *title, const char *details, float progress, const char *status) {
    launcher_draw_background();

    bool has_details = details && details[0];
    int details_gap = has_details ? LOADING_DETAILS_GAP : 0;
    int title_y = LOADING_TITLE_Y - details_gap / 2;
    int bar_y = title_y + (LOADING_BAR_Y - LOADING_TITLE_Y) + details_gap;
    int status_y = title_y + (LOADING_STATUS_Y - LOADING_TITLE_Y) + details_gap;
    if (launch_cover_valid) {
        // The cover sits above the name, and the whole block stays centered on screen.
        float scale = fminf(launch_cover_large ? 1.0f : LAUNCH_COVER_ZOOM, fminf((float)LAUNCH_COVER_MAX_W / launch_cover.width,
                                                     (float)LAUNCH_COVER_MAX_H / launch_cover.height));
        int dw = (int)(launch_cover.width * scale);
        int dh = (int)(launch_cover.height * scale);
        int block_h = dh + (LOADING_STATUS_Y - LOADING_TITLE_Y) + 44 + details_gap;
        int dy = (DISPLAY_HEIGHT - block_h) / 2;
        int dx = LAUNCHER_CENTER_X - dw / 2;
        title_y = dy + dh + 26;
        bar_y = title_y + (LOADING_BAR_Y - LOADING_TITLE_Y) + details_gap;
        status_y = title_y + (LOADING_STATUS_Y - LOADING_TITLE_Y) + details_gap;

        // Grow from the tile it was launched from, easing out.
        uint64_t now = get_ticks_ms();
        if (launch_grow_start_ms == 0) {
            launch_grow_start_ms = now;
        }
        float t = (progress >= 1.0f) ? 1.0f : fminf(1.0f, (float)(now - launch_grow_start_ms) / LAUNCH_GROW_MS);
        float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        int x = launch_from_x + (int)((dx - launch_from_x) * e);
        int y = launch_from_y + (int)((dy - launch_from_y) * e);
        int w = launch_from_w + (int)((dw - launch_from_w) * e);
        int h = launch_from_h + (int)((dh - launch_from_h) * e);
        launcher_draw_cover_scaled(&launch_cover, x, y, w, h, theme_current()->bg);
    }

    if (title && title[0]) {
        launcher_title(LAUNCHER_X0, title_y, LAUNCHER_WIDTH, ALIGN_CENTER, STL_DEFAULT, title);
    }
    if (has_details) {
        launcher_small(LAUNCHER_X0, title_y + LOADING_DETAILS_DY, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, details);
    }

    if (progress < 0.0f) {
        progress = 0.0f;
    }
    if (progress > 1.0f) {
        progress = 1.0f;
    }
    int x0 = LAUNCHER_CENTER_X - LOADING_BAR_W / 2;
    int x1 = x0 + LOADING_BAR_W;
    launcher_fill_rounded(x0, bar_y, x1, bar_y + LOADING_BAR_H, LOADING_BAR_H / 2, theme_current()->panel);
    int filled = (int)(progress * LOADING_BAR_W);
    if (filled >= LOADING_BAR_H) {
        launcher_fill_rounded(x0, bar_y, x0 + filled, bar_y + LOADING_BAR_H, LOADING_BAR_H / 2, theme_current()->accent);
    }

    char text[64];
    if (!status) {
        snprintf(text, sizeof(text), "Loading %d%%", (int)(progress * 100.0f));
        status = text;
    }
    launcher_small(LAUNCHER_X0, status_y, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, status);
}

#define NOW_PLAYING_MS      (4000)

static uint32_t now_playing_generation = 0;
static uint64_t now_playing_until_ms = 0;

/**
 * @brief "Now playing: ..." in the header between the tabs and the clock, for a few seconds when a track starts.
 *
 * @return Whether it was drawn.
 */
static bool draw_now_playing (void) {
    uint32_t generation = sound_bgm_track_generation();
    if (generation != now_playing_generation) {
        now_playing_generation = generation;
        now_playing_until_ms = get_ticks_ms() + NOW_PLAYING_MS;
    }
    const char *name = sound_bgm_track_name();
    if (!name || !sound_bgm_is_playing() || get_ticks_ms() > now_playing_until_ms) {
        return false;
    }
    char text[128];
    snprintf(text, sizeof(text), "Now playing: %s", name);
    launcher_small(LAUNCHER_X0 + HEADER_NOTE_X, LAUNCHER_HEADER_Y + 2, LAUNCHER_WIDTH - HEADER_NOTE_X - HEADER_CLOCK_W,
        ALIGN_RIGHT, STL_GRAY, text);
    return true;
}

#define TRANSITION_MS       (180)
#define TRANSITION_SLIDE    (40)    /* Logical pixels the content travels while sliding in */
#define MENU_FADE_MS        (120)
#define MENU_BACKDROP_ALPHA (150)   /* How far the screen behind the Start menu fades back */

static void motion_frame(void);

static int transition_dir = 0;              /* +1 slides in from the right, -1 from the left, 0 only fades */
static bool transition_pending = false;
static uint64_t transition_start_ms = 0;
static int transition_y0 = 0;
static float transition_fade = 0.0f;        /* Content fade still to clear this frame (1 = background only) */

void launcher_transition (menu_mode_t from, menu_mode_t to) {
    if (from == to || (to != MENU_MODE_HOME && to != MENU_MODE_LIBRARY && to != MENU_MODE_ABOUT)) {
        return;
    }
    // The tabs read Home | Library: going right slides in from the right, going left from the left.
    // About fades in and out without a slide.
    if (from == MENU_MODE_ABOUT || to == MENU_MODE_ABOUT) {
        transition_dir = 0;
    } else {
        transition_dir = (to == MENU_MODE_LIBRARY) ? 1 : -1;
    }
    transition_pending = true;
    transition_start_ms = 0;    // The clock starts with the new screen's first frame.
}

void launcher_transition_content_begin (int y0) {
    motion_frame();
    transition_fade = 0.0f;
    slide_dx = 0;
    if (!transition_pending) {
        return;
    }
    static uint64_t last_frame_ms = 0;
    uint64_t now = get_ticks_ms();
    if (transition_start_ms == 0) {
        transition_start_ms = now;
    } else if (now - last_frame_ms > 50) {
        transition_start_ms += (now - last_frame_ms) - 16;     // A slow frame pauses the slide rather than skipping it.
    }
    last_frame_ms = now;
    float t = fminf(1.0f, (float)(now - transition_start_ms) / TRANSITION_MS);
    if (t >= 1.0f) {
        transition_pending = false;
        return;
    }
    float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    slide_dx = (int)lroundf(transition_dir * TRANSITION_SLIDE * (1.0f - e) * (widescreen ? WIDE_SQUEEZE : 1.0f));
    transition_fade = 1.0f - e;
    transition_y0 = y0;
}

/** @brief Cover a band of the screen with a colour at some opacity (fading things in or back). */
static void fade_band (int y0, int y1, color_t color, int alpha) {
    if (alpha <= 0) {
        return;
    }
    color.a = (alpha > 255) ? 255 : alpha;
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(color);
        rdpq_fill_rectangle(0, y0, display_get_width(), y1);
    rdpq_mode_pop();
}

/** @brief End a screen's content: undo the slide and fade the content in from the background. */
static void transition_content_end (void) {
    slide_dx = 0;
    if (transition_fade > 0.0f) {
        fade_band(transition_y0, LAUNCHER_FOOTER_Y, theme_current()->bg, (int)(transition_fade * 255.0f));
        transition_fade = 0.0f;
    }
}

static void draw_header_parts (menu_t *menu, launcher_tab_t active) {
    launcher_draw_background();

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
    // The music note takes the spot for its few seconds; the new games notice comes back after.
    if (!draw_now_playing()) {
        launcher_draw_new_games_notice();
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

void launcher_draw_header (menu_t *menu, launcher_tab_t active) {
    draw_header_parts(menu, active);
    launcher_transition_content_begin(HEADER_UNDERLINE_Y + 6);
}

void launcher_draw_footer (const launcher_hint_t *hints, int count) {
    transition_content_end();
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

/**
 * @brief Filled five-point star centered on logical (cx, cy), drawn as a triangle fan.
 */
static void fill_star (float cx, float cy, float radius, color_t color) {
    float squeeze = widescreen ? WIDE_SQUEEZE : 1.0f;
    float points[10][2];
    for (int i = 0; i < 10; i++) {
        float r = (i % 2 == 0) ? radius : radius * 0.45f;
        float a = -1.5707963f + i * 0.6283185f;
        points[i][0] = px((int)cx) + cosf(a) * r * squeeze;
        points[i][1] = cy + sinf(a) * r;
    }
    float center[2] = { px((int)cx), cy };
    flat_begin(color);
    for (int i = 0; i < 10; i++) {
        rdpq_triangle(&TRIFMT_FILL, center, points[i], points[(i + 1) % 10]);
    }
    flat_end();
}

void launcher_draw_favorite_star (int x, int y, int w) {
    float cx = x + w - 9;
    float cy = y + 9;
    fill_star(cx, cy, 8.0f, theme_current()->bg);           // backing, so it reads on any cover
    fill_star(cx, cy, 6.0f, theme_current()->accent);
}

/**
 * @brief Outline of a rounded rectangle in real pixels: the band between the outer shape and the
 * inner one (inset by thickness). Only the band is drawn, so whatever is inside shows through.
 */
static void outline_rounded_real (int x0, int y0, int x1, int y1, int radius, int thickness, float squeeze, color_t color) {
    int inner_radius = radius - thickness;
    int t = (int)(thickness * squeeze + 0.5f);
    flat_begin(color);
    for (int y = y0; y < y1; y++) {
        int from_top = y - y0;
        int from_bottom = y1 - 1 - y;
        int edge = from_top < from_bottom ? from_top : from_bottom;
        int outer = (edge < radius) ? (int)(corner_inset(radius, edge) * squeeze + 0.5f) : 0;
        if (edge < thickness) {
            rdpq_fill_rectangle(x0 + outer, y, x1 - outer, y + 1);
            continue;
        }
        int inner_edge = edge - thickness;
        int inner = (inner_radius > 0 && inner_edge < inner_radius) ? (int)(corner_inset(inner_radius, inner_edge) * squeeze + 0.5f) : 0;
        rdpq_fill_rectangle(x0 + outer, y, x0 + t + inner, y + 1);
        rdpq_fill_rectangle(x1 - t - inner, y, x1 - outer, y + 1);
    }
    flat_end();
}

#define MOTION_TAU_MS       (20.0f) /* Glides close ~63% of the remaining distance every this many ms */
#define MOTION_SNAP_PX      (1.5f)  /* Closer than this, a glide lands (no slow creep over the last pixels) */
#define FOCUS_LIFT          (3)     /* Pixels the selected tile rises */
#define FOCUS_LIFT_MS       (120)
#define FOCUS_SNAP_DISTANCE (220)   /* Farther jumps than this appear in place */

static bool ring_valid = false;
static float ring_x0, ring_y0, ring_x1, ring_y1, ring_radius;

int launcher_px (int x) {
    return px(x);
}

static float ease_factor = 0.4f;    /* This frame's share of the remaining distance (from its length) */

/** @brief Once per launcher frame: size this frame's glide step from how long the last frame took. */
static void motion_frame (void) {
    static uint64_t last_ms = 0;
    uint64_t now = get_ticks_ms();
    float dt = last_ms ? (float)(now - last_ms) : 16.0f;
    last_ms = now;
    if (dt < 1.0f) {
        dt = 1.0f;
    }
    if (dt > 100.0f) {
        dt = 100.0f;
    }
    ease_factor = 1.0f - expf(-dt / MOTION_TAU_MS);
}

float launcher_ease (float current, float target) {
    float next = current + (target - current) * ease_factor;
    if (fabsf(target - next) < MOTION_SNAP_PX) {
        next = target;
    }
    return next;
}

void launcher_focus_reset (void) {
    ring_valid = false;
}

void launcher_draw_focus_ring (int x0, int y0, int x1, int y1, int radius) {
    if (!ring_valid || fabsf(x0 - ring_x0) > FOCUS_SNAP_DISTANCE || fabsf(y0 - ring_y0) > FOCUS_SNAP_DISTANCE) {
        ring_x0 = x0;
        ring_y0 = y0;
        ring_x1 = x1;
        ring_y1 = y1;
        ring_radius = radius;
        ring_valid = true;
    } else {
        ring_x0 = launcher_ease(ring_x0, x0);
        ring_y0 = launcher_ease(ring_y0, y0);
        ring_x1 = launcher_ease(ring_x1, x1);
        ring_y1 = launcher_ease(ring_y1, y1);
        ring_radius = launcher_ease(ring_radius, radius);
    }
    launcher_draw_ring((int)lroundf(ring_x0), (int)lroundf(ring_y0), (int)lroundf(ring_x1), (int)lroundf(ring_y1),
        (int)lroundf(ring_radius));
}

void launcher_draw_focus_ring_tile (int x, int y, int w, int h) {
    launcher_draw_focus_ring(x, y, x + w, y + h, TILE_RADIUS);
}

#define FAST_SCROLL_GAP_MS  (250)   /* Two moves this close together count as fast scrolling... */
#define FAST_SCROLL_HOLD_MS (140)   /* ...until the selection has rested this long */

bool launcher_scrolling_fast (int key) {
    static int last_key = -1;
    static uint64_t last_change = 0;
    static uint64_t previous_change = 0;
    uint64_t now = get_ticks_ms();
    if (key != last_key) {
        last_key = key;
        previous_change = last_change;
        last_change = now;
    }
    return (now - last_change < FAST_SCROLL_HOLD_MS) && (last_change - previous_change < FAST_SCROLL_GAP_MS);
}

int launcher_focus_lift (int key) {
    static int last_key = -1;
    static uint64_t since = 0;
    uint64_t now = get_ticks_ms();
    if (key != last_key) {
        last_key = key;
        since = now;
    }
    float t = fminf(1.0f, (float)(now - since) / FOCUS_LIFT_MS);
    float e = 1.0f - (1.0f - t) * (1.0f - t);
    return (int)(FOCUS_LIFT * e + 0.5f);
}

void launcher_draw_ring (int x0, int y0, int x1, int y1, int radius) {
    outline_rounded_real(px(x0 - LAUNCHER_RING), y0 - LAUNCHER_RING, px(x1 + LAUNCHER_RING), y1 + LAUNCHER_RING,
        radius + LAUNCHER_RING, LAUNCHER_RING, widescreen ? WIDE_SQUEEZE : 1.0f, theme_current()->accent);
}

/**
 * @brief Draw a cover at logical (x, y) at its own size: copied 1:1 in 4:3, squeezed to 3/4 width in 16:9.
 */
static void blit_cover (surface_t *image, int x, int y) {
    // Covers keep a 1-bit alpha (e.g. the arch above a cartridge label); transparent pixels are skipped.
    surface_t *squeezed = widescreen ? launcher_cover_physical(image) : NULL;
    rdpq_mode_push();
    if (squeezed) {
        rdpq_set_mode_copy(true);
        rdpq_tex_blit(squeezed, px(x), y, NULL);
    } else if (widescreen) {
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_alphacompare(COVER_ALPHA_THRESHOLD);
        rdpq_tex_blit(image, px(x), y, &(rdpq_blitparms_t) { .scale_x = WIDE_SQUEEZE, .scale_y = 1.0f });
    } else {
        rdpq_set_mode_copy(true);
        rdpq_tex_blit(image, px(x), y, NULL);
    }
    rdpq_mode_pop();
}

void launcher_draw_tile (int x, int y, int w, int h, surface_t *image, bool pending, const char *title, bool selected, color_t behind) {
    color_t edge = behind;
    if (selected) {
        launcher_draw_ring(x, y, x + w, y + h, TILE_RADIUS);
        launcher_note_focused_cover(image, x, y, w, h);
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
        rdpq_mode_alphacompare(COVER_ALPHA_THRESHOLD);
        // A cover from a 16:9 pack is squeezed already: resize it as is.
        surface_t *squeezed = widescreen ? launcher_cover_physical(image) : NULL;
        rdpq_tex_blit(squeezed ? squeezed : image, px(dx), dy, &(rdpq_blitparms_t) {
            .scale_x = scale * ((widescreen && !squeezed) ? WIDE_SQUEEZE : 1.0f),
            .scale_y = scale,
        });
    rdpq_mode_pop();
    mask_corners(dx, dy, dx + dw, dy + dh, (int)(TILE_RADIUS * scale + 0.5f), behind);
}

static bool settings_save_pending = false;

void launcher_save_pending_settings (menu_t *menu) {
    if (settings_save_pending) {
        settings_save_pending = false;
        settings_save(&menu->settings);
    }
}

void launcher_leave (menu_t *menu, menu_mode_t next_mode) {
    // Category, tile size and layout changes made with the controller are saved here, in one write,
    // rather than on every button press (an SD card write is a visible stall).
    if (next_mode == MENU_MODE_LOAD_ROM && menu->load.quick_start) {
        // The write would also break up the music: it waits until the game has loaded.
        settings_save_pending = true;
        sound_bgm_begin_load();
        launch_cover_take();
    } else {
        settings_save_pending = false;
        settings_save(&menu->settings);
        launcher_launch_cover_free();
    }
    launcher_transition(menu->mode, next_mode);
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
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(10) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(11) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(12) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(13) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(14) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(15) },
        { .text = "", .action = set_theme, .arg = (void *)(uintptr_t)(16) },
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

static void set_cover_style (menu_t *menu, void *arg) {
    menu->settings.launcher_cartridge_art = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static int get_cover_style_selection (menu_t *menu) {
    return menu->settings.launcher_cartridge_art ? 1 : 0;
}

static component_context_menu_t cover_style_menu = {
    .get_default_selection = get_cover_style_selection,
    .list = {
        { .text = "Box", .action = set_cover_style, .arg = (void *)(uintptr_t)(false) },
        { .text = "Cartridge", .action = set_cover_style, .arg = (void *)(uintptr_t)(true) },
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
static char rescan_label[40] = "Rescan folders";
static char sort_label[40] = "Sort by";
static char theme_label[48] = "Theme";
static char layout_label[40] = "Layout";
static char tile_size_label[40] = "Tile size";
static char cover_style_label[40] = "Cover style";
static char titles_label[48] = "Game titles";
static char pause_label[24] = "Pause";
static char music_label[24] = "Music";
static char order_label[32] = "Order";
static char volume_label[32] = "Volume";
static char folder_label[64] = "Folder";
static int requested_category = 0;

static int favorite_index (menu_t *menu, path_t *rom) {
    for (int i = 0; rom && i < FAVORITES_COUNT; i++) {
        bookkeeping_item_t *item = &menu->bookkeeping.favorite_items[i];
        if (item->bookkeeping_type == BOOKKEEPING_TYPE_ROM && path_are_match(item->primary_path, rom)) {
            return i;
        }
    }
    return -1;
}

static uint32_t favorites_generation = 1;

void launcher_favorites_touched (void) {
    favorites_generation++;
}

uint32_t launcher_favorites_generation (void) {
    return favorites_generation;
}

bool launcher_is_favorite (menu_t *menu, path_t *rom) {
    return favorite_index(menu, rom) >= 0;
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
    favorites_generation++;
}

static uint64_t last_input_ms = 0;

void launcher_reset_idle (void) {
    last_input_ms = get_ticks_ms();
}

void launcher_track_idle (menu_t *menu) {
    uint64_t now = get_ticks_ms();
    bool input = menu->actions.go_up || menu->actions.go_down || menu->actions.go_left || menu->actions.go_right ||
        menu->actions.enter || menu->actions.back || menu->actions.options || menu->actions.settings ||
        menu->actions.lz_context || menu->actions.shoulder_l || menu->actions.shoulder_r || menu->actions.trigger_z;
    if (input || last_input_ms == 0) {
        last_input_ms = now;
        return;
    }
    int minutes = menu->settings.launcher_attract_minutes;
    if (minutes > 0 && now - last_input_ms > (uint64_t)minutes * 60 * 1000 && menu->next_mode == menu->mode) {
        launcher_reset_idle();
        menu->launcher_return_mode = menu->mode;
        menu->next_mode = MENU_MODE_ATTRACT;
    }
}

static path_t *about_rom = NULL;

path_t *launcher_about_rom (void) {
    return about_rom;
}

/** @brief Whether the highlighted entry is a ROM (64DD disks have their own load screen). */
static bool selected_is_rom (void) {
    return selected_rom && !file_has_extensions(path_get(selected_rom), (const char *[]) { "ndd", NULL });
}

static void about_game (menu_t *menu, void *arg) {
    (void)arg;
    if (!selected_is_rom()) {
        return;
    }
    path_free(about_rom);
    about_rom = path_clone(selected_rom);
    launcher_leave(menu, MENU_MODE_ABOUT);
}

static void game_options (menu_t *menu, void *arg) {
    (void)arg;
    if (!selected_is_rom()) {
        return;
    }
    // The ROM details screen with all its per-game settings, even when Quick Launch is on.
    path_free(menu->load.launcher_rom_path);
    menu->load.launcher_rom_path = path_clone(selected_rom);
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    menu->load.quick_start = false;
    launcher_leave(menu, MENU_MODE_LOAD_ROM);
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

static void hide_game (menu_t *menu, void *arg) {
    (void)arg;
    if (selected_rom) {
        library_set_hidden(menu, selected_rom, true);
    }
}

static void open_hidden (menu_t *menu, void *arg) {
    (void)arg;
    request(menu, LAUNCHER_REQUEST_HIDDEN);
}

static void open_folders (menu_t *menu, void *arg) {
    (void)arg;
    request(menu, LAUNCHER_REQUEST_FOLDERS);
}

static void open_filter (menu_t *menu, void *arg) {
    (void)arg;
    request(menu, LAUNCHER_REQUEST_FILTER);
}

int launcher_requested_category (void) {
    return requested_category;
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

/* Z: things to do with the highlighted game. */
static component_context_menu_t game_menu = {
    .list = {
        { .text = favorite_label, .action = toggle_favorite },
        { .text = "About this game", .action = about_game },
        { .text = "Game options", .action = game_options },
        { .text = "Hide game", .action = hide_game },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static component_context_menu_t browse_menu = {
    .list = {
        { .text = sort_label, .submenu = &sort_menu },
        { .text = "Filter", .action = open_filter },
        { .text = "Hidden games", .action = open_hidden },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static component_context_menu_t look_menu = {
    .list = {
        { .text = theme_label, .submenu = &theme_menu },
        { .text = layout_label, .submenu = &layout_menu },
        { .text = tile_size_label, .submenu = &tile_size_menu },
        { .text = cover_style_label, .submenu = &cover_style_menu },
        { .text = titles_label, .submenu = &titles_menu },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static void next_track (menu_t *menu, void *arg) {
    (void)menu;
    (void)arg;
    sound_bgm_next();
}

static void toggle_pause (menu_t *menu, void *arg) {
    (void)menu;
    (void)arg;
    sound_bgm_set_paused(!sound_bgm_is_paused());
}

static void set_music (menu_t *menu, void *arg) {
    menu->settings.bgm_enabled = (bool)(uintptr_t)(arg);
    sound_use_bgm(menu->settings.bgm_enabled);
    settings_save(&menu->settings);
}

static int get_music_selection (menu_t *menu) {
    return menu->settings.bgm_enabled ? 0 : 1;
}

static component_context_menu_t music_onoff_menu = {
    .get_default_selection = get_music_selection,
    .list = {
        { .text = "On", .action = set_music, .arg = (void *)(uintptr_t)(true) },
        { .text = "Off", .action = set_music, .arg = (void *)(uintptr_t)(false) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static void set_music_order (menu_t *menu, void *arg) {
    menu->settings.bgm_order = (int)(uintptr_t)(arg);
    sound_bgm_set_order(menu->settings.bgm_order);
    settings_save(&menu->settings);
}

static int get_music_order_selection (menu_t *menu) {
    return menu->settings.bgm_order;
}

static component_context_menu_t music_order_menu = {
    .get_default_selection = get_music_order_selection,
    .list = {
        { .text = "Shuffle", .action = set_music_order, .arg = (void *)(uintptr_t)(SOUND_BGM_ORDER_SHUFFLE) },
        { .text = "In order", .action = set_music_order, .arg = (void *)(uintptr_t)(SOUND_BGM_ORDER_IN_ORDER) },
        { .text = "Repeat one", .action = set_music_order, .arg = (void *)(uintptr_t)(SOUND_BGM_ORDER_REPEAT_ONE) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static void set_music_volume (menu_t *menu, void *arg) {
    menu->settings.bgm_volume = (int)(uintptr_t)(arg);
    sound_bgm_set_volume(menu->settings.bgm_volume);
    settings_save(&menu->settings);
}

static int get_music_volume_selection (menu_t *menu) {
    return menu->settings.bgm_volume;
}

static component_context_menu_t music_volume_menu = {
    .get_default_selection = get_music_volume_selection,
    .list = {
        { .text = "Low", .action = set_music_volume, .arg = (void *)(uintptr_t)(SOUND_BGM_VOLUME_LOW) },
        { .text = "Medium", .action = set_music_volume, .arg = (void *)(uintptr_t)(SOUND_BGM_VOLUME_MEDIUM) },
        { .text = "High", .action = set_music_volume, .arg = (void *)(uintptr_t)(SOUND_BGM_VOLUME_HIGH) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static component_context_menu_t music_menu = {
    .list = {
        { .text = "Next track", .action = next_track },
        { .text = pause_label, .action = toggle_pause },
        { .text = music_label, .submenu = &music_onoff_menu },
        { .text = order_label, .submenu = &music_order_menu },
        { .text = volume_label, .submenu = &music_volume_menu },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

#define FOLDER_MENU_ROWS    (16)

static void choose_folder (menu_t *menu, void *arg) {
    requested_category = (int)(uintptr_t)(arg);
    request(menu, LAUNCHER_REQUEST_CATEGORY);
}

static int get_folder_selection (menu_t *menu) {
    int index = library_category_find(menu->settings.launcher_category);
    return index >= 0 ? index : 0;
}

// Rows are filled with the folder names in prepare_menus; the row after the last folder ends the list.
static component_context_menu_t folder_menu = {
    .get_default_selection = get_folder_selection,
    .list = {
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(0) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(1) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(2) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(3) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(4) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(5) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(6) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(7) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(8) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(9) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(10) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(11) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(12) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(13) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(14) },
        { .text = "", .action = choose_folder, .arg = (void *)(uintptr_t)(15) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static component_context_menu_t files_menu = {
    .list = {
        { .text = rescan_label, .action = rescan_folders },
        { .text = "Folders", .action = open_folders },
        { .text = "File browser", .action = open_file_browser },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static component_context_menu_t system_menu = {
    .list = {
        { .text = "Menu settings", .action = open_screen, .arg = (void *)(MENU_MODE_SETTINGS_EDITOR) },
        { .text = "Controller Pak manager", .action = open_screen, .arg = (void *)(MENU_MODE_CONTROLLER_PAKFS) },
        { .text = "Time (RTC) settings", .action = open_screen, .arg = (void *)(MENU_MODE_RTC) },
        { .text = "Menu information", .action = open_screen, .arg = (void *)(MENU_MODE_CREDITS) },
        { .text = "Flashcart information", .action = open_screen, .arg = (void *)(MENU_MODE_FLASHCART) },
        { .text = "N64 information", .action = open_screen, .arg = (void *)(MENU_MODE_SYSTEM_INFO) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

/* Start: the whole launcher, grouped. */
static component_context_menu_t start_menu = {
    .list = {
        { .text = "Surprise me", .action = surprise_me },
        { .text = folder_label, .submenu = &folder_menu },
        { .text = "Browse", .submenu = &browse_menu },
        { .text = "Look", .submenu = &look_menu },
        { .text = "Music", .submenu = &music_menu },
        { .text = "Library", .submenu = &files_menu },
        { .text = "System", .submenu = &system_menu },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

void launcher_menu_init (void) {
    for (int i = 0; i < THEME_BUILTIN_COUNT; i++) {
        const theme_t *theme = theme_at(i);
        theme_menu.list[i].text = theme ? theme->name : "-";
    }
    ui_components_context_menu_init(&start_menu);
    ui_components_context_menu_init(&game_menu);
}

/** @brief The name of a choice menu's current choice, e.g. "Medium" from the tile size menu. */
static const char *current_choice (menu_t *menu, component_context_menu_t *choices) {
    int index = choices->get_default_selection(menu);
    for (int i = 0; choices->list[i].text; i++) {
        if (i == index) {
            return choices->list[i].text;
        }
    }
    return "-";
}

/** @brief Remember the highlighted game and refresh the labels that depend on it or on settings. */
static void prepare_menus (menu_t *menu, path_t *rom) {
    path_free(selected_rom);
    selected_rom = rom;
    snprintf(favorite_label, sizeof(favorite_label), "%s",
        favorite_index(menu, rom) >= 0 ? "Remove from favorites" : "Add to favorites");
    int new_games = library_new_game_count();
    if (new_games > 0) {
        snprintf(rescan_label, sizeof(rescan_label), "Rescan folders (%d new)", new_games);
    } else {
        snprintf(rescan_label, sizeof(rescan_label), "Rescan folders");
    }
    snprintf(sort_label, sizeof(sort_label), "Sort by: %s", current_choice(menu, &sort_menu));
    snprintf(theme_label, sizeof(theme_label), "Theme: %s", current_choice(menu, &theme_menu));
    snprintf(layout_label, sizeof(layout_label), "Layout: %s", current_choice(menu, &layout_menu));
    snprintf(tile_size_label, sizeof(tile_size_label), "Tile size: %s", current_choice(menu, &tile_size_menu));
    snprintf(cover_style_label, sizeof(cover_style_label), "Cover style: %s", current_choice(menu, &cover_style_menu));
    snprintf(titles_label, sizeof(titles_label), "Game titles: %s",
        menu->settings.launcher_titles_highlight_only ? "Highlighted" : "Always");
    snprintf(pause_label, sizeof(pause_label), "%s", sound_bgm_is_paused() ? "Resume" : "Pause");

    // Folders, in the order set in Library > Folders.
    library_init(menu);
    int folders = library_category_count();
    for (int i = 0; i < FOLDER_MENU_ROWS; i++) {
        folder_menu.list[i].text = (i < folders) ? library_category_name(i) : NULL;
    }
    if (folders == 0) {
        folder_menu.list[0].text = "No folders";     // An empty submenu can't be drawn.
    }
    const char *shown = library_category_name(get_folder_selection(menu));
    snprintf(folder_label, sizeof(folder_label), "Folder: %s", shown ? shown : "-");
    snprintf(music_label, sizeof(music_label), "Music: %s", current_choice(menu, &music_onoff_menu));
    snprintf(order_label, sizeof(order_label), "Order: %s", current_choice(menu, &music_order_menu));
    snprintf(volume_label, sizeof(volume_label), "Volume: %s", current_choice(menu, &music_volume_menu));
}

void launcher_menu_show (menu_t *menu, path_t *rom) {
    prepare_menus(menu, rom);
    ui_components_context_menu_show(&start_menu);
    sound_play_effect(SFX_POP);
}

void launcher_game_menu_show (menu_t *menu, path_t *rom) {
    if (!rom) {
        return;
    }
    prepare_menus(menu, rom);
    ui_components_context_menu_show(&game_menu);
    sound_play_effect(SFX_POP);
}

bool launcher_menu_process (menu_t *menu) {
    return ui_components_context_menu_process(menu, &start_menu) ||
        ui_components_context_menu_process(menu, &game_menu);
}

void launcher_menu_draw (void) {
    static bool was_open = false;
    static uint64_t opened_ms = 0;
    bool open = (start_menu.row_selected >= 0) || (game_menu.row_selected >= 0);
    if (open && !was_open) {
        opened_ms = get_ticks_ms();
    }
    was_open = open;
    if (open) {
        float t = fminf(1.0f, (float)(get_ticks_ms() - opened_ms) / MENU_FADE_MS);
        fade_band(0, display_get_height(), theme_current()->bg, (int)(MENU_BACKDROP_ALPHA * t));
    }
    ui_components_context_menu_draw(&start_menu);
    ui_components_context_menu_draw(&game_menu);
}
