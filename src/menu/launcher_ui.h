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

/* Screen layout (640x480, inside the TV-safe area) */
#define LAUNCHER_X0             (32)    /**< Left edge of content */
#define LAUNCHER_X1             (608)   /**< Right edge of content */
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

/** @brief Top-level launcher tabs. */
typedef enum {
    LAUNCHER_TAB_HOME = 0,
    LAUNCHER_TAB_LIBRARY = 1,
} launcher_tab_t;

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

/** @brief Draw the background, the Home / Library tabs and the clock. */
void launcher_draw_header(menu_t *menu, launcher_tab_t active);

/** @brief Draw the separator and a right-aligned line of button hints. */
void launcher_draw_footer(const char *hints);

/** @brief Draw a selection ring just outside a rectangle. */
void launcher_draw_ring(int x0, int y0, int x1, int y1);

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
 */
void launcher_draw_tile(int x, int y, int w, int h, surface_t *image, bool pending, const char *title, bool selected);

/**
 * @brief Draw a cover scaled to fit inside a box (used by the carousel).
 */
void launcher_draw_cover_scaled(surface_t *image, int x, int y, int w, int h);

/* ------------------------------------------------------------------ navigation */

/**
 * @brief Leave the launcher for another screen, remembering where to come back to.
 *
 * Screens that would return to the file browser return to this launcher view instead.
 */
void launcher_leave(menu_t *menu, menu_mode_t next_mode);

/** @brief Open the Start menu (file browser, settings, info screens). */
void launcher_menu_show(void);

/** @brief Handle input for the Start menu. Returns true while it is open. */
bool launcher_menu_process(menu_t *menu);

/** @brief Draw the Start menu if it is open. */
void launcher_menu_draw(void);

/** @brief Reset the Start menu (call from a view's init). */
void launcher_menu_init(void);

#endif /* LAUNCHER_UI_H__ */
