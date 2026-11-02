/**
 * @file launcher_ui.h
 * @brief Shared pieces of the console-style launcher: cover cache, layout and drawing.
 * @ingroup menu
 */

#ifndef LAUNCHER_UI_H__
#define LAUNCHER_UI_H__

#include <stdbool.h>

#include <libdragon.h>

#include "fonts.h"
#include "menu_state.h"

/*
 * Screen layout, in logical coordinates. The logical screen is 640 wide in 4:3 and 853 wide in
 * 16:9 (the launcher helpers squeeze it into the real 640-pixel frame). Heights are real pixels.
 */
#define LAUNCHER_SCREEN_W       (launcher_screen_width())
#define LAUNCHER_CENTER_X       (launcher_screen_width() / 2)
#define LAUNCHER_X0             (launcher_x0())     /**< Left edge of content */
#define LAUNCHER_X1             (launcher_x1())     /**< Right edge of content */
#define LAUNCHER_WIDTH          (LAUNCHER_X1 - LAUNCHER_X0)
#define LAUNCHER_HEADER_Y       (24)    /**< Top tabs */
#define LAUNCHER_SUBHEADER_Y    (60)    /**< Category tabs */
#define LAUNCHER_CONTENT_Y      (98)    /**< First row of covers */
#define LAUNCHER_CONTENT_Y1     (410)   /**< Bottom of the cover area */
#define LAUNCHER_FOOTER_Y       (420)   /**< Separator above the button hints */
#define LAUNCHER_TITLE_SPACE    (22)    /**< Room under a tile for its title */
#define LAUNCHER_RING           (3)     /**< Selection ring thickness */

/* Full-size cover dimensions, as stored on the SD card */
#define LAUNCHER_COVER_W        (158)
#define LAUNCHER_COVER_H        (112)

/** @brief Controller buttons shown in the hints bar (sets the icon color). */
typedef enum {
    LAUNCHER_BUTTON_A,
    LAUNCHER_BUTTON_B,
    LAUNCHER_BUTTON_START,
    LAUNCHER_BUTTON_C_UP_DOWN,      /**< Yellow "C" with up/down arrows */
    LAUNCHER_BUTTON_C_LEFT_RIGHT,   /**< Yellow "C" with left/right arrows */
    LAUNCHER_BUTTON_OTHER,
} launcher_button_t;

/** @brief One entry of the hints bar: a button icon and what it does. */
typedef struct {
    launcher_button_t kind;
    const char *button;     /**< Text on the icon, e.g. "A" or "L R" */
    const char *label;      /**< What the button does */
} launcher_hint_t;

/** @brief Top-level launcher tabs. */
typedef enum {
    LAUNCHER_TAB_HOME = 0,
    LAUNCHER_TAB_LIBRARY = 1,
} launcher_tab_t;

/* ------------------------------------------------------------------ 16:9 */

/**
 * @brief Draw the launcher for a 16:9 display (squeezed horizontally, for the TV to stretch back).
 *
 * Views set this from the settings at the start of every frame.
 */
void launcher_set_widescreen(bool enabled);

/** @brief Whether the launcher is drawing for 16:9. */
bool launcher_is_widescreen(void);

/** @brief Logical screen width: 640 in 4:3, 853 in 16:9. */
int launcher_screen_width(void);

/** @brief Logical left edge of content (inside the TV-safe margin). */
int launcher_x0(void);

/** @brief Logical right edge of content. */
int launcher_x1(void);

/** @brief The font to use for a launcher font id: its squeezed variant in 16:9. */
uint8_t launcher_font(uint8_t font);

/** @brief Rounded rectangle in real framebuffer pixels (for stock dialogs, never squeezed). */
void launcher_fill_rounded_px(int x0, int y0, int x1, int y1, int radius, color_t color);

/* ------------------------------------------------------------------ covers */

/**
 * @brief Get a decoded cover, starting a decode if it isn't cached yet.
 *
 * Covers decode one at a time in the background (png_decoder_poll in the main loop);
 * call this every frame for each visible cover, most important first.
 *
 * @param path Cover PNG path (NULL returns NULL).
 * @param max_w Largest width to decode to (aspect ratio is kept).
 * @param max_h Largest height to decode to.
 * @param pending Set to true while the cover is still loading.
 * @return The image, or NULL when it isn't ready or failed.
 */
surface_t *launcher_cover_get(const char *path, int max_w, int max_h, bool *pending);

/**
 * @brief Show cartridge label art in place of box fronts where the cover packs have it.
 *
 * Views set this from the settings at the start of every frame.
 */
void launcher_set_cartridge_art(bool enabled);

/** @brief Mark the start of a frame (for least-recently-used eviction). */
void launcher_covers_begin_frame(void);

/** @brief Abort any decode and free every cached cover. */
void launcher_covers_reset(void);

/* ------------------------------------------------------------------ drawing */

/**
 * @brief Draw one line of text with its top-left (or top-center/right) at x, y.
 *
 * @return Width of the drawn text in pixels.
 */
int launcher_text(int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *fmt, ...);

/**
 * @brief Draw a file or folder name as plain text ('$' and '^' are not treated as text codes).
 *
 * @return Width of the drawn text in pixels.
 */
int launcher_name(int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *name);

/**
 * @brief Draw a game title in the larger title font.
 *
 * @return Width of the drawn text in pixels.
 */
int launcher_title(int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *name);

