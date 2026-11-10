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
#include "../perf_log.h"
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
#define WIDTH_CACHE_SIZE        (128)
#define FIT_SLACK               (32)    // physical pixels, wider than any glyph's advance

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
#define C_ARROWS_WIDTH          (19)    /* Room for the drawn arrows beside the "C" (and before the pill's end) */

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
        case FNT_LARGE: return FNT_LARGE_WIDE;
        case FNT_DISPLAY: return FNT_DISPLAY_WIDE;
        case FNT_DISPLAY_MID: return FNT_DISPLAY_MID_WIDE;
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

#define LAYOUT_CACHE_SIZE       (64)

/*
 * Text laid out once and drawn again each frame: building a layout (glyph lookup, kerning, wrapping)
 * cost most of a frame's drawing when every label was rebuilt every frame. Keyed by the text and
 * everything that shapes it; drawn at any position.
 */
typedef struct {
    char *text;
    uint32_t hash;
    uint8_t font;
    uint8_t style;
    uint8_t align;
    uint8_t valign;
    uint8_t wrap;               /* WRAP_ELLIPSES: one line, fitted as below; WRAP_WORD: as many lines as fit */
    int16_t real_w;
    int16_t h;
    int16_t dx;                 /* Offset from the box's left edge where the layout is drawn */
    int16_t advance;
    rdpq_paragraph_t *layout;
    uint32_t last_used;
} layout_entry_t;

static layout_entry_t layout_cache[LAYOUT_CACHE_SIZE];
static uint32_t layout_clock = 0;

void launcher_text_cache_clear (void) {
    for (int i = 0; i < LAYOUT_CACHE_SIZE; i++) {
        if (layout_cache[i].layout) {
            rdpq_paragraph_free(layout_cache[i].layout);
        }
        free(layout_cache[i].text);
        memset(&layout_cache[i], 0, sizeof(layout_cache[i]));
    }
}

static uint32_t text_hash (const char *text) {
    uint32_t hash = 2166136261u;    // FNV-1a
    for (const char *p = text; *p; p++) {
        hash = (hash ^ (uint8_t)*p) * 16777619u;
    }
    return hash;
}

/** @brief Lay text out in a box (real pixels wide, 0 for no limit) as text_box_font draws it. */
static void layout_build (layout_entry_t *e, const char *text) {
    int nbytes = (int)strlen(text);
    if (e->wrap == WRAP_WORD) {
        e->layout = rdpq_paragraph_build(
            &(rdpq_textparms_t) {
                .style_id = e->style,
                .width = e->real_w,
                .height = e->h,
                .align = e->align,
                .valign = e->valign,
                .wrap = WRAP_WORD,
            },
            e->font,
            text,
            &nbytes
        );
        e->dx = 0;
        e->advance = (int16_t)e->layout->advance_x;
        return;
    }
    if (e->real_w > 0) {
        // libdragon cuts a line once its last glyph plus that glyph's advance passes the box, so text
        // that fits (Dr. Mario 64) could still end in "...". Lay it out with room to spare and keep
        // that layout when it really fits.
        rdpq_paragraph_t *layout = rdpq_paragraph_build(
            &(rdpq_textparms_t) {
                .style_id = e->style,
                .width = e->real_w + FIT_SLACK,
                .height = e->h,
                .valign = e->valign,
                .wrap = WRAP_ELLIPSES,
            },
            e->font,
            text,
            &nbytes
        );
        float text_w = layout->bbox.x1 + 1 - layout->bbox.x0;
        if (layout->nlines == 1 && text_w <= e->real_w) {
            float dx = 0;
            if (e->align == ALIGN_CENTER) {
                dx = (e->real_w - text_w) * 0.5f;
            } else if (e->align == ALIGN_RIGHT) {
                dx = e->real_w - text_w;
            }
            if (e->align != ALIGN_LEFT) {
                dx -= layout->bbox.x0;
            }
            e->layout = layout;
            e->dx = (int16_t)dx;
            e->advance = (int16_t)layout->advance_x;
            return;
        }
        rdpq_paragraph_free(layout);
        nbytes = (int)strlen(text);
    }
    e->layout = rdpq_paragraph_build(
        &(rdpq_textparms_t) {
            .style_id = e->style,
            .width = e->real_w,
            .height = e->h,
            .align = e->align,
            .valign = e->valign,
            .wrap = (e->real_w > 0) ? WRAP_ELLIPSES : WRAP_NONE,
        },
        e->font,
        text,
        &nbytes
    );
    e->dx = 0;
    e->advance = (int16_t)e->layout->advance_x;
}

/**
 * @brief Draw already-escaped text inside a box given in logical coordinates.
 *
 * @return Logical width of the drawn text.
 */
static int text_box_wrap_untimed (uint8_t font, int x, int y, int w, int h, rdpq_align_t align, rdpq_valign_t valign, menu_font_style_t style, rdpq_textwrap_t wrap, const char *text);
static int last_layout_lines = 1;   /* Lines of the text last drawn */

static int text_box_font (uint8_t font, int x, int y, int w, int h, rdpq_align_t align, rdpq_valign_t valign, menu_font_style_t style, const char *text) {
    uint64_t started = get_ticks_us();
    perf_log_add(PERF_TEXT_DRAWS, 1);
    int width = text_box_wrap_untimed(font, x, y, w, h, align, valign, style, WRAP_ELLIPSES, text);
    uint32_t us = (uint32_t)(get_ticks_us() - started);
    perf_log_add(PERF_TEXT, us);
    perf_log_note_text(us, font, text);
    return width;
}

/** @brief As text_box_font, with text wrapped onto as many lines as the box holds. */
static int text_box_wrapped (uint8_t font, int x, int y, int w, int h, rdpq_align_t align, rdpq_valign_t valign, menu_font_style_t style, const char *text) {
    uint64_t started = get_ticks_us();
    perf_log_add(PERF_TEXT_DRAWS, 1);
    int width = text_box_wrap_untimed(font, x, y, w, h, align, valign, style, WRAP_WORD, text);
    uint32_t us = (uint32_t)(get_ticks_us() - started);
    perf_log_add(PERF_TEXT, us);
    perf_log_note_text(us, font, text);
    return width;
}

static int text_box_wrap_untimed (uint8_t font, int x, int y, int w, int h, rdpq_align_t align, rdpq_valign_t valign, menu_font_style_t style, rdpq_textwrap_t wrap, const char *text) {
    int real_w = (w > 0) ? px(x + w) - px(x) : 0;
    layout_entry_t key = {
        .font = launcher_font(font), .style = style, .align = align, .valign = valign, .wrap = wrap,
        .real_w = (int16_t)real_w, .h = (int16_t)h,
    };

    uint32_t hash = text_hash(text);
    layout_entry_t *e = NULL;
    layout_entry_t *spare = NULL;   // An empty entry, else the one unused longest
    for (int i = 0; i < LAYOUT_CACHE_SIZE; i++) {
        layout_entry_t *c = &layout_cache[i];
        if (!c->layout) {
            if (!spare || spare->layout) {
                spare = c;
            }
            continue;
        }
        if (c->hash == hash && c->font == key.font && c->style == key.style && c->align == key.align &&
            c->valign == key.valign && c->wrap == key.wrap && c->real_w == key.real_w && c->h == key.h &&
            strcmp(c->text, text) == 0) {
            e = c;
            break;
        }
        if (!spare || (spare->layout && c->last_used < spare->last_used)) {
            spare = c;
        }
    }
    if (!e) {
        e = spare;
        if (e->layout) {
            rdpq_paragraph_free(e->layout);
        }
        free(e->text);
        *e = key;
        e->text = strdup(text);
        e->hash = hash;
        layout_build(e, text);
        perf_log_add(PERF_LAYOUTS, 1);
    }
    e->last_used = ++layout_clock;
    last_layout_lines = (e->layout->nlines > 0) ? e->layout->nlines : 1;
    rdpq_paragraph_render(e->layout, px(x) + e->dx, y);
    return logical_width(e->advance);
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
    uint32_t hash;              /**< Of the text itself: a buffer reused for new text (the clock) isn't stale */
    uint8_t font;
    int width;
    int ink_x0;                 /**< Where the glyphs' ink starts, from the pen (real pixels) */
} width_entry_t;

static width_entry_t width_cache[WIDTH_CACHE_SIZE];
static int width_cache_next = 0;

/**
 * @brief Logical width of already-escaped text, remembered for strings that stay in memory
 * (hint labels, category names) so they aren't laid out again every frame.
 */
static width_entry_t *text_measure (uint8_t font, const char *text) {
    uint8_t real_font = launcher_font(font);
    size_t length = strlen(text);
    uint32_t hash = text_hash(text);
    for (int i = 0; i < WIDTH_CACHE_SIZE; i++) {
        width_entry_t *e = &width_cache[i];
        if (e->text == text && e->length == length && e->font == real_font && e->hash == hash) {
            return e;
        }
    }
    int nbytes = (int)length;
    rdpq_paragraph_t *layout = rdpq_paragraph_build(&(rdpq_textparms_t) { 0 }, real_font, text, &nbytes);
    width_entry_t *e = &width_cache[width_cache_next];
    width_cache_next = (width_cache_next + 1) % WIDTH_CACHE_SIZE;
    e->text = text;
    e->hash = hash;
    e->length = length;
    e->font = real_font;
    e->width = (int)(layout->bbox.x1 - layout->bbox.x0);
    e->ink_x0 = (int)floorf(layout->bbox.x0);
    rdpq_paragraph_free(layout);
    return e;
}

static int text_width_font (uint8_t font, const char *text) {
    return logical_width(text_measure(font, text)->width);
}

/**
 * @brief Draw a button's letters centered on their ink in a logical box: centering by the glyphs' advance leaves
 * letters such as L, R and Z visibly off to one side of the icon. text must stay in memory (it is measured once).
 */
