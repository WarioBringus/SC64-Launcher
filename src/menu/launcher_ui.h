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

/* The carousel's covers and the shelf's open box: a third larger than full size, from their own
   pack (pc-tools/coverpack UP_BOXES, enlarged on the PC; no cover art is this large) */
#define LAUNCHER_SHOWCASE_W     (210)
#define LAUNCHER_SHOWCASE_H     (150)

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

/** @brief Spines in the spine pack (pc-tools/coverpack SPINE_PACKS): 210 tall and as thick as the
 * box, up to LAUNCHER_SPINE_MAX_W; LAUNCHER_SPINE_W when the cover index gives no width. */
#define LAUNCHER_SPINE_W        (30)
#define LAUNCHER_SPINE_MAX_W    (64)
#define LAUNCHER_SPINE_H        (210)

/**
 * @brief A game's box spine for the shelf layout, by its front cover's path. From the spine pack
 * only: NULL when there is none (pending says whether it is still to be read).
 */
surface_t *launcher_spine_get(const char *front_path, bool *pending);

/** @brief Draw a spine resized to w x h (or, without one, a plain panel) with slightly rounded corners. */
void launcher_draw_spine(int x, int y, int w, int h, surface_t *image);

/**
 * @brief Read a cover on screen again at a larger pack size (max_w x max_h), straight from its pack.
 * Returns a surface the caller frees, or NULL when the pack has no such cover.
 */
surface_t *launcher_cover_larger(surface_t *shown, int max_w, int max_h);

/**
 * @brief For a cover from a 16:9 pack: the squeezed image to copy 1:1 (the surface handed out has the
 * 4:3 size for layout, and must only be drawn through the launcher's helpers). NULL for other covers.
 */
surface_t *launcher_cover_physical(surface_t *image);

/** @brief Mark the start of a frame (for least-recently-used eviction). */
void launcher_covers_begin_frame(void);

/** @brief Abort any decode and free every cached cover. */
void launcher_covers_reset(void);

/**
 * @brief While held, covers not yet in memory are read at most one every other frame (the rest show as
 * placeholders until their turn): used during fast scrolling, when a pack read every frame would make
 * the scrolling skip. Released each frame.
 */
void launcher_covers_hold(bool hold);

/**
 * @brief Open the next cover pack not opened yet (one a call), so the first view to use it doesn't wait for
 * the open (~55 ms each). prefix is the card's ("sd:/"); the grid's packs for tile_size come first, then
 * the carousel's, the shelf's and the rest. True once every pack has been opened (or tried).
 */
bool launcher_covers_open_ahead(const char *storage_prefix, int tile_size);

/**
 * @brief Covers asked for from now on are read ahead of need (rows off screen, other folders): they
 * are read at most one every few frames, so reading ahead never slows the screen. Off each frame.
 */
void launcher_covers_background(bool background);

/** @brief Whether the selection (key) is changing quickly, e.g. a direction held down. */
bool launcher_scrolling_fast(int key);

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
 * @brief The box a selected game's title needs under its tile (centred on cx, at least min_w wide,
 * kept on screen) so it shows in full. name must stay in memory (its width is remembered).
 */
void launcher_title_box(const char *name, int cx, int min_w, int *x, int *w);

/** @brief Text size: Large for game titles and details (set by Home and the Library each frame). */
void launcher_set_large_text(bool enabled);

/** @brief Room under a tile for its title (taller with large text). */
int launcher_title_space(void);

/** @brief A game's title under its cover, in the game-title size. */
int launcher_game_name(int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *name);

/** @brief A game's details line ("Racing  ·  1997  ·  ..."), in the game-details size. */
int launcher_game_details(int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *text);

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

/**
 * @brief Full-screen loading card in the launcher's style: the game's name, an optional details line,
 * a rounded progress bar and a status line ("Loading 45%" when status is NULL).
 */
void launcher_draw_loading(const char *title, const char *details, float progress, const char *status);

/** @brief Note the cover in the focused tile this frame (the one a quick launch grows from). */
void launcher_note_focused_cover(surface_t *image, int x, int y, int w, int h);

/** @brief As launcher_note_focused_cover, for a cover drawn enlarged by zoom. */
void launcher_note_focused_cover_zoomed(surface_t *image, int x, int y, int w, int h, float zoom);

/** @brief Forget the focused cover; called at the start of every launcher frame. */
void launcher_clear_focused_cover(void);

/** @brief Drop the copy of the launched game's cover shown on the loading card. */
void launcher_launch_cover_free(void);

/** @brief Draw the background, the Home / Library tabs and the clock. */
void launcher_draw_header(menu_t *menu, launcher_tab_t active);

/** @brief How the next launcher_draw_header draws the tabs and clock (Home's styles). */
typedef enum {
    LAUNCHER_HEADER_TABS,       /**< Tabs at the top left, the clock at the right (every screen's own) */
    LAUNCHER_HEADER_CORNER,     /**< "Home  ·  Library  ·  clock" small in the top-right corner */
    LAUNCHER_HEADER_BAR,        /**< A full-width bar: the tab's name in capitals and the clock */
    LAUNCHER_HEADER_NONE,       /**< Only the background */
} launcher_header_style_t;

/** @brief Use this header style for the next launcher_draw_header only. */
void launcher_set_header_style(launcher_header_style_t style);

/** @brief Height of the bar LAUNCHER_HEADER_BAR draws. */
#define LAUNCHER_HEADER_BAR_H   (60)