/** @brief Draw a page title in the header (for screens opened from the launcher). */
void launcher_draw_page_title(const char *title);

/** @brief Draw the screen background (theme color, or the user's background image). */
void launcher_draw_background(void);

/** @brief A block of wrapped text laid out once and drawn every frame (e.g. a game description). */
typedef struct launcher_paragraph_s launcher_paragraph_t;

/** @brief Lay out text wrapped to a logical width, in the launcher font. */
launcher_paragraph_t *launcher_paragraph_build(const char *text, int width);

/** @brief Height of the laid-out text in pixels. */
int launcher_paragraph_height(launcher_paragraph_t *paragraph);

/**
 * @brief Draw laid-out text with its top at (x, y), showing only the rows between clip_y0 and clip_y1.
 */
void launcher_paragraph_draw(launcher_paragraph_t *paragraph, int x, int y, int clip_y0, int clip_y1);

/** @brief Free laid-out text (NULL is fine). */
void launcher_paragraph_free(launcher_paragraph_t *paragraph);

/**
 * @brief Note controller input and start attract mode after the idle time in settings.
 *
 * Call once per frame from views that should fall into attract mode (Home, Library).
 */
void launcher_track_idle(menu_t *menu);

/** @brief Restart the idle timer (e.g. when leaving attract mode). */
void launcher_reset_idle(void);

/** @brief The game the About page shows (set by the Start menu's "About this game"). */
path_t *launcher_about_rom(void);

/** @brief Draw the "new games found" notice under the header, when there are any. */
void launcher_draw_new_games_notice(void);

/** @brief Draw the background, the Home / Library tabs and the clock. */
void launcher_draw_header(menu_t *menu, launcher_tab_t active);

/** @brief Draw the separator and a right-aligned row of button icons with labels. */
void launcher_draw_footer(const launcher_hint_t *hints, int count);

/** @brief Fill a rectangle with rounded corners. */
void launcher_fill_rounded(int x0, int y0, int x1, int y1, int radius, color_t color);

/**
 * @brief Draw a selection ring around a rounded rectangle (draw it before the content).
 */
void launcher_draw_ring(int x0, int y0, int x1, int y1, int radius);

/**
 * @brief Draw a category pill; active pills are filled with the accent color.
 *
 * @return Width of the pill in pixels.
 */
int launcher_draw_pill(int x, int y, const char *name, bool active);

/**
 * @brief Draw a cover tile: the image (fitted and centered), or a placeholder with the title.
 *
 * @param x Left edge.
 * @param y Top edge.
 * @param w Tile width.
 * @param h Tile height.
 * @param image Decoded cover or NULL.
 * @param pending True while the cover is loading (draws an empty tile instead of the title).
 * @param title Game title.
 * @param selected Draw the selection ring.
 * @param behind Color behind the tile, used to round the cover's corners.
 */
void launcher_draw_tile(int x, int y, int w, int h, surface_t *image, bool pending, const char *title, bool selected, color_t behind);

/**
 * @brief Draw a cover scaled to fit inside a box (used by the carousel).
 *
 * @param behind Color behind the cover, used to round its corners.
 */
void launcher_draw_cover_scaled(surface_t *image, int x, int y, int w, int h, color_t behind);

/* ------------------------------------------------------------------ navigation */

/**
 * @brief Leave the launcher for another screen, remembering where to come back to.
 *
 * Screens that would return to the file browser return to this launcher view instead.
 */
void launcher_leave(menu_t *menu, menu_mode_t next_mode);

/**
 * @brief Switch to a theme now and save it: re-colors fonts and re-tints any background image.
 */
void launcher_apply_theme(menu_t *menu, int index);

/**
 * @brief Open the Start menu (favorite, sort, filter, quick settings, file browser, settings, info screens).
 *
 * @param selected_rom The highlighted game's ROM (for "Add to favorites"); the menu keeps it. May be NULL.
 */
void launcher_menu_show(menu_t *menu, path_t *selected_rom);

/** @brief Requests from the Start menu that the library view carries out. */
typedef enum {
    LAUNCHER_REQUEST_NONE,
    LAUNCHER_REQUEST_SURPRISE,
    LAUNCHER_REQUEST_FILTER,
} launcher_request_t;

/** @brief Take (and clear) the pending Start menu request. */
launcher_request_t launcher_take_request(void);

/** @brief Whether favorites changed since last asked (clears the flag). */
bool launcher_take_favorites_changed(void);

/** @brief Note that favorites may have changed (cached favorite flags are worked out again). */
void launcher_favorites_touched(void);

/** @brief Counter that changes whenever favorites may have changed. */
uint32_t launcher_favorites_generation(void);

/** @brief Whether a ROM is in the favorites. */
bool launcher_is_favorite(menu_t *menu, path_t *rom);

/** @brief Draw the favorite star in the top-right corner of a tile. */
void launcher_draw_favorite_star(int x, int y, int w);

/**
 * @brief Draw a short line in the small font (game details, filter rows).
 *
 * @return Logical width of the drawn text.
 */
int launcher_small(int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *text);

/** @brief Handle input for the Start menu. Returns true while it is open. */
bool launcher_menu_process(menu_t *menu);

/** @brief Draw the Start menu if it is open. */
void launcher_menu_draw(void);

/** @brief Reset the Start menu (call from a view's init). */
void launcher_menu_init(void);

#endif /* LAUNCHER_UI_H__ */