static void text_centered_ink (uint8_t font, int x, int y, int w, int h, menu_font_style_t style, const char *text) {
    // Across, by the ink; down, the font's own centering in the box (the measured ink is relative to the baseline,
    // which a box places by the font's ascent: centering on it put the letters below the icons).
    // Worked out in real pixels: in 16:9, rounding the logical position on its own could leave a small letter a
    // pixel off-centre in a 12-pixel circle (the hint bar's A).
    width_entry_t *e = text_measure(font, text);
    int rx0 = px(x), rx1 = px(x + w);
    int pen = rx0 + (rx1 - rx0 - e->width + 1) / 2 - e->ink_x0;
    // Down, by the letters' own ink too: the glyphs' top and bottom from the font, the baseline put so their middle
    // is the box's middle (the font's line box, which a box centres, sits the capitals a pixel or two off).
    uint8_t real_font = launcher_font(font);
    const rdpq_font_t *f = rdpq_text_get_font(real_font);
    int top = 127, bottom = -127;
    for (const char *c = text; f && *c; c++) {
        rdpq_font_gmetrics_t m;
        if (*c != ' ' && rdpq_font_get_glyph_metrics(f, (unsigned char)*c, &m)) {
            top = (m.y0 < top) ? m.y0 : top;
            bottom = (m.y1 > bottom) ? m.y1 : bottom;
        }
    }
    if (top > bottom) {
        int left = widescreen ? ((pen - slide_dx) * 4) / 3 : pen - slide_dx;
        text_box_font(font, left, y, 0, h, ALIGN_LEFT, VALIGN_CENTER, style, text);
        return;
    }
    // The ink spans y0..y1 (exclusive), so its middle is (top + bottom - 1) / 2 from the baseline. A box with no
    // height puts the first baseline at its top, so it is drawn (and its layout kept) like any other text.
    float baseline = (float)y + (float)(h - 1) / 2.0f - (float)(top + bottom - 1) / 2.0f;
    int left = widescreen ? ((pen - slide_dx) * 4) / 3 : pen - slide_dx;
    while (px(left) < pen) {
        left++;
    }
    while (px(left) > pen) {
        left--;
    }
    text_box_font(font, left, (int)floorf(baseline + 0.5f), 0, 0, ALIGN_LEFT, VALIGN_TOP, style, text);
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

static bool large_text = false;

void launcher_set_large_text (bool enabled) {
    large_text = enabled;
}

int launcher_title_space (void) {
    return large_text ? 27 : 22;
}

int launcher_game_name (int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *name) {
    char escaped[256];
    escape_text_codes(name, escaped, sizeof(escaped));
    return text_box_font(font_for(escaped, large_text ? FNT_LARGE : FNT_LAUNCHER), x, y, width,
        large_text ? TEXT_HEIGHT + 4 : TEXT_HEIGHT, align, VALIGN_TOP, style, escaped);
}

int launcher_game_details (int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *text) {
    char escaped[256];
    escape_text_codes(text, escaped, sizeof(escaped));
    return text_box_font(font_for(escaped, large_text ? FNT_LAUNCHER : FNT_SMALL), x, y, width,
        large_text ? TEXT_HEIGHT : HINT_TEXT_HEIGHT, align, VALIGN_TOP, style, escaped);
}

void launcher_title_box (const char *name, int cx, int min_w, int *x, int *w) {
    int need = text_width_font(font_for(name, large_text ? FNT_LARGE : FNT_LAUNCHER), name) + 8;
    int width = (need > min_w) ? need : min_w;
    if (width > LAUNCHER_WIDTH) {
        width = LAUNCHER_WIDTH;
    }
    int left = cx - width / 2;
    if (left < LAUNCHER_X0) {
        left = LAUNCHER_X0;
    }
    if (left + width > LAUNCHER_X1) {
        left = LAUNCHER_X1 - width;
    }
    *x = left;
    *w = width;
}

int launcher_small (int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *text) {
    char escaped[256];
    escape_text_codes(text, escaped, sizeof(escaped));
    return text_box_font(font_for(escaped, FNT_SMALL), x, y, width, HINT_TEXT_HEIGHT, align, VALIGN_TOP, style, escaped);
}

int launcher_title_wrapped (int x, int y, int width, int max_lines, menu_font_style_t style, const char *name) {
    char escaped[256];
    escape_text_codes(name, escaped, sizeof(escaped));
    text_box_wrapped(font_for(escaped, FNT_TITLE), x, y, width, TITLE_HEIGHT * max_lines, ALIGN_LEFT, VALIGN_TOP, style, escaped);
    return last_layout_lines * TITLE_HEIGHT;
}

/* Separators between the fields of a details line (library.c details_text, and "Favorites  -  " on Home). */
static const char *const detail_separators[] = { "   \xC2\xB7   ", "  -  " };

#define BREAK_CACHE_SIZE    (8)
#define MAX_DETAIL_LINES    (4)

typedef struct {
    uint32_t hash;
    int width;
    uint8_t font;
    int lines;                              /**< 0: a field doesn't fit a line on its own (word wrap instead) */
    int16_t start[MAX_DETAIL_LINES];        /**< Byte offsets where each line starts */
    int16_t end[MAX_DETAIL_LINES];
} break_entry_t;

static break_entry_t break_cache[BREAK_CACHE_SIZE];
static int break_cache_next = 0;

/** @brief Real-pixel width of part of a text (laid out once per break computation, not every frame). */
static int measure_span (uint8_t real_font, const char *text, int length) {
    int nbytes = length;
    rdpq_paragraph_t *layout = rdpq_paragraph_build(&(rdpq_textparms_t) { 0 }, real_font, text, &nbytes);
    int w = (int)(layout->bbox.x1 - layout->bbox.x0);
    rdpq_paragraph_free(layout);
    return w;
}

/** @brief Where a details line breaks: between fields only, as many on a line as fit. */
static break_entry_t *detail_breaks (uint8_t font, const char *text, int width) {
    uint8_t real_font = launcher_font(font);
    uint32_t hash = text_hash(text);
    for (int i = 0; i < BREAK_CACHE_SIZE; i++) {
        break_entry_t *b = &break_cache[i];
        if (b->hash == hash && b->width == width && b->font == real_font) {
            return b;
        }
    }
    break_entry_t *b = &break_cache[break_cache_next];
    break_cache_next = (break_cache_next + 1) % BREAK_CACHE_SIZE;
    memset(b, 0, sizeof(*b));
    b->hash = hash;
    b->width = width;
    b->font = real_font;
    int real_w = px(width) - px(0);
    int len = (int)strlen(text);
    int line_start = 0, line_end = 0, pos = 0;
    while (pos <= len) {
        // The next field ends at the next separator (or the end of the text).
        int field_end = len, next = len + 1;
        for (size_t s = 0; s < sizeof(detail_separators) / sizeof(detail_separators[0]); s++) {
            const char *found = strstr(text + pos, detail_separators[s]);
            if (found && found - text < field_end) {
                field_end = (int)(found - text);
                next = field_end + (int)strlen(detail_separators[s]);
            }
        }
        if (measure_span(real_font, text + pos, field_end - pos) > real_w) {
            b->lines = 0;       // One field alone is too wide: word wrap the whole line.
            return b;
        }
        if (line_end > line_start && measure_span(real_font, text + line_start, field_end - line_start) > real_w) {
            // Doesn't fit after what's on this line: the line ends before this field's separator.
            if (b->lines == MAX_DETAIL_LINES) {
                break;
            }
            b->start[b->lines] = (int16_t)line_start;
            b->end[b->lines] = (int16_t)line_end;
            b->lines++;
            line_start = pos;
        }
        line_end = field_end;
        pos = next;
    }
    if (b->lines < MAX_DETAIL_LINES && line_end > line_start) {
        b->start[b->lines] = (int16_t)line_start;
        b->end[b->lines] = (int16_t)line_end;
        b->lines++;
    }
    return b;
}

int launcher_game_details_wrapped (int x, int y, int width, int max_lines, menu_font_style_t style, const char *text) {
    char escaped[256];
    escape_text_codes(text, escaped, sizeof(escaped));
    uint8_t font = font_for(escaped, large_text ? FNT_LAUNCHER : FNT_SMALL);
    int line_h = large_text ? TEXT_HEIGHT : HINT_TEXT_HEIGHT;
    // Whole fields stay together ("4 players", "HAL Laboratory"): lines break only between them.
    break_entry_t *b = detail_breaks(font, escaped, width);
    if (b->lines > 0) {
        int lines = (b->lines < max_lines) ? b->lines : max_lines;
        for (int i = 0; i < lines; i++) {
            char line[256];
            int n = b->end[i] - b->start[i];
            snprintf(line, sizeof(line), "%.*s", n, escaped + b->start[i]);
            // The last line allowed still ends in "..." if fields are left over.
            text_box_font(font, x, y + i * line_h, width, line_h, ALIGN_LEFT, VALIGN_TOP, style,
                (i == lines - 1 && lines < b->lines) ? escaped + b->start[i] : line);
        }
        return lines * line_h;
    }
    text_box_wrapped(font, x, y, width, line_h * max_lines, ALIGN_LEFT, VALIGN_TOP, style, escaped);
    return last_layout_lines * line_h;
}

int launcher_title (int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *name) {
    char escaped[256];
    escape_text_codes(name, escaped, sizeof(escaped));
    return text_box_font(font_for(escaped, FNT_TITLE), x, y, width, TITLE_HEIGHT, align, VALIGN_TOP, style, escaped);
}

static uint8_t sized_font (launcher_text_size_t size) {
    switch (size) {
        case LAUNCHER_SIZE_SMALL: return FNT_SMALL;
        case LAUNCHER_SIZE_LARGE: return FNT_LARGE;
        case LAUNCHER_SIZE_TITLE: return FNT_TITLE;
        case LAUNCHER_SIZE_DISPLAY: return fonts_display_loaded() ? FNT_DISPLAY : FNT_TITLE;
        case LAUNCHER_SIZE_DISPLAY_MID: return fonts_display_loaded() ? FNT_DISPLAY_MID : FNT_TITLE;
        default: return FNT_LAUNCHER;
    }
}

/** @brief The display font has Latin letters only: other text (Ō, Ž...) takes the title font. */
static uint8_t sized_font_for (launcher_text_size_t size, const char *text) {
    uint8_t font = sized_font(size);
    if (font == FNT_DISPLAY || font == FNT_DISPLAY_MID) {
        for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
            if (*p >= 0xC4) {
                return FNT_TITLE;
            }
        }
    }
    return font_for(text, font);
}

int launcher_text_sized (launcher_text_size_t size, int x, int y, int width, int h, rdpq_align_t align, menu_font_style_t style,
    const char *text) {
    char escaped[256];
    escape_text_codes(text, escaped, sizeof(escaped));
    return text_box_font(sized_font_for(size, escaped), x, y, width, h, align, VALIGN_CENTER, style, escaped);
}

int launcher_text_sized_width (launcher_text_size_t size, const char *text) {
    return text_width_font(sized_font_for(size, text), text);
}

/** @brief Horizontal inset of row i (from the top or bottom edge) of a rounded corner. */
#define INSET_MAX_RADIUS    (24)

/** @brief How far row i of a corner of this radius sits in from the straight edge (worked out once). */
static int corner_inset (int radius, int i) {
    static int8_t table[INSET_MAX_RADIUS + 1][INSET_MAX_RADIUS];
    static bool filled[INSET_MAX_RADIUS + 1];
    if (radius > INSET_MAX_RADIUS) {
        float dy = radius - i - 0.5f;
        return (int)(radius - sqrtf((float)(radius * radius) - dy * dy) + 0.5f);
    }
    if (!filled[radius]) {
        for (int r = 0; r < radius; r++) {
            float dy = radius - r - 0.5f;
            table[radius][r] = (int8_t)(radius - sqrtf((float)(radius * radius) - dy * dy) + 0.5f);
        }
        filled[radius] = true;
    }
    return table[radius][i];
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
    uint64_t perf_started = get_ticks_us();
    if (radius * 2 > y1 - y0) {
        radius = (y1 - y0) / 2;
    }
    if ((int)(radius * squeeze) * 2 > x1 - x0) {
        radius = (int)((x1 - x0) / (2 * squeeze));
    }
    flat_begin(color);
    rdpq_fill_rectangle(x0, y0 + radius, x1, y1 - radius);
    for (int i = 0; i < radius; ) {
        int inset = (int)(corner_inset(radius, i) * squeeze + 0.5f);
        int run = 1;
        while (i + run < radius && (int)(corner_inset(radius, i + run) * squeeze + 0.5f) == inset) {
            run++;
        }
        rdpq_fill_rectangle(x0 + inset, y0 + i, x1 - inset, y0 + i + run);
        rdpq_fill_rectangle(x0 + inset, y1 - i - run, x1 - inset, y1 - i);
        i += run;
    }
    flat_end();
    perf_log_add(PERF_SHAPES, (uint32_t)(get_ticks_us() - perf_started));
}

void launcher_fill_rounded (int x0, int y0, int x1, int y1, int radius, color_t color) {
    fill_rounded_real(px(x0), y0, px(x1), y1, radius, widescreen ? WIDE_SQUEEZE : 1.0f, color);
}

/*
 * Smooth curves: launcher_fill_rounded builds corners from rows of pixels with hard edges, which shows as steps on
 * small circles (button icons, pills), more so in 16:9 where the TV widens every pixel. These corners are quarters of
 * an anti-aliased circle texture instead, tinted to the color and blended over what's behind.
 */
#define SMOOTH_TEX          (32)

static surface_t smooth_circle;
static bool smooth_ready = false;

static void smooth_init (void) {
    if (smooth_ready) {
        return;
    }
    smooth_circle = surface_alloc(FMT_IA8, SMOOTH_TEX, SMOOTH_TEX);
    uint8_t *pixels = smooth_circle.buffer;
    float r = SMOOTH_TEX / 2.0f;
    for (int y = 0; y < SMOOTH_TEX; y++) {
        for (int x = 0; x < SMOOTH_TEX; x++) {
            int inside = 0;     // Coverage from 4x4 samples in the pixel.
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    float fx = x + (sx + 0.5f) / 4.0f - r, fy = y + (sy + 0.5f) / 4.0f - r;
                    inside += (fx * fx + fy * fy <= r * r);
                }
            }
            pixels[y * smooth_circle.stride + x] = 0xF0 | (uint8_t)((inside * 15 + 8) / 16);
        }
    }
    data_cache_hit_writeback(smooth_circle.buffer, smooth_circle.stride * SMOOTH_TEX);
    smooth_ready = true;
}

/** @brief A rounded rectangle (logical coordinates) whose corners are anti-aliased. */
static void fill_smooth_rounded (int x0, int y0, int x1, int y1, int radius, color_t color) {
    uint64_t perf_started = get_ticks_us();
    smooth_init();
    if (radius * 2 > y1 - y0) {
        radius = (y1 - y0) / 2;
    }
    int rx0 = px(x0), rx1 = px(x1);
    int ry = radius;
    int rx = widescreen ? (int)(radius * WIDE_SQUEEZE + 0.5f) : radius;
    if (rx * 2 > rx1 - rx0) {
        rx = (rx1 - rx0) / 2;
    }
    bool pill = (y1 - y0 == 2 * ry);           // Round ends, no straight sides (a circle when also as wide)
    flat_begin(color);
    if (!pill) {
        rdpq_fill_rectangle(rx0, y0 + ry, rx1, y1 - ry);
    }
    if (rx1 - rx0 > 2 * rx) {
        if (pill) {
            rdpq_fill_rectangle(rx0 + rx, y0, rx1 - rx, y1);
        } else {
            rdpq_fill_rectangle(rx0 + rx, y0, rx1 - rx, y0 + ry);
            rdpq_fill_rectangle(rx0 + rx, y1 - ry, rx1 - rx, y1);
        }
    }
    flat_end();
    if (rx <= 0 || ry <= 0) {
        return;
    }
    // The circle (1 KB) is loaded once for the shape and its parts drawn from it: a load per corner cost ~2 ms a
    // frame across the hints and pills (performance log).
    rdpq_mode_push();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER1((0, 0, 0, PRIM), (TEX0, 0, PRIM, 0)));
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_set_prim_color(color);
    rdpq_tex_upload(TILE0, &smooth_circle, NULL);
    int half = SMOOTH_TEX / 2, full = SMOOTH_TEX;
    if (pill) {
        // Left and right halves of the circle (one piece each; a circle is the two side by side).
        rdpq_texture_rectangle_scaled(TILE0, rx0, y0, rx0 + rx, y1, 0, 0, half, full);
        rdpq_texture_rectangle_scaled(TILE0, rx1 - rx, y0, rx1, y1, half, 0, full, full);
    } else {
        rdpq_texture_rectangle_scaled(TILE0, rx0, y0, rx0 + rx, y0 + ry, 0, 0, half, half);
        rdpq_texture_rectangle_scaled(TILE0, rx1 - rx, y0, rx1, y0 + ry, half, 0, full, half);
        rdpq_texture_rectangle_scaled(TILE0, rx0, y1 - ry, rx0 + rx, y1, 0, half, half, full);
        rdpq_texture_rectangle_scaled(TILE0, rx1 - rx, y1 - ry, rx1, y1, half, half, full, full);
    }
    rdpq_mode_pop();
    perf_log_add(PERF_SHAPES, (uint32_t)(get_ticks_us() - perf_started));
}

void launcher_fill_rounded_px (int x0, int y0, int x1, int y1, int radius, color_t color) {
    fill_rounded_real(x0, y0, x1, y1, radius, 1.0f, color);
}

/**
 * @brief Real pixel width of a cover w logical pixels wide: in 16:9, the width the 16:9 packs squeeze
 * it to (pc-tools/coverpack/coverimg.c: an even width, 3/4 of it rounded to an even number).
 */
static int real_cover_width (int w) {
    return widescreen ? 2 * (int)((w & ~1) * 0.375f + 0.5f) : w;
}

/**
 * @brief Round off the corners of something already drawn by painting them in the color behind it.
 * Coordinates are logical.
 */