/**
 * @brief Draw the "Now playing" note while a track has just started, else the new games notice, in a box of
 * the launcher's small text (header styles without room for them draw them through this).
 */
void launcher_draw_notes(int x, int y, int width, rdpq_align_t align);

/** @brief Leave out the separator line above the hints in the next launcher_draw_footer. */
void launcher_hide_footer_line(void);

/** @brief The clock's text ("8:52 PM"), or an empty string when the time isn't known. */
void launcher_clock_text(menu_t *menu, char *buffer, size_t size);

/**
 * @brief Fill a panel from the left edge of the screen with a slanted right side: x_top wide at y0,
 * x_bottom wide at y1 (logical x).
 */
void launcher_fill_slanted(int x_top, int x_bottom, int y0, int y1, color_t color);

/** @brief Draw the separator and a right-aligned row of button icons with labels. */
void launcher_draw_footer(const launcher_hint_t *hints, int count);

/** @brief Fill a rectangle with rounded corners. */
void launcher_fill_rounded(int x0, int y0, int x1, int y1, int radius, color_t color);

/**
 * @brief Draw a selection ring around a cover tile (draw it before the content). In 16:9 its right side
 * meets the cover's squeezed width; launcher_draw_focus_ring is for anything else, such as a card.
 */
void launcher_draw_ring(int x0, int y0, int x1, int y1, int radius);

/** @brief Logical x to framebuffer x (for scissor rectangles). */
int launcher_px(int x);

/** @brief Move a value part of the way to its target (one frame of a glide); snaps when close. */
float launcher_ease(float current, float target);

/** @brief Draw the selection ring gliding toward this rectangle (it snaps after launcher_focus_reset). */
void launcher_draw_focus_ring(int x0, int y0, int x1, int y1, int radius);

/** @brief The same for a tile's rectangle. */
void launcher_draw_focus_ring_tile(int x, int y, int w, int h);

/** @brief Make the next selection ring appear in place rather than glide (e.g. a screen opened). */
void launcher_focus_reset(void);

/** @brief How far the selected tile is lifted, rising briefly whenever the selection (key) changes. */
int launcher_focus_lift(int key);

/**
 * @brief Draw a category pill; active pills are filled with the accent color.
 *
 * @return Width of the pill in pixels.
 */
int launcher_draw_pill(int x, int y, const char *name, bool active);

/** @brief Width launcher_draw_pill would take for a name. */
int launcher_pill_width(const char *name);

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

/** @brief As launcher_draw_tile, with the cover drawn enlarged by zoom (the tile is sized for that). */
void launcher_draw_tile_zoomed(int x, int y, int w, int h, surface_t *image, bool pending, const char *title, bool selected, color_t behind, float zoom);

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

/** @brief Animate the switch from one launcher screen to another (Home/Library slide, About fades). */
void launcher_transition(menu_mode_t from, menu_mode_t to);

/**
 * @brief Start drawing a screen's content (launcher_draw_header calls it): content is offset while a
 * transition slides it in. launcher_draw_footer ends it and fades the content in.
 */
void launcher_transition_content_begin(int y0);

/** @brief Whether a screen is still sliding or fading in (read-ahead waits for it). */
bool launcher_transition_running(void);

/** @brief Fade the content in again on the same screen (e.g. after switching the library's view). */
void launcher_transition_fade(void);

/** @brief Write settings a quick launch put off until the menu music has faded. */
void launcher_save_pending_settings(menu_t *menu);

/**
 * @brief Switch to a theme now and save it: re-colors fonts and re-tints any background image.
 */
void launcher_apply_theme(menu_t *menu, int index);

/**
 * @brief Open the Start menu: Surprise me, then Browse, Look, Library and System groups.
 *
 * @param selected_rom The highlighted game's ROM; the menu keeps it. May be NULL.
 */
void launcher_menu_show(menu_t *menu, path_t *selected_rom);

/**
 * @brief Open the Game menu (Z): favorite, about, game options, hide.
 *
 * @param selected_rom The highlighted game's ROM; the menu keeps it. Nothing opens for NULL.
 */
void launcher_game_menu_show(menu_t *menu, path_t *selected_rom);

/** @brief Requests from the Start menu that the library view carries out. */
typedef enum {
    LAUNCHER_REQUEST_NONE,
    LAUNCHER_REQUEST_SURPRISE,
    LAUNCHER_REQUEST_FILTER,
    LAUNCHER_REQUEST_SEARCH,
    LAUNCHER_REQUEST_HIDDEN,
    LAUNCHER_REQUEST_FOLDERS,
    LAUNCHER_REQUEST_CATEGORY,      /**< Show the folder launcher_requested_category() returns */
} launcher_request_t;

/** @brief Take (and clear) the pending Start menu request. */
launcher_request_t launcher_take_request(void);

/** @brief The folder (category index) chosen in Start > Folder, for LAUNCHER_REQUEST_CATEGORY. */
int launcher_requested_category(void);

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

/** @brief Forget every laid-out text (a font registered under an id it used has changed). */
void launcher_text_cache_clear(void);

/** @brief Cover the whole screen in black, alpha 255 (opaque) to 0 (nothing drawn). */
void launcher_draw_screen_fade(int alpha);

/** @brief The favorite star centered on logical (cx, cy), e.g. at the foot of a shelf spine. */
void launcher_draw_favorite_mark(int cx, int cy);

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