static void mask_corners (int x0, int y0, int x1, int y1, int radius, color_t behind) {
    uint64_t perf_started = get_ticks_us();
    float squeeze = widescreen ? WIDE_SQUEEZE : 1.0f;
    int rx0 = px(x0);
    int rx1 = rx0 + real_cover_width(x1 - x0);
    flat_begin(behind);
    // Rows with the same inset are painted together (four rectangles for each run of rows).
    for (int i = 0; i < radius; ) {
        int inset = (int)(corner_inset(radius, i) * squeeze + 0.5f);
        int run = 1;
        while (i + run < radius && (int)(corner_inset(radius, i + run) * squeeze + 0.5f) == inset) {
            run++;
        }
        if (inset > 0) {
            rdpq_fill_rectangle(rx0, y0 + i, rx0 + inset, y0 + i + run);
            rdpq_fill_rectangle(rx1 - inset, y0 + i, rx1, y0 + i + run);
            rdpq_fill_rectangle(rx0, y1 - i - run, rx0 + inset, y1 - i);
            rdpq_fill_rectangle(rx1 - inset, y1 - i - run, rx1, y1 - i);
        }
        i += run;
    }
    flat_end();
    perf_log_add(PERF_SHAPES, (uint32_t)(get_ticks_us() - perf_started));
}

void launcher_draw_page_title (const char *title) {
    int w = launcher_text(LAUNCHER_X0, LAUNCHER_HEADER_Y, 0, ALIGN_LEFT, STL_DEFAULT, "%s", title);
    launcher_fill_rounded(LAUNCHER_X0, HEADER_UNDERLINE_Y, LAUNCHER_X0 + w, HEADER_UNDERLINE_Y + 3, 1, theme_current()->accent2);
}

static bool background_override = false;
static color_t background_override_color;

void launcher_set_background_color (color_t color) {
    background_override = true;
    background_override_color = color;
}

static bool band_set = false;
static int band_x_top, band_x_bottom, band_y1, band_edge_w;
static color_t band_color, band_edge;

void launcher_set_background_band (int x_top, int x_bottom, int y1, color_t color, color_t edge, int edge_w) {
    band_set = true;
    band_x_top = x_top;
    band_x_bottom = x_bottom;
    band_y1 = y1;
    band_color = color;
    band_edge = edge;
    band_edge_w = edge_w;
}

void launcher_draw_background (void) {
    uint64_t started = get_ticks_us();
    if (background_override) {
        background_override = false;    // Only for the frame it was set in.
        ui_components_box_draw(0, 0, display_get_width(), display_get_height(), background_override_color);
    } else {
        ui_components_background_draw();
    }
    if (band_set) {
        band_set = false;
        launcher_fill_slanted(band_x_top + band_edge_w, band_x_bottom + band_edge_w, 0, band_y1, band_edge);
        launcher_fill_slanted(band_x_top, band_x_bottom, 0, band_y1, band_color);
    }
    perf_log_add(PERF_BACKGROUND, (uint32_t)(get_ticks_us() - started));
}

#define COLOR_SAMPLES       (12)    /* Across and down: 144 pixels read, a few tens of microseconds */

bool launcher_cover_color (const surface_t *image, color_t *out) {
    if (!image || !image->buffer || surface_get_format(image) != FMT_RGBA16 || image->width < 4 || image->height < 4) {
        return false;
    }
    uint32_t r = 0, g = 0, b = 0, weight = 0, ar = 0, ag = 0, ab = 0, count = 0;
    for (int sy = 0; sy < COLOR_SAMPLES; sy++) {
        const uint16_t *line = (const uint16_t *)((const uint8_t *)image->buffer +
            (image->height * (2 * sy + 1) / (2 * COLOR_SAMPLES)) * image->stride);
        for (int sx = 0; sx < COLOR_SAMPLES; sx++) {
            uint16_t p = line[image->width * (2 * sx + 1) / (2 * COLOR_SAMPLES)];
            if (!(p & 1)) {
                continue;   // See-through (a cartridge's outline)
            }
            int pr = ((p >> 11) & 31) << 3, pg = ((p >> 6) & 31) << 3, pb = ((p >> 1) & 31) << 3;
            int hi = pr > pg ? (pr > pb ? pr : pb) : (pg > pb ? pg : pb);
            int lo = pr < pg ? (pr < pb ? pr : pb) : (pg < pb ? pg : pb);
            ar += pr; ag += pg; ab += pb; count++;
            int sat = hi - lo;
            if (hi < 48 || sat < 48) {
                continue;   // Near black, white or grey: not the colour that names the game
            }
            r += pr * sat; g += pg * sat; b += pb * sat; weight += sat;
        }
    }
    if (weight > 0) {
        *out = RGBA32(r / weight, g / weight, b / weight, 0xFF);
    } else if (count > 0) {
        *out = RGBA32(ar / count, ag / count, ab / count, 0xFF);
    } else {
        return false;
    }
    return true;
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

static void draw_new_games_notice_at(int x, int y, int width, rdpq_align_t align);

void launcher_draw_new_games_notice (void) {
    draw_new_games_notice_at(LAUNCHER_X0 + HEADER_NOTE_X, LAUNCHER_HEADER_Y + 2,
        LAUNCHER_WIDTH - HEADER_NOTE_X - HEADER_CLOCK_W, ALIGN_RIGHT);
}

#define LOADING_TITLE_Y     (186)
#define LOADING_BAR_Y       (228)
#define LOADING_BAR_W       (320)
#define LOADING_BAR_H       (8)
#define LOADING_STATUS_Y    (246)
#define LOADING_DETAILS_DY  (34)    /* Details line below the title */
#define LOADING_DETAILS_GAP (22)    /* Room the details line adds above the bar */

#define LAUNCH_COVER_MAX_W  (LAUNCHER_HIRES_W)
#define LAUNCH_COVER_MAX_H  (LAUNCHER_HIRES_H)
#define LAUNCH_COVER_ZOOM   (1.5f)  /* A tile-sized thumbnail (no Large cover in the pack) grows no more than this */
#define LAUNCH_LARGE_ZOOM   (1.15f) /* The Large cover grows a little too, so it visibly grows even from a large tile */
#define LAUNCH_LARGE_W      (LAUNCHER_SHOWCASE_W)   /* The largest cover-pack size (pc-tools/coverpack UP_BOXES) */
#define LAUNCH_LARGE_H      (LAUNCHER_SHOWCASE_H)
#define LAUNCH_GROW_MS      (300)

static surface_t *focused_image = NULL;     /* Cover in the focused tile this frame (owned by the cover cache) */
static int focused_x, focused_y, focused_w, focused_h;
static surface_t launch_cover;              /* Our own copy: the cover cache is freed when the launcher is left */
static bool launch_cover_valid = false;
static bool launch_cover_large = false;     /* The copy is the Large pack cover: shown at its own size */
static int launch_cover_logical_w = 0;      /* 16:9: the copy holds squeezed pixels standing for this width; else 0 */
static int launch_from_x, launch_from_y, launch_from_w, launch_from_h;
static uint64_t launch_grow_start_ms = 0;

void launcher_note_focused_cover_zoomed (surface_t *image, int x, int y, int w, int h, float zoom) {
    if (!image) {
        return;
    }
    focused_image = image;
    focused_w = (int)(image->width * zoom);
    focused_h = (int)(image->height * zoom);
    focused_x = x + (w - focused_w) / 2;
    focused_y = y + (h - focused_h) / 2;
}

void launcher_note_focused_cover (surface_t *image, int x, int y, int w, int h) {
    launcher_note_focused_cover_zoomed(image, x, y, w, h, 1.0f);
}

surface_t *launcher_focused_cover (void) {
    return focused_image;
}

void launcher_clear_focused_cover (void) {
    focused_image = NULL;
}

void launcher_launch_cover_free (void) {
    if (launch_cover_valid) {
        surface_free(&launch_cover);
        launch_cover_valid = false;
        launch_cover_logical_w = 0;
    }
}

static void launch_cover_take (void) {
    launcher_launch_cover_free();
    if (!focused_image || !focused_image->buffer) {
        return;
    }
    launch_from_x = focused_x;
    launch_from_y = focused_y;
    launch_from_w = focused_w;
    launch_from_h = focused_h;
    launch_grow_start_ms = 0;   // The clock starts with the loading card's first frame.

    // Every launch shows the hi-res cover, grown to Spotlight's size: its squeezed pixels, from memory when held
    // (Spotlight's own, or read ahead while the controller rested) or else read from the pack, are shown 1:1 once
    // the cover reaches that size. Widening them and squeezing them again for the screen had softened the cover.
    int logical_w = 0;
    surface_t *hires = launcher_cover_launch_copy(focused_image, &logical_w);
    if (hires && logical_w > 0) {
        launch_cover = *hires;
        free(hires);
        launch_cover_valid = true;
        launch_cover_large = true;
        launch_cover_logical_w = logical_w;
        return;
    }
    if (hires) {
        surface_free(hires);
        free(hires);
    }

    // The Large cover from the pack ends the animation sharp; a thumbnail would have to be enlarged.
    // A cover already shown at that size (carousel, shelf) is read again too: in 16:9 the one on
    // screen is squeezed and can't be copied.
    bool landscape = focused_image->width >= focused_image->height;
    // A cover shown from the hi-res pack (Home's large covers) is larger than the Large one: read it from there.
    surface_t *large = (focused_image->width > LAUNCH_LARGE_W + 2 || focused_image->height > LAUNCH_LARGE_H + 2) ?
        launcher_cover_larger(focused_image, LAUNCHER_HIRES_W, LAUNCHER_HIRES_H) : NULL;
    if (!large) {
        large = launcher_cover_larger(focused_image, landscape ? LAUNCH_LARGE_W : LAUNCH_LARGE_H,
                                      landscape ? LAUNCH_LARGE_H : LAUNCH_LARGE_W);
    }
    // Within a couple of pixels: covers are trimmed to even sizes, so a 16:9 cover read again at the size it is
    // shown at (Home's Spotlight and Minimal, the carousel, the shelf) can come back a pixel narrower.
    if (large && large->width + 2 >= focused_image->width) {
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

static void blit_filtered(surface_t *source, int rx, int y, int real_w, int h);

/** @brief The loading card's squeezed cover at logical (x, y, w, h): copied 1:1 at its own size, else resized. */
static void draw_launch_squeezed (int x, int y, int w, int h) {
    rdpq_mode_push();
    if (w == launch_cover_logical_w && h == launch_cover.height) {
        rdpq_set_mode_copy(true);
        rdpq_tex_blit(&launch_cover, px(x), y, NULL);
    } else {
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_alphacompare(COVER_ALPHA_THRESHOLD);
        blit_filtered(&launch_cover, px(x), y, real_cover_width(w), h);
    }
    rdpq_mode_pop();
    float scale = (float)h / launch_cover.height;
    mask_corners(x, y, x + w, y + h, (int)(TILE_RADIUS * scale + 0.5f), theme_current()->bg);
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
        int cover_w = launch_cover_logical_w ? launch_cover_logical_w : launch_cover.width;
        float scale = fminf(launch_cover_large ? LAUNCH_LARGE_ZOOM : LAUNCH_COVER_ZOOM, fminf((float)LAUNCH_COVER_MAX_W / cover_w,
                                                     (float)LAUNCH_COVER_MAX_H / launch_cover.height));
        int dw = (int)(cover_w * scale);
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
        if (launch_cover_logical_w) {
            draw_launch_squeezed(x, y, w, h);
        } else {
            launcher_draw_cover_scaled(&launch_cover, x, y, w, h, theme_current()->bg);
        }
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
static bool draw_now_playing_at (int x, int y, int width, rdpq_align_t align);

static bool draw_now_playing (void) {
    return draw_now_playing_at(LAUNCHER_X0 + HEADER_NOTE_X, LAUNCHER_HEADER_Y + 2,
        LAUNCHER_WIDTH - HEADER_NOTE_X - HEADER_CLOCK_W, ALIGN_RIGHT);
}

static bool draw_now_playing_at (int x, int y, int width, rdpq_align_t align) {
    uint32_t generation = sound_bgm_track_generation();
    if (generation != now_playing_generation) {
        now_playing_generation = generation;
        now_playing_until_ms = get_ticks_ms() + NOW_PLAYING_MS;
    }
    const char *name = sound_bgm_track_name();
    if (!name || !sound_bgm_is_playing() || get_ticks_ms() > now_playing_until_ms) {
        return false;
    }
    static char text[128];      // Kept in memory: its width is remembered
    snprintf(text, sizeof(text), "Now playing: %s", name);
    launcher_small(x, y, width, align, STL_GRAY, text);
    return true;
}

static void draw_new_games_notice_at (int x, int y, int width, rdpq_align_t align) {
    int count = library_new_game_count();
    if (count <= 0) {
        return;
    }
    char text[128];
    snprintf(text, sizeof(text), "%d new game%s: run the cover tool", count, count == 1 ? "" : "s");
    launcher_small(x, y, width, align, STL_ACCENT, text);
}

void launcher_draw_now_playing (int x, int y, int width, rdpq_align_t align) {
    draw_now_playing_at(x, y, width, align);
}

void launcher_draw_notes (int x, int y, int width, rdpq_align_t align) {
    // The music note takes the spot for its few seconds; the new games notice comes back after.
    if (!draw_now_playing_at(x, y, width, align)) {
        draw_new_games_notice_at(x, y, width, align);
    }
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
static int transition_y1 = LAUNCHER_FOOTER_Y;  /* The fade's band ends here (the whole screen for Home's styles) */
static float transition_fade = 0.0f;        /* Content fade still to clear this frame (1 = background only) */

void launcher_transition (menu_mode_t from, menu_mode_t to) {
    if (from == to || (to != MENU_MODE_HOME && to != MENU_MODE_LIBRARY && to != MENU_MODE_ABOUT &&
        to != MENU_MODE_SETTINGS_EDITOR)) {
        return;
    }
    // The tabs read Home | Library: going right slides in from the right, going left from the left.
    // About and Settings fade in and out without a slide.
    if (from == MENU_MODE_ABOUT || to == MENU_MODE_ABOUT ||
        from == MENU_MODE_SETTINGS_EDITOR || to == MENU_MODE_SETTINGS_EDITOR) {
        transition_dir = 0;
    } else {
        transition_dir = (to == MENU_MODE_LIBRARY) ? 1 : -1;
    }
    transition_pending = true;
    transition_start_ms = 0;    // The clock starts with the new screen's first frame.
}

bool launcher_transition_running (void) {
    return transition_pending;
}

void launcher_transition_fade (void) {
    transition_dir = 0;
    transition_pending = true;
    transition_start_ms = 0;
}

void launcher_transition_content_begin (int y0) {
    motion_frame();
    transition_fade = 0.0f;
    slide_dx = 0;
    if (!transition_pending) {
        return;
    }
    static uint64_t last_frame_ms = 0;
    static uint64_t began_ms = 0;
    static int frames = 0;
    static uint64_t slowest_ms = 0;
    uint64_t now = get_ticks_ms();
    if (transition_start_ms == 0) {
        transition_start_ms = now;
        began_ms = now;
        frames = 0;
        slowest_ms = 0;
    } else {
        slowest_ms = (now - last_frame_ms > slowest_ms) ? now - last_frame_ms : slowest_ms;
        if (now - last_frame_ms > 50) {
            transition_start_ms += (now - last_frame_ms) - 16;     // A slow frame pauses the slide rather than skipping it.
        }
    }
    last_frame_ms = now;
    frames++;
    float t = fminf(1.0f, (float)(now - transition_start_ms) / TRANSITION_MS);
    if (t >= 1.0f) {
        transition_pending = false;
        perf_log_event("screen transition (%s): %d frames over %lu ms, slowest frame %lu ms",
            transition_dir == 0 ? "fade" : "slide", frames, (unsigned long)(now - began_ms), (unsigned long)slowest_ms);
        return;
    }
    float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    slide_dx = (int)lroundf(transition_dir * TRANSITION_SLIDE * (1.0f - e) * (widescreen ? WIDE_SQUEEZE : 1.0f));
    transition_fade = 1.0f - e;
    transition_y0 = y0;
}

/** @brief Cover a band of the screen with a colour at some opacity (fading things in or back). */
static void fade_band (int y0, int y1, color_t color, int alpha);

void launcher_draw_screen_fade (int alpha) {
    if (alpha >= 255) {
        // Opaque: a plain fill, several times quicker for the RDP than blending the whole screen (which
        // cost ~10 ms a frame and slowed the hidden startup Home).
        rdpq_mode_push();
            rdpq_set_mode_fill(RGBA32(0x00, 0x00, 0x00, 0xFF));
            rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());
        rdpq_mode_pop();
        return;
    }
    fade_band(0, display_get_height(), RGBA32(0x00, 0x00, 0x00, 0xFF), alpha);
}

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
static bool band_set;
static bool header_on_feature = false;
static float tab_line_x = -1.0f, tab_line_w = 0.0f;
static menu_t *header_menu = NULL;          /* This frame's tabs, drawn again over a full-screen fade */
static launcher_tab_t header_active = LAUNCHER_TAB_HOME;
static bool header_redraw = false;

static void draw_tabs(menu_t *menu, launcher_tab_t active, bool ease);

static void transition_content_end (void) {
    slide_dx = 0;
    if (transition_fade > 0.0f) {
        fade_band(transition_y0, transition_y1, theme_current()->bg, (int)(transition_fade * 255.0f));
        transition_fade = 0.0f;
        if (header_redraw && header_menu) {
            draw_tabs(header_menu, header_active, false);
        }
    }
    header_redraw = false;
    header_on_feature = false;
}

static launcher_header_style_t header_style = LAUNCHER_HEADER_TABS;
static bool footer_line = true;

void launcher_set_header_style (launcher_header_style_t style) {
    header_style = style;
}

void launcher_hide_footer_line (void) {
    footer_line = false;
}

void launcher_clock_text (menu_t *menu, char *buffer, size_t size) {
    buffer[0] = '\0';
    if (menu->current_time >= 0) {
        char clock[16];
        struct tm *t = localtime(&menu->current_time);
        // 13:45 or 1:45 PM (Settings > System > Clock).
        strftime(clock, sizeof(clock), menu->settings.launcher_clock_24h ? "%H:%M" : "%I:%M %p", t);
        snprintf(buffer, size, "%s", (clock[0] == '0' && !menu->settings.launcher_clock_24h) ? clock + 1 : clock);
    }
}

void launcher_draw_arrow (int cx, int cy, int size, bool right, color_t color) {
    float half = size / 2.0f;
    float depth = size * 0.55f;     /* Across: a little over half its height */
    float back = right ? cx - depth / 2.0f : cx + depth / 2.0f;
    float tip = right ? cx + depth / 2.0f : cx - depth / 2.0f;
    // In real pixels: px() squeezes x in 16:9, so the arrow keeps the same look as the squeezed text.
    float rb = widescreen ? back * 0.75f + slide_dx : back + slide_dx;
    float rt = widescreen ? tip * 0.75f + slide_dx : tip + slide_dx;
    float a[] = { rb, cy - half }, b[] = { rt, (float)cy }, c[] = { rb, cy + half };
    flat_begin(color);
    rdpq_triangle(&TRIFMT_FILL, a, b, c);
    flat_end();
}

void launcher_fill_slanted (int x_top, int x_bottom, int y0, int y1, color_t color) {
    int straight = (x_top < x_bottom) ? x_top : x_bottom;
    // The large straight part in fill mode, which the RDP draws about four times faster (Spotlight's panel is
    // half the screen; in the performance log the blended fill cost ~2 ms a frame).
    rdpq_mode_push();
    rdpq_set_mode_fill(color);
    rdpq_fill_rectangle(0, y0, px(straight), y1);
    rdpq_mode_pop();
    flat_begin(color);
    // The slanted part: a triangle between the two right ends.
    float a[] = { px(straight), y0 }, b[] = { px(x_top), y0 }, c[] = { px(x_bottom), y1 }, d[] = { px(straight), y1 };
    if (x_top > x_bottom) {
        rdpq_triangle(&TRIFMT_FILL, a, b, d);
    } else if (x_bottom > x_top) {
        rdpq_triangle(&TRIFMT_FILL, a, c, d);
    }
    flat_end();
}

#define CORNER_GAP          (22)    /* Between the corner header's words, with a dot in the middle */

/** @brief Home  ·  Library  ·  clock, right-aligned at the top. */
static void draw_header_corner (menu_t *menu, launcher_tab_t active) {
    char clock[16];
    launcher_clock_text(menu, clock, sizeof(clock));
    // Separate words and drawn dots: a separator string's spaces aren't measured, which overlapped the words.
    const char *parts[3] = { "Home", "Library", clock };
    int count = clock[0] ? 3 : 2;
    int widths[3], total = 0;
    for (int i = 0; i < count; i++) {
        widths[i] = text_width(parts[i]) + 2;
        total += widths[i] + (i > 0 ? CORNER_GAP : 0);
    }
    int x = LAUNCHER_X1 - total;
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            int cx = x + CORNER_GAP / 2;
            launcher_fill_rounded(cx - 2, LAUNCHER_HEADER_Y + 8, cx + 2, LAUNCHER_HEADER_Y + 12, 2, theme_current()->text_muted);
            x += CORNER_GAP;
        }
        bool on = (i == 0 && active == LAUNCHER_TAB_HOME) || (i == 1 && active == LAUNCHER_TAB_LIBRARY);
        launcher_text(x, LAUNCHER_HEADER_Y, 0, ALIGN_LEFT, on ? STL_DEFAULT : STL_GRAY, "%s", parts[i]);
        x += widths[i];
    }
}

/** @brief A full-width bar with the tab's name in capitals and the clock. */
static void draw_header_bar (menu_t *menu, launcher_tab_t active) {
    const theme_t *t = theme_current();
    flat_begin(t->feature);
    rdpq_fill_rectangle(0, 0, display_get_width(), LAUNCHER_HEADER_BAR_H);
    flat_end();
    launcher_text(LAUNCHER_X0, LAUNCHER_HEADER_Y - 2, 0, ALIGN_LEFT, STL_ON_FEATURE, "%s",
        active == LAUNCHER_TAB_HOME ? "HOME" : "LIBRARY");
    char clock[16];
    launcher_clock_text(menu, clock, sizeof(clock));
    if (clock[0]) {
        launcher_text(LAUNCHER_X0, LAUNCHER_HEADER_Y - 2, LAUNCHER_WIDTH, ALIGN_RIGHT, STL_ON_FEATURE, "%s", clock);
    }
}


void launcher_set_header_on_feature (bool on) {
    header_on_feature = on;
}

/**
 * @brief Home / Library with the underline, the clock, and the music note or new games notice. ease: move the underline
 * on (false when drawn a second time in a frame, over a full-screen fade).
 */
static void draw_tabs (menu_t *menu, launcher_tab_t active, bool ease) {
    static const char *tabs[] = { "Home", "Library" };
    bool on_feature = header_on_feature;
    int x = LAUNCHER_X0, line_x = x, line_w = 0;
    for (int i = 0; i < 2; i++) {
        bool on = (i == (int)active);
        menu_font_style_t style = on_feature ? STL_ON_FEATURE : (on ? STL_DEFAULT : STL_GRAY);
        int w = launcher_text(x, LAUNCHER_HEADER_Y, 0, ALIGN_LEFT, style, "%s", tabs[i]);
        if (on) {
            line_x = x;
            line_w = w;
        }
        x += w + HEADER_TAB_SPACING;
    }
    // The underline glides from Home to Library and back as the screens change.
    if (tab_line_x < 0.0f) {
        tab_line_x = (float)line_x;
        tab_line_w = (float)line_w;
    }
    if (ease) {
        tab_line_x = launcher_ease(tab_line_x, (float)line_x);
        tab_line_w = launcher_ease(tab_line_w, (float)line_w);
    }
    int lx = (int)lroundf(tab_line_x);
    launcher_fill_rounded(lx, HEADER_UNDERLINE_Y, lx + (int)lroundf(tab_line_w), HEADER_UNDERLINE_Y + 3, 1,
        on_feature ? theme_current()->on_feature : theme_current()->accent2);

    if (menu->current_time >= 0) {
        char clock[16];
        launcher_clock_text(menu, clock, sizeof(clock));
        launcher_text(LAUNCHER_X0, LAUNCHER_HEADER_Y, LAUNCHER_WIDTH, ALIGN_RIGHT, STL_GRAY, "%s", clock);
    }
    // The music note takes the spot for its few seconds; the new games notice comes back after.
    if (!draw_now_playing()) {
        launcher_draw_new_games_notice();
    }
}

static void draw_header_parts (menu_t *menu, launcher_tab_t active) {
    launcher_draw_background();
    launcher_header_style_t style = header_style;
    header_style = LAUNCHER_HEADER_TABS;    // Only for this frame's header.
    if (style == LAUNCHER_HEADER_CORNER) {
        draw_header_corner(menu, active);
        launcher_draw_notes(LAUNCHER_X0, LAUNCHER_HEADER_Y + 22, LAUNCHER_WIDTH, ALIGN_RIGHT);   // Under it.
        return;
    }
    if (style == LAUNCHER_HEADER_BAR) {
        draw_header_bar(menu, active);
        launcher_draw_notes(LAUNCHER_X0 + HEADER_NOTE_X, LAUNCHER_HEADER_Y, LAUNCHER_WIDTH - HEADER_NOTE_X - HEADER_CLOCK_W,
            ALIGN_RIGHT);
        return;
    }
    if (style == LAUNCHER_HEADER_NONE) {
        return;
    }

    draw_tabs(menu, active, true);
}

int launcher_pill_width (const char *name) {
    char escaped[128];
    escape_text_codes(name, escaped, sizeof(escaped));
    return text_width(strpbrk(name, "$^") ? escaped : name) + PILL_PADDING * 2;
}

int launcher_draw_pill (int x, int y, const char *name, bool active) {
    char escaped[128];
    escape_text_codes(name, escaped, sizeof(escaped));
    // '$' and '^' are rare in folder names; measuring the original keeps the width cached.
    int w = text_width(strpbrk(name, "$^") ? escaped : name) + PILL_PADDING * 2;
    fill_smooth_rounded(x, y, x + w, y + PILL_HEIGHT, PILL_HEIGHT / 2, active ? theme_current()->accent : theme_current()->panel);
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

#define PROMPT_H            (34)    /* launcher_draw_prompt's pill */
#define PROMPT_ICON         (22)
#define PROMPT_PAD          (8)

int launcher_draw_prompt (int x, int y, launcher_button_t kind, const char *button, const char *label, color_t pill) {
    color_t color;
    switch (kind) {
        case LAUNCHER_BUTTON_A: color = BUTTON_A_COLOR; break;
        case LAUNCHER_BUTTON_B: color = BUTTON_B_COLOR; break;
        case LAUNCHER_BUTTON_START: color = BUTTON_START_COLOR; break;
        default: color = theme_current()->text_muted; break;
    }
    int label_w = text_width(label) + 2;
    int w = PROMPT_PAD + PROMPT_ICON + PROMPT_PAD + label_w + PROMPT_PAD + 4;
    fill_smooth_rounded(x, y, x + w, y + PROMPT_H, PROMPT_H / 2, pill);
    // The button as the hints draw it: a colored circle with its letter.
    int ix = x + PROMPT_PAD, iy = y + (PROMPT_H - PROMPT_ICON) / 2;
    fill_smooth_rounded(ix, iy, ix + PROMPT_ICON, iy + PROMPT_ICON, PROMPT_ICON / 2, color);
    text_centered_ink(font_for(button, FNT_LAUNCHER), ix, iy, PROMPT_ICON, PROMPT_ICON, STL_ON_BUTTON, button);
    text_box(ix + PROMPT_ICON + PROMPT_PAD, y, 0, PROMPT_H, ALIGN_LEFT, VALIGN_CENTER, STL_DEFAULT, label);
    return w;
}

static bool is_c_button (launcher_button_t kind) {
    return kind == LAUNCHER_BUTTON_C_UP_DOWN || kind == LAUNCHER_BUTTON_C_LEFT_RIGHT;
}

void launcher_draw_header (menu_t *menu, launcher_tab_t active) {
    uint64_t perf_started = get_ticks_us();
    // Home's styles without the tab row draw from the top of the screen to the bottom (Spotlight's panel): their
    // whole screen fades in, or the panel's ends showed at full color around the fading middle, like a tear.
    // So do styles with a band drawn with the background (Spotlight's panel, Cartridge's band): the tabs on top are
    // drawn again over the fade, so the header stays steady while the rest fades.
    bool tabs = (header_style == LAUNCHER_HEADER_TABS);
    bool full_screen = !tabs || band_set;
    header_redraw = tabs && band_set;
    header_menu = menu;
    header_active = active;
    bool on_feature = header_on_feature;
    draw_header_parts(menu, active);
    header_on_feature = on_feature;     // Kept for the redraw; cleared after it.
    launcher_transition_content_begin(full_screen ? 0 : HEADER_UNDERLINE_Y + 6);
    transition_y1 = full_screen ? display_get_height() : LAUNCHER_FOOTER_Y;
    perf_log_add(PERF_CHROME, (uint32_t)(get_ticks_us() - perf_started));
}

#define DIALOG_W            (380)
#define DIALOG_PAD          (24)

void launcher_draw_dialog (const char *title, const char *body, const launcher_hint_t *buttons, int count) {
    const theme_t *t = theme_current();
    fade_band(0, display_get_height(), t->bg, MENU_BACKDROP_ALPHA);
    int w = DIALOG_W;
    int text_w = w - 2 * DIALOG_PAD;
    // The message's height, laid out once (it is cached with the text).
    char escaped[512];
    int body_h = 0;
    if (body && body[0]) {
        escape_text_codes(body, escaped, sizeof(escaped));
        int nbytes = (int)strlen(escaped);
        rdpq_paragraph_t *layout = rdpq_paragraph_build(&(rdpq_textparms_t) { .width = px(text_w) - px(0), .wrap = WRAP_WORD },
            launcher_font(FNT_LAUNCHER), escaped, &nbytes);
        body_h = (int)(layout->bbox.y1 - layout->bbox.y0) + 6;
        rdpq_paragraph_free(layout);
        body_h = (body_h > 5 * 22) ? 5 * 22 : body_h;
    }
    int h = DIALOG_PAD + 30 + (body_h ? 8 + body_h : 0) + 20 + PROMPT_H + DIALOG_PAD;
    int x0 = LAUNCHER_CENTER_X - w / 2, y0 = display_get_height() / 2 - h / 2 - 10;
    launcher_fill_rounded(x0 - 1, y0 - 1, x0 + w + 1, y0 + h + 1, 15, t->line);
    launcher_fill_rounded(x0, y0, x0 + w, y0 + h, 14, t->panel);
    int y = y0 + DIALOG_PAD;
    launcher_title(x0 + DIALOG_PAD, y, text_w, ALIGN_LEFT, STL_DEFAULT, title);
    y += 30;
    if (body_h) {
        y += 8;
        text_box_wrapped(FNT_LAUNCHER, x0 + DIALOG_PAD, y, text_w, 5 * 22, ALIGN_LEFT, VALIGN_TOP, STL_GRAY, escaped);
        y += body_h;
    }
    y += 20;
    int x = x0 + DIALOG_PAD - PROMPT_PAD;
    for (int i = 0; i < count; i++) {
        x += launcher_draw_prompt(x, y, buttons[i].kind, buttons[i].button, buttons[i].label, t->panel) + 10;
    }
}

/* The hints bar: one size up from the small text (15 px, 20 px icons), to read from the couch. */
#define FOOTER_BUTTON_SIZE      (20)
#define FOOTER_TEXT_H           (24)
#define FOOTER_ICON_GAP         (6)
#define FOOTER_GAP              (18)

/*
 * L, R and Z drawn as on the controller (the N64 pad and the 8BitDo 64 alike): L and R are wide shoulder bars, Z an
 * upright trigger; "L R" is the two bars side by side. A, B, Start and the C buttons are round, as they are.
 */
#define SHOULDER_W          (32)
#define SHOULDER_H          (16)
#define SHOULDER_PAIR_W     (28)    /* Each bar of "L R" */
#define SHOULDER_PAIR_GAP   (4)
#define TRIGGER_W           (18)
#define TRIGGER_H           (24)
#define SHOULDER_RADIUS     (5)

typedef enum { SHAPE_ROUND, SHAPE_SHOULDER, SHAPE_TRIGGER, SHAPE_SHOULDERS } button_shape_t;

static button_shape_t button_shape (const launcher_hint_t *hint) {
    if (hint->kind != LAUNCHER_BUTTON_OTHER) {
        return SHAPE_ROUND;
    }
    if (strcmp(hint->button, "Z") == 0) {
        return SHAPE_TRIGGER;
    }
    if (strcmp(hint->button, "L") == 0 || strcmp(hint->button, "R") == 0) {
        return SHAPE_SHOULDER;
    }
    if (strcmp(hint->button, "L R") == 0) {
        return SHAPE_SHOULDERS;
    }
    return SHAPE_ROUND;
}

static int shape_width (button_shape_t shape) {
    switch (shape) {
        case SHAPE_SHOULDER: return SHOULDER_W;
        case SHAPE_TRIGGER: return TRIGGER_W;
        case SHAPE_SHOULDERS: return 2 * SHOULDER_PAIR_W + SHOULDER_PAIR_GAP;
        default: return 0;
    }
}

/**
 * @brief A shoulder bar: round at its outer end (left for L, right for R), squarer at the inner end, like the
 * controller's shoulders seen from the front.
 */
static void shoulder_bar (int x, int y0, int w, bool right, color_t color) {
    int r = SHOULDER_H / 2;
    if (right) {
        fill_smooth_rounded(x, y0, x + w - r, y0 + SHOULDER_H, 3, color);
        fill_smooth_rounded(x + w - 2 * r, y0, x + w, y0 + SHOULDER_H, r, color);
    } else {
        fill_smooth_rounded(x, y0, x + 2 * r, y0 + SHOULDER_H, r, color);
        fill_smooth_rounded(x + r, y0, x + w, y0 + SHOULDER_H, 3, color);
    }
}

/** @brief A shoulder button or trigger centred on the row (y is the top of the round icons, size their height). */
static void draw_shaped_button (button_shape_t shape, int x, int y, int size, color_t color, const char *text) {
    int cy = y + size / 2;
    if (shape == SHAPE_TRIGGER) {
        int y0 = cy - TRIGGER_H / 2;
        fill_smooth_rounded(x, y0, x + TRIGGER_W, y0 + TRIGGER_H, SHOULDER_RADIUS, color);
        text_centered_ink(FNT_LAUNCHER, x, y0, TRIGGER_W, TRIGGER_H, STL_ON_BUTTON, text);
        return;
    }
    int y0 = cy - SHOULDER_H / 2;
    if (shape == SHAPE_SHOULDER) {
        shoulder_bar(x, y0, SHOULDER_W, text[0] == 'R', color);
        text_centered_ink(FNT_LAUNCHER, x, y0, SHOULDER_W, SHOULDER_H, STL_ON_BUTTON, text);
        return;
    }
    // "L R": the two shoulders, mirrored.
    int x1 = x + SHOULDER_PAIR_W + SHOULDER_PAIR_GAP;
    shoulder_bar(x, y0, SHOULDER_PAIR_W, false, color);
    shoulder_bar(x1, y0, SHOULDER_PAIR_W, true, color);
    text_centered_ink(FNT_LAUNCHER, x, y0, SHOULDER_PAIR_W, SHOULDER_H, STL_ON_BUTTON, "L");
    text_centered_ink(FNT_LAUNCHER, x1, y0, SHOULDER_PAIR_W, SHOULDER_H, STL_ON_BUTTON, "R");
}

void launcher_draw_footer (const launcher_hint_t *hints, int count) {
    uint64_t perf_started = get_ticks_us();
    transition_content_end();
    if (footer_line) {
        ui_components_box_draw(px(LAUNCHER_X0), LAUNCHER_FOOTER_Y, px(LAUNCHER_X1), LAUNCHER_FOOTER_Y + 1, theme_current()->line);
    }
    footer_line = true;     // Only for this frame's footer.

    // Lay the hints out right-aligned: [button] label   [button] label ...
    int button_w[8];
    int label_w[8];
    int total = 0;
    if (count > 8) {
        count = 8;
    }
    // Measured widths are glyph extents; a little slack keeps the last letter from touching the next icon.
    for (int i = 0; i < count; i++) {
        // One letter sits on a circle (wider would be an oval); more letters ("L R", "C" and arrows) on a pill.
        int bw = (hints[i].kind == LAUNCHER_BUTTON_LEFT_RIGHT) ? FOOTER_BUTTON_SIZE + 8 :
            text_width_font(FNT_LAUNCHER, hints[i].button) + 9 + (is_c_button(hints[i].kind) ? C_ARROWS_WIDTH : 0);
        bool single = strlen(hints[i].button) == 1 && !is_c_button(hints[i].kind);
        button_w[i] = (single || bw < FOOTER_BUTTON_SIZE) ? FOOTER_BUTTON_SIZE : bw;
        if (button_shape(&hints[i]) != SHAPE_ROUND) {
            button_w[i] = shape_width(button_shape(&hints[i]));
        }
        label_w[i] = text_width_font(FNT_LAUNCHER, hints[i].label) + 2;
        total += button_w[i] + FOOTER_ICON_GAP + label_w[i] + (i > 0 ? FOOTER_GAP : 0);
    }

    int x = LAUNCHER_X1 - total;
    int y = LAUNCHER_FOOTER_Y + 12;
    int text_y = y - (FOOTER_TEXT_H - FOOTER_BUTTON_SIZE) / 2;
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
        button_shape_t shape = button_shape(&hints[i]);
        if (shape != SHAPE_ROUND) {
            draw_shaped_button(shape, x, y, FOOTER_BUTTON_SIZE, color, hints[i].button);
        } else {
            fill_smooth_rounded(x, y, x + button_w[i], y + FOOTER_BUTTON_SIZE, FOOTER_BUTTON_SIZE / 2, color);
        }
        if (shape != SHAPE_ROUND) {
            // Drawn with its letters above.
        } else if (is_c_button(hints[i].kind)) {
            // "C" on the left, a pair of arrows drawn on the right.
            int letter_w = button_w[i] - C_ARROWS_WIDTH;
            text_centered_ink(FNT_LAUNCHER, x + 1, text_y, letter_w, FOOTER_TEXT_H, STL_ON_C_BUTTON, hints[i].button);
            int ax = x + letter_w + 1;
            int cy = y + FOOTER_BUTTON_SIZE / 2;
            if (hints[i].kind == LAUNCHER_BUTTON_C_UP_DOWN) {
                draw_arrow(ax + C_ARROWS_WIDTH / 2 - 2, cy - 3, 0, -1);
                draw_arrow(ax + C_ARROWS_WIDTH / 2 - 2, cy + 3, 0, 1);
            } else {
                // Two arrow heads apart, not touching tips.
                launcher_draw_arrow(ax + 4, cy, 8, false, BUTTON_C_INK);
                launcher_draw_arrow(ax + 12, cy, 8, true, BUTTON_C_INK);
            }
        } else if (hints[i].kind == LAUNCHER_BUTTON_LEFT_RIGHT) {
            // Two arrow heads, each centred on its half of the pill.
            int cx = x + button_w[i] / 2, cy = y + FOOTER_BUTTON_SIZE / 2;
            launcher_draw_arrow(cx - 5, cy, 9, false, RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
            launcher_draw_arrow(cx + 5, cy, 9, true, RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
        } else {
            text_centered_ink(FNT_LAUNCHER, x, text_y, button_w[i], FOOTER_TEXT_H, STL_ON_BUTTON, hints[i].button);
        }
        x += button_w[i] + FOOTER_ICON_GAP;
        // No width: the label is never cut short, whatever the measurement said.
        text_box_font(FNT_LAUNCHER, x, text_y, 0, FOOTER_TEXT_H, ALIGN_LEFT, VALIGN_CENTER, STL_DEFAULT, hints[i].label);
        x += label_w[i] + FOOTER_GAP;
    }
    perf_log_add(PERF_CHROME, (uint32_t)(get_ticks_us() - perf_started));
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

void launcher_draw_favorite_mark (int cx, int cy) {
    fill_star(cx, cy, 8.0f, theme_current()->bg);           // backing, so it reads on any cover
    fill_star(cx, cy, 6.0f, theme_current()->accent);
}

void launcher_draw_favorite_star (int x, int y, int w) {
    launcher_draw_favorite_mark(x + w - 9, y + 9);
}

/**
 * @brief Outline of a rounded rectangle in real pixels: the band between the outer shape and the
 * inner one (inset by thickness). Only the band is drawn, so whatever is inside shows through.
 */
static void outline_rounded_real (int x0, int y0, int x1, int y1, int radius, int thickness, float squeeze, color_t color) {
    uint64_t perf_started = get_ticks_us();
    int inner_radius = radius - thickness;
    int t = (int)(thickness * squeeze + 0.5f);
    // Rows within the corners differ; between them both sides are straight and drawn in one piece each.
    int corner = (radius > thickness) ? radius : thickness;
    if (corner * 2 > y1 - y0) {
        corner = (y1 - y0) / 2;
    }
    flat_begin(color);
    for (int y = y0; y < y1; y++) {
        int from_top = y - y0;
        int from_bottom = y1 - 1 - y;
        int edge = from_top < from_bottom ? from_top : from_bottom;
        if (edge >= corner) {
            int straight_end = y1 - corner;
            rdpq_fill_rectangle(x0, y, x0 + t, straight_end);
            rdpq_fill_rectangle(x1 - t, y, x1, straight_end);
            y = straight_end - 1;
            continue;
        }
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
    perf_log_add(PERF_SHAPES, (uint32_t)(get_ticks_us() - perf_started));
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

static void draw_ring_around (int x0, int y0, int x1, int y1, int radius, bool cover);

/** @brief The gliding focus ring, around a cover tile or around something else (a card). */
static void focus_ring (int x0, int y0, int x1, int y1, int radius, bool cover) {
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
    draw_ring_around((int)lroundf(ring_x0), (int)lroundf(ring_y0), (int)lroundf(ring_x1), (int)lroundf(ring_y1),
        (int)lroundf(ring_radius), cover);
}

void launcher_draw_focus_ring (int x0, int y0, int x1, int y1, int radius) {
    focus_ring(x0, y0, x1, y1, radius, false);
}

void launcher_draw_focus_ring_tile (int x, int y, int w, int h) {
    focus_ring(x, y, x + w, y + h, TILE_RADIUS, true);
}

/**
 * @brief Where a cover drawn edge to edge sits in its tile (as launcher_draw_tile_zoomed places it); false when it
 * doesn't fill the tile (it sits on a panel). It can be a pixel or two short (packs keep even sizes: 86x60 in an
 * 87x61 grid tile), so a ring around the tile showed the background in between.
 */
static bool cover_rect_in_tile (int x, int y, int w, int h, surface_t *image, float zoom, int *ix, int *iy, int *iw, int *ih) {
    if (!image) {
        return false;
    }
    *iw = (int)(image->width * zoom);
    *ih = (int)(image->height * zoom);
    if (*iw < w - 2 || *ih < h - 2) {
        return false;
    }
    *ix = x + (w - *iw) / 2;
    *iy = y + (h - *ih) / 2;
    return true;
}

void launcher_draw_focus_ring_cover_zoomed (int x, int y, int w, int h, surface_t *image, float zoom) {
    int ix, iy, iw, ih;
    if (cover_rect_in_tile(x, y, w, h, image, zoom, &ix, &iy, &iw, &ih)) {
        focus_ring(ix, iy, ix + iw, iy + ih, TILE_RADIUS, true);     // Hugging the cover itself
        return;
    }
    launcher_draw_focus_ring_tile(x, y, w, h);
}

void launcher_draw_focus_ring_cover (int x, int y, int w, int h, surface_t *image) {
    launcher_draw_focus_ring_cover_zoomed(x, y, w, h, image, 1.0f);
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

/**
 * @brief The ring's inner edges meet what it surrounds: around a cover in 16:9, the width the cover
 * was squeezed to; around anything else (a card), its own right edge.
 */
static void draw_ring_around (int x0, int y0, int x1, int y1, int radius, bool cover) {
    int t = (int)(LAUNCHER_RING * (widescreen ? WIDE_SQUEEZE : 1.0f) + 0.5f);
    int rx0 = px(x0) - t;
    int rx1 = (cover ? px(x0) + real_cover_width(x1 - x0) : px(x1)) + t;
    outline_rounded_real(rx0, y0 - LAUNCHER_RING, rx1, y1 + LAUNCHER_RING,
        radius + LAUNCHER_RING, LAUNCHER_RING, widescreen ? WIDE_SQUEEZE : 1.0f, theme_current()->accent);
}

void launcher_draw_ring (int x0, int y0, int x1, int y1, int radius) {
    draw_ring_around(x0, y0, x1, y1, radius, true);
}

/**
 * @brief Draw a cover resized (bilinear) to exactly real_w x h framebuffer pixels at real x rx. The filter blends
 * each pixel with the next texel, so the last column and row would read past the image (a ragged line along the
 * right edge): the blit stops at the last texel, and the size is set exactly so the corner masks line up.
 */
static void blit_filtered (surface_t *source, int rx, int y, int real_w, int h) {
    int sw = (source->width > 1) ? source->width - 1 : source->width;
    int sh = (source->height > 1) ? source->height - 1 : source->height;
    rdpq_tex_blit(source, rx, y, &(rdpq_blitparms_t) {
        .width = sw,
        .height = sh,
        .scale_x = (float)real_w / sw,
        .scale_y = (float)h / sh,
    });
}

/**
 * @brief Draw a cover at logical (x, y) at its own size: copied 1:1 in 4:3, squeezed to 3/4 width in 16:9.
 */
static void blit_cover (surface_t *image, int x, int y) {
    // Covers keep a 1-bit alpha (e.g. the arch above a cartridge label); transparent pixels are skipped.
    surface_t *squeezed = launcher_cover_physical(image);
    rdpq_mode_push();
    if (squeezed && widescreen) {
        rdpq_set_mode_copy(true);
        rdpq_tex_blit(squeezed, px(x), y, NULL);
    } else if (squeezed) {
        // 4:3 with packs built for 16:9 only (coverpack --wide-only): the squeezed cover widened back as it is drawn,
        // rather than every cover decoded from its PNG (which made covers very slow on a 4:3 screen).
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_alphacompare(COVER_ALPHA_THRESHOLD);
        blit_filtered(squeezed, px(x), y, image->width, image->height);
    } else if (widescreen) {
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_alphacompare(COVER_ALPHA_THRESHOLD);
        blit_filtered(image, px(x), y, real_cover_width(image->width), image->height);
    } else {
        rdpq_set_mode_copy(true);
        rdpq_tex_blit(image, px(x), y, NULL);
    }
    rdpq_mode_pop();
}

/**
 * @brief Draw a cover resized to fill logical (x, y, w, h) (filtered). In 16:9 it ends where
 * launcher_draw_ring's inner edge begins, as a copied cover does.
 */
static void blit_cover_resized (surface_t *image, int x, int y, int w, int h) {
    surface_t *squeezed = launcher_cover_physical(image);   // In 4:3 too: resized to the 4:3 width.
    surface_t *source = squeezed ? squeezed : image;
    int real_w = real_cover_width(w);
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_alphacompare(COVER_ALPHA_THRESHOLD);
        blit_filtered(source, px(x), y, real_w, h);
    rdpq_mode_pop();
}

void launcher_draw_tile_zoomed (int x, int y, int w, int h, surface_t *image, bool pending, const char *title, bool selected, color_t behind, float zoom) {
    uint64_t started = get_ticks_us();
    color_t edge = behind;
    if (selected) {
        // Around the cover itself where it fills the tile (Carousel): around the tile it left a gap.
        int rx, ry, rw, rh;
        if (cover_rect_in_tile(x, y, w, h, image, zoom, &rx, &ry, &rw, &rh)) {
            launcher_draw_ring(rx, ry, rx + rw, ry + rh, TILE_RADIUS);
        } else {
            launcher_draw_ring(x, y, x + w, y + h, TILE_RADIUS);
        }
        launcher_note_focused_cover_zoomed(image, x, y, w, h, zoom);
    }

    // Covers that nearly fill the tile are drawn edge to edge; others sit on a rounded panel.
    int iw = image ? (int)(image->width * zoom) : 0;
    int ih = image ? (int)(image->height * zoom) : 0;
    bool fills_tile = image && iw >= w - 2 && ih >= h - 2;
    if (!fills_tile) {
        launcher_fill_rounded(x, y, x + w, y + h, TILE_RADIUS, theme_current()->panel);
    }

    if (image) {
        int ix = x + (w - iw) / 2;
        int iy = y + (h - ih) / 2;
        if (zoom == 1.0f) {
            blit_cover(image, ix, iy);
        } else {
            blit_cover_resized(image, ix, iy, iw, ih);
        }
        if (fills_tile) {
            mask_corners(ix, iy, ix + iw, iy + ih, TILE_RADIUS, edge);
        }
    } else if (!pending && title) {
        char escaped[256];
        escape_text_codes(title, escaped, sizeof(escaped));
        text_box_wrapped(font_for(escaped, FNT_LAUNCHER), x + 4, y + 4, w - 8, h - 8, ALIGN_CENTER, VALIGN_CENTER, STL_GRAY, escaped);
    }
    perf_log_add(PERF_COVERS, (uint32_t)(get_ticks_us() - started));
}

void launcher_draw_tile (int x, int y, int w, int h, surface_t *image, bool pending, const char *title, bool selected, color_t behind) {
    launcher_draw_tile_zoomed(x, y, w, h, image, pending, title, selected, behind, 1.0f);
}

#define SPINE_RADIUS        (2)

void launcher_draw_spine (int x, int y, int w, int h, surface_t *image) {
    uint64_t started = get_ticks_us();
    if (!image) {
        launcher_fill_rounded(x, y, x + w, y + h, SPINE_RADIUS, theme_current()->panel);
        return;
    }
    if (image->width == w && image->height == h) {
        blit_cover(image, x, y);
    } else {
        blit_cover_resized(image, x, y, w, h);
    }
    mask_corners(x, y, x + w, y + h, SPINE_RADIUS, theme_current()->bg);
    perf_log_add(PERF_COVERS, (uint32_t)(get_ticks_us() - started));
}

void launcher_draw_cover_scaled (surface_t *image, int x, int y, int w, int h, color_t behind) {
    uint64_t started = get_ticks_us();
    float scale = fminf((float)w / image->width, (float)h / image->height);
    int dw = (int)(image->width * scale);
    int dh = (int)(image->height * scale);
    int dx = x + (w - dw) / 2;
    int dy = y + (h - dh) / 2;
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_alphacompare(COVER_ALPHA_THRESHOLD);
        // A cover from a 16:9 pack is squeezed already: resize it as is. Its width is the one the corner
        // masks below use, so they meet its edges.
        surface_t *squeezed = launcher_cover_physical(image);
        blit_filtered(squeezed ? squeezed : image, px(dx), dy, real_cover_width(dw), dh);
    rdpq_mode_pop();
    mask_corners(dx, dy, dx + dw, dy + dh, (int)(TILE_RADIUS * scale + 0.5f), behind);
    perf_log_add(PERF_COVERS, (uint32_t)(get_ticks_us() - started));
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
        uint64_t started = get_ticks_us();
        sound_bgm_begin_load();
        uint64_t filled = get_ticks_us();
        launch_cover_take();
        perf_log_event("launch: music queued ahead in %.1f ms, cover taken in %.1f ms", (filled - started) / 1000.0f,
            (get_ticks_us() - filled) / 1000.0f);
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
        { .text = "Shelf", .action = set_layout, .arg = (void *)(uintptr_t)(LAUNCHER_LAYOUT_SHELF) },
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







/* Start menu state: the highlighted game, and requests for the library view. */
static path_t *selected_rom = NULL;
static launcher_request_t pending_request = LAUNCHER_REQUEST_NONE;
static bool favorites_changed = false;
static char favorite_label[32] = "Add to favorites";
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
static bool quick_visible(void);

#define SAVE_IDLE_MS        (1000)

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
    // A save the Quick menu left waiting: written once the controller has rested a moment, not as it closed.
    if (settings_save_waiting() && now - last_input_ms > SAVE_IDLE_MS && !quick_visible()) {
        settings_flush();
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



static void open_search (menu_t *menu, void *arg) {
    (void)arg;
    request(menu, LAUNCHER_REQUEST_SEARCH);
}

int launcher_requested_category (void) {
    return requested_category;
}

launcher_request_t launcher_peek_request (void) {
    return pending_request;
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

void launcher_settings_do (menu_t *menu, launcher_do_t what) {
    settings_save(&menu->settings);
    launcher_transition(menu->mode, (what == LAUNCHER_DO_FILE_BROWSER) ? MENU_MODE_BROWSER : MENU_MODE_LIBRARY);
    menu->launcher_return_mode = MENU_MODE_NONE;
    switch (what) {
        case LAUNCHER_DO_FILE_BROWSER:
            menu->next_mode = MENU_MODE_BROWSER;    // The file browser itself (no launcher screen to return to).
            return;
        case LAUNCHER_DO_RESCAN: rescan_folders(menu, NULL); break;
        case LAUNCHER_DO_FOLDERS: pending_request = LAUNCHER_REQUEST_FOLDERS; break;
        case LAUNCHER_DO_FAVORITES: pending_request = LAUNCHER_REQUEST_FAVORITES; break;
        case LAUNCHER_DO_HIDDEN: pending_request = LAUNCHER_REQUEST_HIDDEN; break;
    }
    menu->next_mode = MENU_MODE_LIBRARY;
}




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











static void choose_folder (menu_t *menu, void *arg) {
    requested_category = (int)(uintptr_t)(arg);
    request(menu, LAUNCHER_REQUEST_CATEGORY);
}

static int get_folder_selection (menu_t *menu) {
    int index = library_category_find(menu->settings.launcher_category);
    return index >= 0 ? index : 0;
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
    library_init(menu);     // The folders, for the Quick menu's Folder row.
}

/*
 * Start: the Quick menu, a panel sliding in from the right. Its rows follow the screen it was opened on: Home's own
 * (style, theme) on Home, the view's (folder, filter, sort, layout, size, covers, theme) in the Library. Every value
 * changes with left and right, in place; A is for the buttons and Settings.
 */
typedef enum {
    QR_BUTTONS,         /* Search | Surprise me */
    QR_GAME_FAVORITE,   /* This game: the highlighted one (Z opens the panel here) */
    QR_GAME_ABOUT,
    QR_GAME_OPTIONS,
    QR_GAME_HIDE,
    QR_FOLDER,
    QR_FILTER_BY,
    QR_FILTER_SHOW,     /* Only with a field to filter by */
    QR_SORT,
    QR_LAYOUT,
    QR_TILE_SIZE,
    QR_COVERS,
    QR_HOME_STYLE,
    QR_THEME,
    QR_MUSIC,           /* Pause | Skip */
    QR_SETTINGS,
} quick_kind_t;

typedef struct {
    quick_kind_t kind;
    const char *heading;    /* Starts a group ("This view") */
} quick_row_t;

#define QUICK_MAX_ROWS      (16)
#define QUICK_W             (320)   /* Logical width of the panel */
#define QUICK_PAD           (18)
#define QUICK_TOP           (12)
#define QUICK_BOTTOM        (418)   /* Above the button hints */
#define QUICK_BUTTON_H      (38)
#define QUICK_ROW_H         (28)
#define QUICK_ROW_GAP       (2)
#define QUICK_GAP           (6)
#define QUICK_HEADING_H     (22)
#define QUICK_MS            (170)   /* Slide in or out */
#define QUICK_REDRAW_MS     (1000)  /* Held, the picture is still redrawn this often (the clock) */

static const char *filter_field_names[] = { "None", "Genre", "Developer", "Year", "Players" };
static const int filter_field_ids[] = { -1, LAUNCHER_SORT_GENRE, LAUNCHER_SORT_DEVELOPER, LAUNCHER_SORT_YEAR, LAUNCHER_SORT_PLAYERS };
#define FILTER_FIELD_COUNT  (5)

static const int quick_home_styles[] = { LAUNCHER_HOME_CLASSIC, LAUNCHER_HOME_SPOTLIGHT, LAUNCHER_HOME_CARTRIDGE, LAUNCHER_HOME_MINIMAL };
static const char *quick_home_style_names[] = { "Classic", "Spotlight", "Cartridge", "Minimal" };
#define QUICK_HOME_STYLES   (4)

static quick_row_t quick_rows[QUICK_MAX_ROWS];
static int quick_row_count = 0;
static bool quick_open = false;
static uint64_t quick_changed_ms = 0;       /* When it last started opening or closing */
static int quick_row = 0;
static int quick_col = 0;
static menu_mode_t quick_mode = MENU_MODE_NONE;    /* The screen it was opened on */
static float quick_scroll_pos = 0.0f;
static int quick_scroll = 0;
static int quick_folder = 0;                /* The folder shown (changed here before the Library catches up) */
static int quick_filter_field = -1;         /* Filtering by (a field whose Show is still "All" counts) */
static library_group_t *quick_groups = NULL;
static int quick_group_count = 0;
static int quick_groups_field = -1, quick_groups_folder = -1;   /* What quick_groups were listed for */
static uint64_t quick_drawn_ms = 0;         /* The last frame drawn with the panel fully in */
static uint32_t quick_drawn_song = 0;       /* The track playing when it was drawn */
static int quick_settled_frames = 0;        /* Frames drawn in a row with nothing moving */
static int quick_frames_drawn = 0;          /* While open, for the performance log */
static menu_t *quick_owner;                 /* The menu it was opened with (defined with launcher_menu_show) */
static uint64_t quick_arrange_ms = 0;       /* The last change that re-sorts the Library (sort, filter) */
static surface_t quick_image;               /* The panel, drawn once and copied to the screen each frame */
static void *quick_image_block = NULL;      /* Its pixels, borrowed from the cover memory */
static bool quick_image_fresh = false;      /* It shows the panel as it is now */
static uint32_t quick_image_song = 0;

#define QUICK_IMAGE_H       (QUICK_BOTTOM + 14)

static void quick_image_release (void) {
    if (quick_image_block) {
        launcher_covers_return(quick_image_block);
        quick_image_block = NULL;
    }
    quick_image_fresh = false;
}
static int quick_cost_logs = 0;             /* Draw-cost notes so far (the log holds only so many events) */

#define ARRANGE_WAIT_MS     (300)   /* Re-sorting a large folder takes ~90 ms: once the presses stop, not per press */

static uint32_t quick_draw_us = 0;
static int quick_draw_frames = 0;
static bool quick_draw_logged = false;

/** @brief 0 (off screen) to 1 (fully in), eased; it slides back out after closing. */
static float quick_progress (void) {
    float p = fminf(1.0f, (float)(get_ticks_ms() - quick_changed_ms) / QUICK_MS);
    float eased = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
    return quick_open ? eased : 1.0f - eased;
}

static bool quick_visible (void) {
    return quick_open || get_ticks_ms() - quick_changed_ms < QUICK_MS;
}

static void quick_free_groups (void) {
    free(quick_groups);
    quick_groups = NULL;
    quick_group_count = 0;
    quick_groups_field = quick_groups_folder = -1;
}

/** @brief The groups of the field being filtered by, in the folder shown (listed once for each). */
static void quick_need_groups (void) {
    if (quick_filter_field < 0 || (quick_groups_field == quick_filter_field && quick_groups_folder == quick_folder)) {
        return;
    }
    quick_free_groups();
    library_category_t *c = library_category(quick_folder);
    quick_group_count = c ? library_category_groups(c, quick_filter_field, &quick_groups) : 0;
    quick_groups_field = quick_filter_field;
    quick_groups_folder = quick_folder;
}

/** @brief Whether the panel has a game to act on (none on an empty Home or a filter that matches nothing). */
static bool quick_has_game (void) {
    return selected_rom != NULL;
}

static void quick_build_rows (void) {
    int n = 0;
    quick_rows[n++] = (quick_row_t) { QR_BUTTONS, NULL };
    if (quick_has_game()) {
        quick_rows[n++] = (quick_row_t) { QR_GAME_FAVORITE, "This game" };
        if (selected_is_rom()) {
            quick_rows[n++] = (quick_row_t) { QR_GAME_ABOUT, NULL };
            quick_rows[n++] = (quick_row_t) { QR_GAME_OPTIONS, NULL };
        }
        quick_rows[n++] = (quick_row_t) { QR_GAME_HIDE, NULL };
    }
    if (quick_mode == MENU_MODE_HOME) {
        quick_rows[n++] = (quick_row_t) { QR_HOME_STYLE, "Home" };
        quick_rows[n++] = (quick_row_t) { QR_THEME, NULL };
    } else {
        quick_rows[n++] = (quick_row_t) { QR_FOLDER, "This view" };
        quick_rows[n++] = (quick_row_t) { QR_FILTER_BY, NULL };
        if (quick_filter_field >= 0) {
            quick_rows[n++] = (quick_row_t) { QR_FILTER_SHOW, NULL };
        }
        quick_rows[n++] = (quick_row_t) { QR_SORT, NULL };
        quick_rows[n++] = (quick_row_t) { QR_LAYOUT, NULL };
        quick_rows[n++] = (quick_row_t) { QR_TILE_SIZE, NULL };
        quick_rows[n++] = (quick_row_t) { QR_COVERS, NULL };
        quick_rows[n++] = (quick_row_t) { QR_THEME, "Look" };
    }
    quick_rows[n++] = (quick_row_t) { QR_MUSIC, "Music" };
    quick_rows[n++] = (quick_row_t) { QR_SETTINGS, NULL };
    quick_row_count = n;
    if (quick_row >= n) {
        quick_row = n - 1;
    }
}

static void quick_close (bool slide) {
    if (!quick_open) {
        return;
    }
    if (get_ticks_ms() - quick_changed_ms > 5000) {
        perf_log_event("quick menu: closed after %.1f s, %d frames drawn", (get_ticks_ms() - quick_changed_ms) / 1000.0f,
            quick_frames_drawn);
    }
    quick_open = false;
    quick_changed_ms = slide ? get_ticks_ms() : 0;
    quick_free_groups();
    if (!slide) {
        quick_image_release();      // Leaving at once: the screen it goes to may reset the cover memory.
    }
    settings_resume_saves();    // What changed is written in one go once the controller rests, not as it closes.
}

bool launcher_arrange_waiting (void) {
    return quick_open && get_ticks_ms() - quick_arrange_ms < ARRANGE_WAIT_MS;
}

bool launcher_frame_held (void) {
    if (!quick_open || quick_owner == NULL || quick_owner->mode != quick_mode || quick_owner->next_mode != quick_mode) {
        return false;
    }
    if (get_ticks_ms() - quick_arrange_ms < ARRANGE_WAIT_MS + 150) {
        return false;   // A re-sort is waiting for the presses to stop: frames go on until it has shown.
    }
    // Something moving (the slide in, a scroll), a new song, or a while since the last frame.
    return quick_settled_frames >= 4 && sound_bgm_track_generation() == quick_drawn_song &&
        get_ticks_ms() - quick_drawn_ms < QUICK_REDRAW_MS;
}

static bool quick_is_pair (quick_kind_t kind) {
    return kind == QR_BUTTONS || kind == QR_MUSIC;
}

static bool quick_is_value (quick_kind_t kind) {
    return !quick_is_pair(kind) && kind != QR_SETTINGS && kind != QR_GAME_FAVORITE && kind != QR_GAME_ABOUT &&
        kind != QR_GAME_OPTIONS && kind != QR_GAME_HIDE;
}

static const char *quick_label (quick_kind_t kind) {
    switch (kind) {
        case QR_FOLDER: return "Folder";
        case QR_FILTER_BY: return "Filter by";
        case QR_FILTER_SHOW: return "Show";
        case QR_SORT: return "Sort";
        case QR_LAYOUT: return "Layout";
        case QR_TILE_SIZE: return "Tile size";
        case QR_COVERS: return "Covers";
        case QR_HOME_STYLE: return "Home style";
        case QR_THEME: return "Theme";
        case QR_SETTINGS: return "Settings";
        case QR_GAME_FAVORITE: return favorite_label;
        case QR_GAME_ABOUT: return "About this game";
        case QR_GAME_OPTIONS: return "Game options";
        case QR_GAME_HIDE: return "Hide game";
        default: return "";
    }
}

static int quick_home_style_index (menu_t *menu) {
    for (int i = 0; i < QUICK_HOME_STYLES; i++) {
        if (quick_home_styles[i] == menu->settings.launcher_home_style) {
            return i;
        }
    }
    return 0;
}

/** @brief A row's value as shown. */
static const char *quick_value (menu_t *menu, quick_kind_t kind) {
    static char show[80];
    switch (kind) {
        case QR_FOLDER: {
            const char *name = library_category_name(quick_folder);
            return name ? name : "-";
        }
        case QR_FILTER_BY:
            for (int i = 0; i < FILTER_FIELD_COUNT; i++) {
                if (filter_field_ids[i] == quick_filter_field) {
                    return filter_field_names[i];
                }
            }
            return "None";
        case QR_FILTER_SHOW:
            if (launcher_filter_field() < 0) {
                return "All";
            }
            quick_need_groups();
            for (int i = 0; i < quick_group_count; i++) {
                if (strcmp(quick_groups[i].label, launcher_filter_value()) == 0) {
                    snprintf(show, sizeof(show), "%s (%d)", quick_groups[i].label, quick_groups[i].count);
                    return show;
                }
            }
            return launcher_filter_value();
        case QR_SORT: return current_choice(menu, &sort_menu);
        case QR_LAYOUT: return current_choice(menu, &layout_menu);
        case QR_TILE_SIZE: return current_choice(menu, &tile_size_menu);
        case QR_COVERS: return current_choice(menu, &cover_style_menu);
        case QR_HOME_STYLE: return quick_home_style_names[quick_home_style_index(menu)];
        case QR_THEME: return theme_current()->name;
        default: return NULL;
    }
}

static int wrap_index (int i, int n) {
    return ((i % n) + n) % n;
}

/** @brief Step a choice menu to its next (or previous) choice, wrapping round. */
static void quick_cycle_menu (menu_t *menu, component_context_menu_t *c, int step) {
    int n = 0;
    while (c->list[n].text) {
        n++;
    }
    if (n > 0) {
        int i = wrap_index(c->get_default_selection(menu) + step, n);
        c->list[i].action(menu, c->list[i].arg);
    }
}

/** @brief Left or right on a value row: the next or previous value, applied straight away. */
static void quick_change (menu_t *menu, quick_kind_t kind, int step) {
    if (kind == QR_SORT || kind == QR_FILTER_BY || kind == QR_FILTER_SHOW) {
        quick_arrange_ms = get_ticks_ms();
    }
    switch (kind) {
        case QR_FOLDER: {
            int n = library_category_count();
            if (n <= 1) {
                return;
            }
            quick_folder = wrap_index(quick_folder + step, n);
            choose_folder(menu, (void *)(uintptr_t)(quick_folder));     // The Library shows it at once.
            if (launcher_filter_field() >= 0) {
                launcher_filter_set(-1, NULL);      // Its groups differ: Show starts again at All.
            }
            break;
        }
        case QR_FILTER_BY: {
            int i = 0;
            for (int k = 0; k < FILTER_FIELD_COUNT; k++) {
                i = (filter_field_ids[k] == quick_filter_field) ? k : i;
            }
            quick_filter_field = filter_field_ids[wrap_index(i + step, FILTER_FIELD_COUNT)];
            launcher_filter_set(-1, NULL);          // A new field shows everything until a group is picked.
            break;
        }
        case QR_FILTER_SHOW: {
            quick_need_groups();
            int n = quick_group_count + 1;          // "All", then each group
            int i = 0;
            if (launcher_filter_field() >= 0) {
                for (int k = 0; k < quick_group_count; k++) {
                    i = (strcmp(quick_groups[k].label, launcher_filter_value()) == 0) ? k + 1 : i;
                }
            }
            i = wrap_index(i + step, n);
            if (i == 0) {
                launcher_filter_set(-1, NULL);
            } else {
                launcher_filter_set(quick_filter_field, quick_groups[i - 1].label);
            }
            break;
        }
        case QR_SORT: quick_cycle_menu(menu, &sort_menu, step); break;
        case QR_LAYOUT: quick_cycle_menu(menu, &layout_menu, step); break;
        case QR_TILE_SIZE: quick_cycle_menu(menu, &tile_size_menu, step); break;
        case QR_COVERS: quick_cycle_menu(menu, &cover_style_menu, step); break;
        case QR_HOME_STYLE:
            menu->settings.launcher_home_style = quick_home_styles[wrap_index(quick_home_style_index(menu) + step, QUICK_HOME_STYLES)];
            settings_save(&menu->settings);
            break;
        case QR_THEME: {
            int i = theme_current_index();
            for (int k = 0; k < THEME_BUILTIN_COUNT; k++) {
                i = wrap_index(i + step, THEME_BUILTIN_COUNT);
                if (theme_at(i)) {
                    break;
                }
            }
            launcher_apply_theme(menu, i);
            break;
        }
        default:
            return;
    }
    sound_play_effect(SFX_SETTING);
}

/** @brief A on a button or on Settings. */
static void quick_run (menu_t *menu, const quick_row_t *row) {
    sound_play_effect(SFX_ENTER);
    if (row->kind == QR_BUTTONS) {
        quick_close(false);
        if (quick_col == 0) {
            open_search(menu, NULL);
        } else {
            surprise_me(menu, NULL);
        }
    } else if (row->kind == QR_MUSIC) {
        if (quick_col == 0) {
            toggle_pause(menu, NULL);
        } else {
            next_track(menu, NULL);
        }
    } else if (row->kind == QR_SETTINGS) {
        quick_close(false);
        open_screen(menu, (void *)(MENU_MODE_SETTINGS_EDITOR));
    } else if (row->kind == QR_GAME_FAVORITE) {
        // Stays open: the row says what it does now.
        toggle_favorite(menu, NULL);
        snprintf(favorite_label, sizeof(favorite_label), "%s",
            launcher_is_favorite(menu, selected_rom) ? "Remove from favorites" : "Add to favorites");
    } else if (row->kind == QR_GAME_ABOUT) {
        quick_close(false);
        about_game(menu, NULL);
    } else if (row->kind == QR_GAME_OPTIONS) {
        quick_close(false);
        game_options(menu, NULL);
    } else if (row->kind == QR_GAME_HIDE) {
        quick_close(true);
        hide_game(menu, NULL);
    }
}

static bool quick_process (menu_t *menu) {
    if (quick_open && menu->mode != quick_mode) {
        quick_close(false);         // Its screen was left another way (attract mode): it doesn't follow.
    }
    if (!quick_open) {
        return quick_visible() && menu->mode == quick_mode;    // Sliding out: the screen behind waits for it.
    }
    bool input = menu->actions.go_up || menu->actions.go_down || menu->actions.go_left || menu->actions.go_right ||
        menu->actions.enter || menu->actions.back || menu->actions.settings;
    if (input) {
        quick_settled_frames = 0;   // Draw a few frames: the screen behind catches up with a change.
        quick_image_fresh = false;
    }
    quick_build_rows();
    const quick_row_t *row = &quick_rows[quick_row];
    if (menu->actions.back || menu->actions.settings) {
        sound_play_effect(SFX_EXIT);
        quick_close(true);
    } else if (menu->actions.go_up && quick_row > 0) {
        quick_row--;
        sound_play_effect(SFX_TICK);
    } else if (menu->actions.go_down && quick_row + 1 < quick_row_count) {
        quick_row++;
        sound_play_effect(SFX_TICK);
    } else if ((menu->actions.go_left || menu->actions.go_right) && quick_is_value(row->kind)) {
        quick_change(menu, row->kind, menu->actions.go_left ? -1 : 1);
        quick_build_rows();         // Filter by None hides Show; a field brings it back.
    } else if ((menu->actions.go_left || menu->actions.go_right) && quick_is_pair(row->kind)) {
        int col = menu->actions.go_left ? 0 : 1;
        if (col != quick_col) {
            quick_col = col;
            sound_play_effect(SFX_TICK);
        }
    } else if (menu->actions.enter && !quick_is_value(row->kind)) {
        quick_run(menu, row);
    }
    return true;
}

/** @brief A big button: the accent colour when selected, the background colour otherwise. */
static void quick_draw_button (int x0, int y, int x1, int h, bool on, const char *label) {
    const theme_t *theme = theme_current();
    launcher_fill_rounded(x0, y, x1, y + h, 8, on ? theme->accent : theme->bg);
    launcher_text_sized(LAUNCHER_SIZE_LARGE, x0, y, x1 - x0, h, ALIGN_CENTER, on ? STL_ON_ACCENT : STL_DEFAULT, label);
}

/** @brief Each row's top and height (headings above it included in its top), and the bottom of them all. */
static int quick_layout (int *top, int *height) {
    int y = QUICK_TOP;
    for (int r = 0; r < quick_row_count; r++) {
        if (quick_rows[r].heading) {
            y += QUICK_GAP + QUICK_HEADING_H;
        }
        top[r] = y;
        height[r] = (quick_rows[r].kind == QR_BUTTONS) ? QUICK_BUTTON_H : (quick_rows[r].kind == QR_MUSIC) ? QUICK_ROW_H + 4 :
            QUICK_ROW_H;
        y += height[r] + (quick_is_pair(quick_rows[r].kind) ? QUICK_GAP : QUICK_ROW_GAP);
    }
    return y;
}

/* Where the panel is drawn: its own image (from 0), or the screen. */
static int quick_target_width = 0, quick_target_height = 0;

static int quick_target_w (void) {
    return quick_target_width ? quick_target_width : display_get_width();
}

static int quick_target_h (void) {
    return quick_target_height ? quick_target_height : display_get_height();
}

static void quick_draw_rows (menu_t *menu, int left, int right) {
    const theme_t *theme = theme_current();
    int top[QUICK_MAX_ROWS], height[QUICK_MAX_ROWS];
    int bottom = quick_layout(top, height);

    // Keep the selected row (and its group's heading) in view; the list glides there.
    int first = top[quick_row] - (quick_rows[quick_row].heading ? QUICK_HEADING_H + QUICK_GAP : 0);
    if (first - quick_scroll < QUICK_TOP) {
        quick_scroll = first - QUICK_TOP;
    }
    if (top[quick_row] + height[quick_row] - quick_scroll > QUICK_BOTTOM) {
        quick_scroll = top[quick_row] + height[quick_row] - QUICK_BOTTOM;
    }
    int max_scroll = bottom - QUICK_BOTTOM;
    quick_scroll = (quick_scroll > max_scroll) ? max_scroll : quick_scroll;
    quick_scroll = (quick_scroll < 0) ? 0 : quick_scroll;
    quick_scroll_pos = launcher_ease(quick_scroll_pos, (float)quick_scroll);
    int scroll = (int)(quick_scroll_pos + 0.5f);

    rdpq_set_scissor(0, 2, quick_target_w(), QUICK_BOTTOM + 2);
    for (int r = 0; r < quick_row_count; r++) {
        const quick_row_t *row = &quick_rows[r];
        int y = top[r] - scroll;
        bool on = (r == quick_row);
        if (row->heading) {
            int hy = y - QUICK_HEADING_H;
            int w = launcher_text_sized(LAUNCHER_SIZE_BODY, left, hy, 0, QUICK_HEADING_H, ALIGN_LEFT, STL_ACCENT, row->heading);
            if (row->kind == QR_GAME_FAVORITE) {
                // Which game, beside its heading.
                char *title = library_title_from_path(menu, selected_rom);
                launcher_text_sized(LAUNCHER_SIZE_BODY, left + w + 12, hy, right - left - w - 12, QUICK_HEADING_H, ALIGN_LEFT,
                    STL_GRAY, title ? title : "");
                free(title);
            }
            if (row->kind == QR_MUSIC) {
                // What's playing, beside its heading.
                const char *name = sound_bgm_track_name();
                const char *song = (!menu->settings.bgm_enabled || !name) ? "Music is off" : name;
                launcher_text_sized(LAUNCHER_SIZE_BODY, left + w + 12, hy, right - left - w - 12, QUICK_HEADING_H, ALIGN_LEFT,
                    STL_GRAY, song);
            }
        }
        if (y + height[r] < 0 || y > QUICK_BOTTOM) {
            continue;
        }
        if (quick_is_pair(row->kind)) {
            int mid = (left + right) / 2;
            bool buttons = (row->kind == QR_BUTTONS);
            quick_draw_button(left, y, mid - 4, height[r], on && quick_col == 0,
                buttons ? "Search" : (sound_bgm_is_paused() ? "Play" : "Pause"));
            quick_draw_button(mid + 4, y, right, height[r], on && quick_col == 1, buttons ? "Surprise me" : "Skip");
            continue;
        }
        if (on) {
            launcher_fill_rounded(left - 8, y, right + 8, y + QUICK_ROW_H, 6, theme->highlight);
            launcher_fill_rounded(left - 8, y + 4, left - 4, y + QUICK_ROW_H - 4, 2, theme->accent2);
        }
        int label_w = launcher_text_sized(LAUNCHER_SIZE_LARGE, left + 4, y, 0, QUICK_ROW_H, ALIGN_LEFT,
            on ? STL_DEFAULT : STL_GRAY, quick_label(row->kind));
        const char *value = quick_value(menu, row->kind);
        int cy = y + QUICK_ROW_H / 2;
        if (value) {
            // Right-aligned, cut short to fit beside its name; arrows either side when selected.
            int end = right - (on ? 20 : 4);
            int room = end - (left + 4 + label_w + 30);
            int w = launcher_text_sized_width(LAUNCHER_SIZE_LARGE, value);
            w = (w > room) ? room : w;
            launcher_text_sized(LAUNCHER_SIZE_LARGE, end - w, y, w + 2, QUICK_ROW_H, ALIGN_LEFT, on ? STL_ACCENT : STL_GRAY, value);
            if (on) {
                launcher_draw_arrow(end - w - 14, cy, 11, false, theme->accent);
                launcher_draw_arrow(right - 6, cy, 11, true, theme->accent);
            }
        } else if (on) {
            launcher_draw_arrow(right - 6, cy, 11, true, theme->accent);
        }
    }
    rdpq_set_scissor(0, 0, quick_target_w(), quick_target_h());
    if (quick_scroll < max_scroll) {
        launcher_draw_arrow((left + right) / 2, QUICK_BOTTOM + 4, 7, true, theme->text_muted);
    }
}

static void quick_draw (menu_t *menu) {
    float t = quick_progress();
    if (t <= 0.0f) {
        quick_draw_logged = false;
        quick_draw_frames = 0;
        quick_draw_us = 0;
        quick_image_release();
        return;
    }
    uint64_t started = get_ticks_us();
    const theme_t *theme = theme_current();
    int x0 = LAUNCHER_SCREEN_W - (int)(QUICK_W * t + 0.5f);
    // The screen behind fades back; only the part the panel leaves showing (the blend costs the RDP every pixel).
    color_t dim = theme->bg;
    dim.a = (uint8_t)(MENU_BACKDROP_ALPHA * t);
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(dim);
        rdpq_fill_rectangle(0, 0, launcher_px(x0), display_get_height());
    rdpq_mode_pop();
    quick_build_rows();
    // The panel as an image: drawn again only when it changes (a press, a song, a glide), copied to the screen each
    // frame. Drawn every frame it cost ~14 ms on top of the screen behind (30-40 fps while it slid in).
    int image_w = launcher_px(QUICK_W) - launcher_px(0);
    if (quick_image_block && !launcher_covers_lent(quick_image_block)) {
        quick_image_block = NULL;   // The cover memory was reset.
        quick_image_fresh = false;
    }
    if (!quick_image_block) {
        quick_image_block = launcher_covers_borrow((uint32_t)(image_w * QUICK_IMAGE_H * 2));
        if (quick_image_block) {
            quick_image = surface_make_linear(quick_image_block, FMT_RGBA16, image_w, QUICK_IMAGE_H);
            quick_image_fresh = false;
        }
    }
    int pad_right = QUICK_PAD + (launcher_is_widescreen() ? 6 : 0);
    if (quick_image_block) {
        bool gliding = fabsf(quick_scroll_pos - (float)quick_scroll) >= 0.5f;
        if (!quick_image_fresh || gliding || sound_bgm_track_generation() != quick_image_song) {
            rdpq_attach(&quick_image, NULL);
            quick_target_width = image_w;
            quick_target_height = QUICK_IMAGE_H;
            ui_components_box_draw(0, 0, image_w, QUICK_IMAGE_H, theme->panel);
            ui_components_box_draw(0, 0, 1, QUICK_IMAGE_H, theme->line);
            quick_draw_rows(menu, QUICK_PAD, QUICK_W - pad_right);
            quick_target_width = quick_target_height = 0;
            rdpq_detach();
            quick_image_fresh = true;
            quick_image_song = sound_bgm_track_generation();
        }
        rdpq_mode_push();
            rdpq_set_mode_copy(false);
            rdpq_tex_blit(&quick_image, launcher_px(x0), 0, NULL);
        rdpq_mode_pop();
        ui_components_box_draw(launcher_px(x0), QUICK_IMAGE_H, display_get_width(), display_get_height(), theme->panel);
        ui_components_box_draw(launcher_px(x0), QUICK_IMAGE_H, launcher_px(x0) + 1, display_get_height(), theme->line);
    } else {
        // No memory to spare for the image: drawn straight to the screen.
        ui_components_box_draw(launcher_px(x0), 0, display_get_width(), display_get_height(), theme->panel);
        ui_components_box_draw(launcher_px(x0), 0, launcher_px(x0) + 1, display_get_height(), theme->line);
        quick_draw_rows(menu, x0 + QUICK_PAD, x0 + QUICK_W - pad_right);
    }

    if (quick_open && t >= 1.0f) {
        static const launcher_hint_t select_hints[] = {
            { LAUNCHER_BUTTON_A, "A", "Select" },
            { LAUNCHER_BUTTON_B, "B", "Close" },
        };
        static const launcher_hint_t change_hints[] = {
            { LAUNCHER_BUTTON_LEFT_RIGHT, "", "Change" },
            { LAUNCHER_BUTTON_B, "B", "Close" },
        };
        launcher_hide_footer_line();
        launcher_draw_footer(quick_is_value(quick_rows[quick_row].kind) ? change_hints : select_hints, 2);
    }

    // Settled: fully in, and the list's glide has landed.
    bool settled = quick_open && t >= 1.0f && fabsf(quick_scroll_pos - (float)quick_scroll) < 0.5f;
    quick_settled_frames = settled ? quick_settled_frames + 1 : 0;
    quick_drawn_ms = get_ticks_ms();
    quick_drawn_song = sound_bgm_track_generation();
    quick_frames_drawn++;

    // For the performance log: what the panel costs a frame, once per opening.
    if (quick_open && t >= 1.0f && !quick_draw_logged && quick_cost_logs < 3) {
        quick_draw_us += (uint32_t)(get_ticks_us() - started);
        if (++quick_draw_frames == 30) {
            perf_log_event("quick menu: %.1f ms a frame to draw", quick_draw_us / 30000.0f);
            quick_draw_logged = true;
            quick_cost_logs++;
        }
    }
}

void launcher_menu_init (void) {
}

static menu_t *quick_owner = NULL;

void launcher_menu_show (menu_t *menu, path_t *rom) {
    prepare_menus(menu, rom);
    quick_owner = menu;
    quick_mode = menu->mode;
    quick_open = true;
    quick_changed_ms = get_ticks_ms();
    quick_settled_frames = 0;
    quick_frames_drawn = 0;
    quick_free_groups();
    quick_folder = get_folder_selection(menu);
    quick_filter_field = launcher_filter_field();
    quick_scroll = 0;
    quick_scroll_pos = 0.0f;
    quick_row = 0;
    quick_col = 0;
    quick_build_rows();
    settings_defer_saves(true);
    sound_play_effect(SFX_POP);
}


bool launcher_menu_process (menu_t *menu) {
    quick_owner = menu;
    return quick_process(menu);
}

void launcher_menu_draw (void) {
    if (quick_owner && quick_owner->mode != quick_mode) {
        quick_close(false);
    }
    if (quick_owner) {
        quick_draw(quick_owner);
    }
}
