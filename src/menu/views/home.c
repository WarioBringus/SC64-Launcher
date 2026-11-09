/**
 * @file home.c
 * @brief Launcher home screen: resume the last game, recently played and favorites.
 * @ingroup view
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "views.h"
#include "../bookkeeping.h"
#include "../launcher_ui.h"
#include "../perf_log.h"
#include "../library.h"
#include "../sound.h"
#include "../theme.h"

#define RESUME_CARD_H       (140)
#define RESUME_COVER_X      (LAUNCHER_X0 + 14)
#define RESUME_TEXT_X       (LAUNCHER_X0 + 194)
#define ROW_LABEL_H         (22)
#define ROW_TILE_W          (74)
#define ROW_TILE_H          (52)
#define ROW_TILE_STEP       (82)
#define ROW_H               (100)
#define ROW_GAP             (14)
#define ROW_VISIBLE_TILES   (LAUNCHER_WIDTH / ROW_TILE_STEP)
#define MAX_ROW_ITEMS       (HISTORY_COUNT > FAVORITES_COUNT ? HISTORY_COUNT : FAVORITES_COUNT)
#define COVER_CACHE_SIZE    (HISTORY_COUNT + FAVORITES_COUNT)

#define CARD_RADIUS         (10)
#define CONTENT_TOP         (LAUNCHER_SUBHEADER_Y + 2)
#define CONTENT_BOTTOM      (412)
#define ROW_KINDS           (3)

static const launcher_hint_t hints_home[] = {
    { LAUNCHER_BUTTON_A, "A", "Play" },
    { LAUNCHER_BUTTON_OTHER, "Z", "Game" },
    { LAUNCHER_BUTTON_OTHER, "R", "Library" },
    { LAUNCHER_BUTTON_START, "S", "Menu" },
};

static const launcher_hint_t hints_empty[] = {
    { LAUNCHER_BUTTON_OTHER, "R", "Library" },
    { LAUNCHER_BUTTON_START, "S", "Menu" },
};

#define REVEAL_TIMEOUT_MS   (3000)  /* The first Home shows by then even if covers are still loading */
#define REVEAL_LOOKUPS      (64)    /* Cover lookups per frame while it's hidden */
#define REVEAL_FADE_MS      (250)   /* Then it fades in from black over this long */
#define REVEAL_STEADY_FRAMES (6)    /* ...once this many frames in a row came quickly (startup has settled) */
#define REVEAL_STEADY_MS    (25)
#define REVEAL_STEP_MS      (40)    /* A frame moves the fade on by at most this much, so a hitch pauses it */
#define REVEAL_CHECK_US     (40000) /* While hidden, the new-ROM check runs this long a frame (music fills ~140 ms ahead) */

#define HINT_COUNT(h) ((int)(sizeof(h) / sizeof((h)[0])))

typedef enum {
    ROW_RESUME,
    ROW_RECENT,
    ROW_FAVORITES,
} row_kind_t;

typedef struct {
    bool favorite;              /**< Index refers to favorite_items rather than history_items */
    int index;                  /**< Index into the bookkeeping list */
    bookkeeping_item_t *item;   /**< History/favorite entry */
    char *title;
} home_item_t;

/** @brief The ROM an entry points at. */
static path_t *item_path (home_item_t *h) {
    return h->item->primary_path;
}

typedef struct {
    row_kind_t kind;
    home_item_t items[MAX_ROW_ITEMS];
    int count;
    int selected;
    int scroll;
    float scroll_pos;           /**< Drawn scroll in pixels, gliding toward scroll */
} home_row_t;

/* ROM path -> cover path, kept across visits so covers are only looked up once. */
typedef struct {
    char *rom_path;
    char *cover_path;
} cover_lookup_t;

static home_row_t rows[ROW_KINDS];
static int row_count = 0;
static int row_selected = 0;
static int scroll_y = 0;
static float scroll_y_pos = 0.0f;       /* Drawn page scroll, gliding toward scroll_y */
static int scroll_y_ahead = 0;          /* Drawn minus target scroll: where things are headed is this much lower */
static bool motion_snap = true;
static cover_lookup_t cover_lookups[COVER_CACHE_SIZE];
static int cover_lookup_next = 0;


static void free_items (home_row_t *row) {
    for (int i = 0; i < row->count; i++) {
        free(row->items[i].title);
    }
    row->count = 0;
}

static void free_rows (void) {
    for (int r = 0; r < row_count; r++) {
        free_items(&rows[r]);
    }
    row_count = 0;
}

static void add_item (menu_t *menu, home_row_t *row, bookkeeping_item_t *item, bool favorite, int index) {
    if (item->bookkeeping_type == BOOKKEEPING_TYPE_EMPTY || !item->primary_path || row->count >= MAX_ROW_ITEMS) {
        return;
    }
    home_item_t *h = &row->items[row->count++];
    h->favorite = favorite;
    h->index = index;
    h->item = item;
    h->title = library_title_from_path(menu, item->primary_path);
}

static void build_rows (menu_t *menu) {
    int previous_selected[ROW_KINDS] = { 0, 0, 0 };
    for (int r = 0; r < row_count; r++) {
        previous_selected[rows[r].kind] = rows[r].selected;
    }
    free_rows();

    bookkeeping_t *b = &menu->bookkeeping;
    home_row_t built[ROW_KINDS];
    memset(built, 0, sizeof(built));

    built[ROW_RESUME].kind = ROW_RESUME;
    add_item(menu, &built[ROW_RESUME], &b->history_items[0], false, 0);

    built[ROW_RECENT].kind = ROW_RECENT;
    // With the resume card shown, the first history entry is already on screen.
    for (int i = menu->settings.launcher_home_resume ? 1 : 0; i < HISTORY_COUNT; i++) {
        add_item(menu, &built[ROW_RECENT], &b->history_items[i], false, i);
    }

    built[ROW_FAVORITES].kind = ROW_FAVORITES;
    for (int i = 0; i < FAVORITES_COUNT; i++) {
        add_item(menu, &built[ROW_FAVORITES], &b->favorite_items[i], true, i);
    }

    bool show[ROW_KINDS] = {
        menu->settings.launcher_home_resume,
        menu->settings.launcher_home_recent,
        menu->settings.launcher_home_favorites,
    };
    for (int k = 0; k < ROW_KINDS; k++) {
        if (show[k] && built[k].count > 0) {
            rows[row_count] = built[k];
            rows[row_count].selected = previous_selected[k];
            if (rows[row_count].selected >= rows[row_count].count) {
                rows[row_count].selected = rows[row_count].count - 1;
            }
            rows[row_count].scroll = 0;
            row_count++;
        } else {
            free_items(&built[k]);
        }
    }

    if (row_selected >= row_count) {
        row_selected = row_count > 0 ? row_count - 1 : 0;
    }
}

/**
 * @brief Cover path for a history/favorite entry. Looks up at most one new ROM per frame.
 */
static const char *item_cover (menu_t *menu, home_item_t *h, int *lookups_left, bool *pending) {
    const char *rom = path_get(item_path(h));
    for (int i = 0; i < COVER_CACHE_SIZE; i++) {
        if (cover_lookups[i].rom_path && strcmp(cover_lookups[i].rom_path, rom) == 0) {
            *pending = false;
            return cover_lookups[i].cover_path;
        }
    }
    if (*lookups_left <= 0) {
        *pending = true;
        return NULL;
    }
    (*lookups_left)--;

    cover_lookup_t *slot = &cover_lookups[cover_lookup_next];
    cover_lookup_next = (cover_lookup_next + 1) % COVER_CACHE_SIZE;
    free(slot->rom_path);
    free(slot->cover_path);
    slot->rom_path = strdup(rom);
    slot->cover_path = library_find_cover(menu, item_path(h));
    *pending = false;
    return slot->cover_path;
}

/*
 * The first Home after power-on stays hidden behind black while it loads every cover it shows (and the
 * one-off startup work runs), so it appears complete rather than filling in.
 */
typedef enum {
    REVEAL_HIDDEN,
    REVEAL_FADING,
    REVEAL_SHOWN,
} reveal_t;

static reveal_t reveal = REVEAL_HIDDEN;
static uint64_t reveal_since_ms = 0;
static int reveal_frames = 0;
static int reveal_steady = 0;           /* Quick frames in a row while hidden */
static bool folders_ready = false;      /* The other folders are listed and sorted (view_library_prepare_folders) */
static uint64_t reveal_last_ms = 0;     /* When the last frame was drawn */
static float reveal_fade_ms = 0.0f;     /* How far the fade has got */
static int fade_frames = 0;             /* For the performance log: the fade's frames... */
static uint64_t fade_slowest_ms = 0;    /* ...its slowest... */
static uint64_t fade_started_ms = 0;    /* ...and when it began */
static bool covers_pending = false;     /* A cover drawn this frame is still loading */

static surface_t *item_image (menu_t *menu, home_item_t *h, int w, int hgt, int *lookups_left, bool *pending) {
    const char *path = item_cover(menu, h, lookups_left, pending);
    surface_t *image = path ? launcher_cover_get(path, w, hgt, pending) : NULL;
    covers_pending |= *pending;
    return image;
}

static void launch (menu_t *menu, home_item_t *h) {
    if (h->favorite) {
        menu->load.load_favorite_id = h->index;
        menu->load.load_history_id = -1;
    } else {
        menu->load.load_history_id = h->index;
        menu->load.load_favorite_id = -1;
    }
    sound_play_effect(SFX_ENTER);
    bool disk = (h->item->bookkeeping_type == BOOKKEEPING_TYPE_DISK);
    menu->load.quick_start = !disk && menu->settings.launcher_quick_launch;
    launcher_leave(menu, disk ? MENU_MODE_LOAD_DISK : MENU_MODE_LOAD_ROM);
}

#define OPEN_AHEAD_IDLE_MS  (1000)  /* Home opens the cover packs ahead once the controller rests this long */

static uint64_t home_last_input_ms = 0;
static bool packs_open = false;         /* Every cover pack opened ahead (again after the launcher was left) */

static void process_styled(menu_t *menu, int style);

static void process (menu_t *menu) {
    if (menu->actions.go_up || menu->actions.go_down || menu->actions.go_left || menu->actions.go_right ||
        menu->actions.enter || menu->actions.back || menu->actions.settings || menu->actions.lz_context ||
        menu->actions.shoulder_r) {
        home_last_input_ms = get_ticks_ms();
    }
    if (launcher_menu_process(menu)) {
        return;
    }

    if (menu->actions.settings) {
        path_t *rom = NULL;
        if (row_count > 0 && rows[row_selected].count > 0) {
            rom = path_clone(item_path(&rows[row_selected].items[rows[row_selected].selected]));
        }
        launcher_menu_show(menu, rom);
        return;
    }
    if (menu->actions.shoulder_r) {
        sound_play_effect(SFX_SWISH);
        launcher_transition(menu->mode, MENU_MODE_LIBRARY);
        menu->next_mode = MENU_MODE_LIBRARY;
        return;
    }
    if (row_count == 0) {
        return;
    }
    if (menu->settings.launcher_home_style != LAUNCHER_HOME_CLASSIC) {
        process_styled(menu, menu->settings.launcher_home_style);
        return;
    }

    home_row_t *row = &rows[row_selected];

    if (menu->actions.go_up && row_selected > 0) {
        row_selected--;
        sound_play_effect(SFX_TICK);
    } else if (menu->actions.go_down && row_selected < row_count - 1) {
        row_selected++;
        sound_play_effect(SFX_TICK);
    } else if (menu->actions.go_left && row->selected > 0) {
        row->selected--;
        sound_play_effect(SFX_TICK);
    } else if (menu->actions.go_right && row->selected < row->count - 1) {
        row->selected++;
        sound_play_effect(SFX_TICK);
    } else if (menu->actions.enter && row->count > 0) {
        launch(menu, &row->items[row->selected]);
    } else if (menu->actions.trigger_z && row->count > 0) {
        launcher_game_menu_show(menu, path_clone(item_path(&row->items[row->selected])));
    }
}

static int draw_resume (menu_t *menu, home_row_t *row, int y, bool focused, int *lookups_left) {
    home_item_t *h = &row->items[0];
    if (focused) {
        launcher_draw_focus_ring(LAUNCHER_X0, y + scroll_y_ahead, LAUNCHER_X1, y + scroll_y_ahead + RESUME_CARD_H, CARD_RADIUS);
    }
    launcher_fill_rounded(LAUNCHER_X0, y, LAUNCHER_X1, y + RESUME_CARD_H, CARD_RADIUS, theme_current()->panel);

    bool pending;
    int cover_y = y + (RESUME_CARD_H - LAUNCHER_COVER_H) / 2;
    surface_t *image = item_image(menu, h, LAUNCHER_COVER_W, LAUNCHER_COVER_H, lookups_left, &pending);
    launcher_draw_tile(RESUME_COVER_X, cover_y, LAUNCHER_COVER_W, LAUNCHER_COVER_H, image, pending, h->title, false, theme_current()->panel);
    if (focused) {
        launcher_note_focused_cover(image, RESUME_COVER_X, cover_y, LAUNCHER_COVER_W, LAUNCHER_COVER_H);
    }

    // The folder the game lives in, e.g. "N64 USA".
    char folder[64] = "";
    path_t *dir = path_clone(item_path(h));
    path_pop(dir);
    snprintf(folder, sizeof(folder), "%s", path_last_get(dir));
    path_free(dir);

    int text_w = LAUNCHER_X1 - RESUME_TEXT_X - 14;
    launcher_text(RESUME_TEXT_X, y + 28, text_w, ALIGN_LEFT, STL_ACCENT, "Resume");
    char where[96];
    snprintf(where, sizeof(where), "Last played  -  %s", folder);
    launcher_title(RESUME_TEXT_X, y + 50, text_w, ALIGN_LEFT, STL_DEFAULT, h->title);
    launcher_name(RESUME_TEXT_X, y + 84, text_w, ALIGN_LEFT, STL_GRAY, where);

    char details[160];
    library_details_for_path(menu, item_path(h), details, sizeof(details));
    if (details[0]) {
        launcher_game_details(RESUME_TEXT_X, y + 108, text_w, ALIGN_LEFT, STL_GRAY, details);
    }
    return y + RESUME_CARD_H + ROW_GAP;
}

static int draw_row (menu_t *menu, home_row_t *row, int y, bool focused, int *lookups_left) {
    const char *label = (row->kind == ROW_FAVORITES) ? "Favorites" : "Recently played";
    launcher_text(LAUNCHER_X0, y, 0, ALIGN_LEFT, focused ? STL_DEFAULT : STL_GRAY, "%s", label);

    // Keep the highlighted tile inside the visible part of the row.
    if (row->selected < row->scroll) {
        row->scroll = row->selected;
    }
    if (row->selected >= row->scroll + ROW_VISIBLE_TILES) {
        row->scroll = row->selected - ROW_VISIBLE_TILES + 1;
    }

    bool highlight_only = menu->settings.launcher_titles_highlight_only;
    int tile_y = y + ROW_LABEL_H;

    // Glide sideways to the scroll position rather than jumping a tile.
    float target = (float)(row->scroll * ROW_TILE_STEP);
    row->scroll_pos = motion_snap ? target : launcher_ease(row->scroll_pos, target);
    int scroll_px = (int)lroundf(row->scroll_pos);
    int lift = focused ? launcher_focus_lift((int)(row - rows) * 1000 + row->selected) : 0;

    // The selected game's title shows in full; titles beside it that it would run into wait.
    int sel_tx = 0, sel_tw = 0;
    if (focused && row->count > 0) {
        int sel_x = LAUNCHER_X0 + row->selected * ROW_TILE_STEP - scroll_px;
        launcher_title_box(row->items[row->selected].title, sel_x + ROW_TILE_W / 2,
            ROW_TILE_W + 2 * (highlight_only ? 60 : 3), &sel_tx, &sel_tw);
    }
    for (int i = scroll_px / ROW_TILE_STEP; i < row->count && i <= scroll_px / ROW_TILE_STEP + ROW_VISIBLE_TILES; i++) {
        home_item_t *h = &row->items[i];
        int x = LAUNCHER_X0 + i * ROW_TILE_STEP - scroll_px;
        bool selected = focused && (i == row->selected);
        int item_y = selected ? tile_y - lift : tile_y;
        bool pending;
        surface_t *image = item_image(menu, h, ROW_TILE_W, ROW_TILE_H, lookups_left, &pending);
        launcher_draw_tile(x, item_y, ROW_TILE_W, ROW_TILE_H, image, pending, h->title, false, theme_current()->bg);
        if (selected) {
            launcher_note_focused_cover(image, x, item_y, ROW_TILE_W, ROW_TILE_H);
            launcher_draw_focus_ring_tile(LAUNCHER_X0 + (i - row->scroll) * ROW_TILE_STEP, item_y + scroll_y_ahead,
                ROW_TILE_W, ROW_TILE_H);
        }
        if (row->kind != ROW_FAVORITES && launcher_is_favorite(menu, item_path(h))) {
            launcher_draw_favorite_star(x, item_y, ROW_TILE_W);
        }
        if (selected || !highlight_only) {
            int tx = x - 3;
            int tw = ROW_TILE_W + 6;
            if (tx < LAUNCHER_X0) {
                tw -= LAUNCHER_X0 - tx;
                tx = LAUNCHER_X0;
            }
            if (tx + tw > LAUNCHER_X1) {
                tw = LAUNCHER_X1 - tx;
            }
            bool blocked = false;
            if (selected) {
                tx = sel_tx;
                tw = sel_tw;
            } else if (focused) {
                blocked = (tx < sel_tx + sel_tw + 6) && (tx + tw > sel_tx - 6);
            }
            if (!blocked && tw > 0) {
                launcher_game_name(tx, tile_y + ROW_TILE_H + LAUNCHER_RING + 3, tw, ALIGN_CENTER, selected ? STL_DEFAULT : STL_GRAY, h->title);
            }
        }
    }
    return y + ROW_H;
}

/** @brief Work done while Home is on screen: the library's covers and the cover packs, read ahead. */
static void home_background_work (menu_t *menu, int *lookups_left) {
    // Home's own covers come first, and the library's wait until the fade-in is over: their card
    // reads would make its frames uneven.
    if (*lookups_left > 0 && reveal == REVEAL_SHOWN) {
        launcher_covers_background(true);
        view_library_preload(menu, lookups_left);
        launcher_covers_background(false);
    }
    // The library's views each read their own pack; opening one cost the first switch into that view a
    // ~70-90 ms frame, so they are opened here while nothing moves.
    if (!packs_open && reveal == REVEAL_SHOWN && get_ticks_ms() - home_last_input_ms >= OPEN_AHEAD_IDLE_MS) {
        packs_open = launcher_covers_open_ahead(menu->storage_prefix, menu->settings.launcher_tile_size);
    }
}

/* ------------------------------------------------------------------ Home styles other than Classic */

/*
 * Each style lays its games out as slots (rectangles on screen, games off screen included) as it draws,
 * and the d-pad moves to the nearest slot in that direction: one way of moving for every layout.
 */
#define MAX_SLOTS           (ROW_KINDS * MAX_ROW_ITEMS)

typedef struct {
    int x, y, w, h;
    int row;                    /**< Index into rows */
    int index;                  /**< Game in that row */
} slot_t;

static slot_t slots[MAX_SLOTS];
static int slot_total = 0;

static void slot_add (int x, int y, int w, int h, int row, int index) {
    if (slot_total < MAX_SLOTS) {
        slots[slot_total++] = (slot_t) { x, y, w, h, row, index };
    }
}

static bool is_focus (int row, int index) {
    return row == row_selected && index == rows[row].selected;
}

/** @brief The focused game, or NULL when there are no games. */
static home_item_t *focused_item (void) {
    if (row_count == 0 || rows[row_selected].count == 0) {
        return NULL;
    }
    return &rows[row_selected].items[rows[row_selected].selected];
}

/** @brief How far apart two ranges are (0 when they overlap). */
static int range_gap (int a0, int a1, int b0, int b1) {
    return (b1 < a0) ? a0 - b1 : ((b0 > a1) ? b0 - a1 : 0);
}

/** @brief A color pct percent of the way from a to b (styles derive their shades from the theme). */
static color_t color_mix (color_t a, color_t b, int pct) {
    return RGBA32(a.r + (b.r - a.r) * pct / 100, a.g + (b.g - a.g) * pct / 100, a.b + (b.b - a.b) * pct / 100, 0xFF);
}

/** @brief Move the focus to the nearest slot in a direction; false when there is none. */
static bool slot_move (int dx, int dy) {
    const slot_t *c = NULL;
    for (int i = 0; i < slot_total && !c; i++) {
        if (is_focus(slots[i].row, slots[i].index)) {
            c = &slots[i];
        }
    }
    if (!c) {
        return false;
    }
    const slot_t *best = NULL;
    int best_score = INT_MAX;
    for (int i = 0; i < slot_total; i++) {
        const slot_t *s = &slots[i];
        if (s == c) {
            continue;
        }
        int ahead;
        if (dx > 0) {
            ahead = s->x - (c->x + c->w);
        } else if (dx < 0) {
            ahead = c->x - (s->x + s->w);
        } else if (dy > 0) {
            ahead = s->y - (c->y + c->h);
        } else {
            ahead = c->y - (s->y + s->h);
        }
        if (ahead < -4) {
            continue;   // Not in that direction.
        }
        int side = dx ? range_gap(c->y, c->y + c->h, s->y, s->y + s->h) : range_gap(c->x, c->x + c->w, s->x, s->x + s->w);
        int centre = dx ? abs((s->y + s->h / 2) - (c->y + c->h / 2)) : abs((s->x + s->w / 2) - (c->x + c->w / 2));
        int score = (ahead > 0 ? ahead : 0) + side * 4 + centre / 4;
        if (score < best_score) {
            best_score = score;
            best = s;
        }
    }
    if (!best) {
        return false;
    }
    row_selected = best->row;
    rows[row_selected].selected = best->index;
    return true;
}

/** @brief The folder a game lives in, e.g. "N64 USA". */
static void item_folder (home_item_t *h, char *buffer, size_t size) {
    path_t *dir = path_clone(item_path(h));
    path_pop(dir);
    snprintf(buffer, size, "%s", path_last_get(dir));
    path_free(dir);
}

/** @brief Clip drawing to the content area (the left edge stays at 0: see draw). */
static void clip_content (void) {
    int clip_x1 = launcher_px(LAUNCHER_X1 + LAUNCHER_RING + 2);
    rdpq_set_scissor(0, CONTENT_TOP - LAUNCHER_RING - 2,
        clip_x1 > display_get_width() ? display_get_width() : (clip_x1 < 1 ? 1 : clip_x1), CONTENT_BOTTOM + 2);
}

static void clip_none (void) {
    rdpq_set_scissor(0, 0, display_get_width(), display_get_height());
}

/**
 * @brief A large Home cover meant to show at the showcase size times want_zoom: read from the hi-res pack when the
 * card has it (drawn at *zoom, at most 1), else from the showcase pack enlarged.
 */
static surface_t *big_cover (menu_t *menu, home_item_t *h, float want_zoom, int *lookups_left, bool *pending, float *zoom) {
    if (launcher_covers_have_pack(menu->storage_prefix, LAUNCHER_HIRES_W, LAUNCHER_HIRES_H)) {
        *zoom = want_zoom * LAUNCHER_SHOWCASE_W / LAUNCHER_HIRES_W;
        return item_image(menu, h, LAUNCHER_HIRES_W, LAUNCHER_HIRES_H, lookups_left, pending);
    }
    *zoom = want_zoom;
    return item_image(menu, h, LAUNCHER_SHOWCASE_W, LAUNCHER_SHOWCASE_H, lookups_left, pending);
}

/* Minimal: no tabs; big type on the background, a short accent line under the focused title, and the focused
   game's cover large on the right. */
#define MIN_TOP             (30)
#define MIN_LABEL_H         (24)
#define MIN_ITEM_H          (30)
#define MIN_RESUME_H        (44)
#define MIN_SECTION_GAP     (22)
#define MIN_BAR_W           (26)
#define MIN_COVER_ZOOM      (1.25f)

static int min_scroll = 0;
static float min_scroll_pos = 0.0f;

static const char *min_label (row_kind_t kind) {
    return (kind == ROW_RESUME) ? "CONTINUE" : ((kind == ROW_RECENT) ? "RECENT" : "FAVORITES");
}

static void draw_minimal (menu_t *menu, int *lookups_left) {
    const theme_t *t = theme_current();
    int cover_w = (int)(LAUNCHER_SHOWCASE_W * MIN_COVER_ZOOM);
    int cover_h = (int)(LAUNCHER_SHOWCASE_H * MIN_COVER_ZOOM);
    int cover_x = LAUNCHER_X1 - cover_w - 6;
    int cover_y = MIN_TOP + 36;
    int list_x = LAUNCHER_X0 + 8;
    int list_w = cover_x - 32 - list_x;
    int top = MIN_TOP, bottom = CONTENT_BOTTOM;

    // Where the focused title sits in the whole list, so the list scrolls to keep it in view.
    int y = 0, focus_top = 0, focus_bottom = 0;
    for (int r = 0; r < row_count; r++) {
        y += MIN_LABEL_H;
        for (int i = 0; i < rows[r].count; i++) {
            int h = (rows[r].kind == ROW_RESUME) ? MIN_RESUME_H : MIN_ITEM_H;
            if (is_focus(r, i)) {
                focus_top = y - (i == 0 ? MIN_LABEL_H : 0);     // The first of a section brings its label.
                focus_bottom = y + h;
            }
            y += h;
        }
        y += MIN_SECTION_GAP;
    }
    if (focus_top < min_scroll) {
        min_scroll = focus_top;
    }
    if (focus_bottom - min_scroll > bottom - top) {
        min_scroll = focus_bottom - (bottom - top);
    }
    min_scroll_pos = motion_snap ? (float)min_scroll : launcher_ease(min_scroll_pos, (float)min_scroll);
    int drawn = (int)lroundf(min_scroll_pos);

    int clip_x1 = launcher_px(cover_x - 8);
    rdpq_set_scissor(0, top - 2, clip_x1 < 1 ? 1 : clip_x1, bottom + 2);
    y = top - drawn;
    for (int r = 0; r < row_count; r++) {
        if (y + MIN_LABEL_H > top - 4 && y < bottom) {
            launcher_small(list_x, y + 4, list_w, ALIGN_LEFT, STL_GRAY, min_label(rows[r].kind));
        }
        y += MIN_LABEL_H;
        for (int i = 0; i < rows[r].count; i++) {
            home_item_t *h = &rows[r].items[i];
            bool resume = (rows[r].kind == ROW_RESUME);
            int item_h = resume ? MIN_RESUME_H : MIN_ITEM_H;
            bool focus = is_focus(r, i);
            slot_add(list_x, y + drawn - min_scroll, list_w, item_h, r, i);
            if (y + item_h > top - 4 && y < bottom) {
                if (resume) {
                    launcher_title(list_x, y, list_w, ALIGN_LEFT, focus ? STL_DEFAULT : STL_GRAY, h->title);
                } else {
                    launcher_game_name(list_x, y + 2, list_w, ALIGN_LEFT, focus ? STL_DEFAULT : STL_GRAY, h->title);
                }
                if (focus) {
                    launcher_fill_rounded(list_x, y + item_h - 5, list_x + MIN_BAR_W, y + item_h - 2, 1, t->feature);
                }
            }
            y += item_h;
        }
        y += MIN_SECTION_GAP;
    }
    clip_none();

    home_item_t *f = focused_item();
    if (f) {
        bool pending;
        float zoom;
        surface_t *image = big_cover(menu, f, MIN_COVER_ZOOM, lookups_left, &pending, &zoom);
        launcher_draw_tile_zoomed(cover_x, cover_y, cover_w, cover_h, image, pending, f->title, false, t->bg, zoom);
        launcher_note_focused_cover_zoomed(image, cover_x, cover_y, cover_w, cover_h, zoom);
        char details[160];
        library_details_for_path(menu, item_path(f), details, sizeof(details));
        int text_y = cover_y + cover_h + 14;
        if (details[0]) {
            launcher_game_details(cover_x - 60, text_y, cover_w + 60, ALIGN_RIGHT, STL_GRAY, details);
            text_y += 24;
        }
        char folder[64];
        item_folder(f, folder, sizeof(folder));
        launcher_name(cover_x - 60, text_y, cover_w + 60, ALIGN_RIGHT, STL_GRAY, folder);
    }
    // The clock at the bottom left, across from the hints; the music note above the cover.
    char clock[16];
    launcher_clock_text(menu, clock, sizeof(clock));
    launcher_small(LAUNCHER_X0 + 8, LAUNCHER_FOOTER_Y + 12, 200, ALIGN_LEFT, STL_GRAY, clock);
    launcher_draw_notes(cover_x - 200, MIN_TOP + 4, cover_w + 206, ALIGN_RIGHT);
}

static int row_of_kind (row_kind_t kind) {
    for (int r = 0; r < row_count; r++) {
        if (rows[r].kind == kind) {
            return r;
        }
    }
    return -1;
}

static const char *row_label (row_kind_t kind) {
    return (kind == ROW_RESUME) ? "Resume" : ((kind == ROW_RECENT) ? "Recently played" : "Favorites");
}

/* Spotlight: the focused game on a bold slanted panel in the theme's feature color, filling the left half of the
   screen; the other games in a column of tiles beside it. */
#define SPOT_TOP_SHARE      (58)    /* The panel's width at the top and bottom, in percent of the screen */
#define SPOT_BOTTOM_SHARE   (52)
#define SPOT_COVER_Y        (66)
#define SPOT_COVER_ZOOM     (1.4f)  /* The focused cover, 1.4x the showcase size (the hi-res pack's own size) */
#define SPOT_OUTLINE        (3)
#define SPOT_GAP            (12)
#define SPOT_LABEL_H        (24)
#define SPOT_RESUME_H       (77)    /* The Resume card pinned at the top of the column */

static int spot_scroll = 0;
static float spot_scroll_pos = 0.0f;

static void draw_spotlight (menu_t *menu, int *lookups_left) {
    const theme_t *t = theme_current();
    int top_x = LAUNCHER_SCREEN_W * SPOT_TOP_SHARE / 100;
    int bottom_x = LAUNCHER_SCREEN_W * SPOT_BOTTOM_SHARE / 100;
    launcher_fill_slanted(top_x, bottom_x, 0, display_get_height(), t->feature);
    int resume_row = row_of_kind(ROW_RESUME);
    bool on_panel = (resume_row >= 0 && is_focus(resume_row, 0));

    home_item_t *f = focused_item();
    if (f) {
        int cx = LAUNCHER_SCREEN_W * 6 / 100;
        if (cx < LAUNCHER_X0) {
            cx = LAUNCHER_X0;
        }
        int cy = SPOT_COVER_Y;
        bool pending;
        float zoom;
        surface_t *image = big_cover(menu, f, SPOT_COVER_ZOOM, lookups_left, &pending, &zoom);
        // The cover sits in an outline in the color text on the feature color takes.
        int box_w = (int)(LAUNCHER_SHOWCASE_W * SPOT_COVER_ZOOM), box_h = (int)(LAUNCHER_SHOWCASE_H * SPOT_COVER_ZOOM);
        int iw = image ? (int)(image->width * zoom) : box_w;
        int ih = image ? (int)(image->height * zoom) : box_h;
        int ix = cx, iy = cy + (box_h - ih) / 2;
        launcher_fill_rounded(ix - SPOT_OUTLINE, iy - SPOT_OUTLINE, ix + iw + SPOT_OUTLINE, iy + ih + SPOT_OUTLINE,
            8, t->on_feature);
        launcher_draw_tile_zoomed(ix, iy, iw, ih, image, pending, f->title, false, t->on_feature, zoom);
        launcher_note_focused_cover_zoomed(image, ix, iy, iw, ih, zoom);

        int tw = bottom_x - cx - 20;
        int y = cy + box_h + 18;
        launcher_title(cx, y, tw, ALIGN_LEFT, STL_ON_FEATURE, f->title);
        y += 34;
        char details[160], line[200];
        library_details_for_path(menu, item_path(f), details, sizeof(details));
        snprintf(line, sizeof(line), "%s%s%s", row_label(rows[row_selected].kind), details[0] ? "  -  " : "", details);
        launcher_game_details(cx, y, tw, ALIGN_LEFT, STL_ON_FEATURE, line);
        y += 32;
        // The A button and what it does, on a pill in the background color.
        launcher_draw_prompt(cx, y, LAUNCHER_BUTTON_A, "A", on_panel ? "Resume" : "Play", t->bg);
    }

    // The column: Recently played and Favorites as tiles, two across where they fit.
    int tw = 133, th = 94;
    int gx0 = LAUNCHER_X1 - (2 * tw + SPOT_GAP);
    if (gx0 < top_x + 16) {
        gx0 = top_x + 16;
    }
    int avail = LAUNCHER_X1 - gx0;
    if (avail < 2 * tw + SPOT_GAP) {
        tw = 87;
        th = 61;
    }
    int cols = (avail + SPOT_GAP) / (tw + SPOT_GAP);
    if (cols < 1) {
        cols = 1;
    }
    int line_h = th + SPOT_GAP;

    // The game to resume stays in view at the top of the column, whichever game the panel shows.
    int grid_top = CONTENT_TOP;
    if (resume_row >= 0) {
        home_item_t *h = &rows[resume_row].items[0];
        int ry0 = CONTENT_TOP + 8, ry1 = ry0 + SPOT_RESUME_H;
        slot_add(gx0, ry0, avail, SPOT_RESUME_H, resume_row, 0);
        if (on_panel) {
            launcher_draw_focus_ring(gx0, ry0, LAUNCHER_X1, ry1, CARD_RADIUS);
        }
        launcher_fill_rounded(gx0, ry0, LAUNCHER_X1, ry1, CARD_RADIUS, t->panel);
        bool pending;
        surface_t *image = item_image(menu, h, 87, 61, lookups_left, &pending);
        launcher_draw_tile(gx0 + 8, ry0 + 8, 87, 61, image, pending, h->title, false, t->panel);
        int text_x = gx0 + 8 + 87 + 12;
        launcher_text(text_x, ry0 + 12, LAUNCHER_X1 - 8 - text_x, ALIGN_LEFT, STL_ACCENT, "Resume");
        launcher_game_name(text_x, ry0 + 38, LAUNCHER_X1 - 8 - text_x, ALIGN_LEFT, on_panel ? STL_DEFAULT : STL_GRAY, h->title);
        grid_top = ry1 + 6;
    }

    // Keep the focused tile's line in view.
    int y = 0;
    for (int r = 0; r < row_count; r++) {
        if (rows[r].kind == ROW_RESUME) {
            continue;
        }
        if (r == row_selected) {
            int line = rows[r].selected / cols;
            int top = y + (line == 0 ? 0 : SPOT_LABEL_H + line * line_h);
            int bottom = y + SPOT_LABEL_H + (line + 1) * line_h;
            if (top < spot_scroll) {
                spot_scroll = top;
            }
            if (bottom - spot_scroll > CONTENT_BOTTOM - grid_top) {
                spot_scroll = bottom - (CONTENT_BOTTOM - grid_top);
            }
        }
        y += SPOT_LABEL_H + ((rows[r].count + cols - 1) / cols) * line_h + 6;
    }
    spot_scroll_pos = motion_snap ? (float)spot_scroll : launcher_ease(spot_scroll_pos, (float)spot_scroll);
    int drawn = (int)lroundf(spot_scroll_pos);

    int clip_x1 = launcher_px(LAUNCHER_X1 + LAUNCHER_RING + 2);
    rdpq_set_scissor(0, grid_top - LAUNCHER_RING - 2, clip_x1 > display_get_width() ? display_get_width() : clip_x1,
        CONTENT_BOTTOM + 2);
    y = grid_top + 8 - drawn;
    for (int r = 0; r < row_count; r++) {
        if (rows[r].kind == ROW_RESUME) {
            continue;
        }
        bool row_focus = (r == row_selected);
        if (y + SPOT_LABEL_H > grid_top - 4 && y < CONTENT_BOTTOM) {
            launcher_text(gx0, y, avail, ALIGN_LEFT, row_focus ? STL_DEFAULT : STL_GRAY, "%s", row_label(rows[r].kind));
        }
        int ty = y + SPOT_LABEL_H;
        for (int i = 0; i < rows[r].count; i++) {
            int x = gx0 + (i % cols) * (tw + SPOT_GAP);
            int iy = ty + (i / cols) * line_h;
            int target_y = iy + drawn - spot_scroll;
            slot_add(x, target_y, tw, th, r, i);
            if (iy + th < grid_top - 4 || iy > CONTENT_BOTTOM) {
                continue;   // Off screen: not drawn, and its cover not read.
            }
            home_item_t *h = &rows[r].items[i];
            bool pending;
            surface_t *image = item_image(menu, h, tw, th, lookups_left, &pending);
            launcher_draw_tile(x, iy, tw, th, image, pending, h->title, false, t->bg);
            if (is_focus(r, i)) {
                launcher_draw_focus_ring_tile(x, target_y, tw, th);
            }
            if (rows[r].kind != ROW_FAVORITES && launcher_is_favorite(menu, item_path(h))) {
                launcher_draw_favorite_star(x, iy, tw);
            }
        }
        y = ty + ((rows[r].count + cols - 1) / cols) * line_h + 6;
    }
    clip_none();
}

/* A row of games that scrolls sideways (Cartridge's Favorites). */
typedef struct {
    int row;
    int index;
} game_ref_t;

typedef struct {
    int scroll;                 /**< First game in view */
    float pos;                  /**< Drawn scroll in pixels, gliding toward scroll */
} strip_t;

/** @brief Add a row's games to a list. */
static int refs_add (game_ref_t *refs, int n, int max, int row) {
    for (int i = 0; row >= 0 && i < rows[row].count && n < max; i++) {
        refs[n++] = (game_ref_t) { row, i };
    }
    return n;
}

/** @brief The focused game's place in a list, or -1. */
static int refs_focus (const game_ref_t *refs, int n) {
    for (int i = 0; i < n; i++) {
        if (is_focus(refs[i].row, refs[i].index)) {
            return i;
        }
    }
    return -1;
}

static void draw_strip (menu_t *menu, const game_ref_t *refs, int n, int x0, int x1, int y, int tw, int th, int step,
    strip_t *st, color_t behind, int *lookups_left) {
    int visible = (x1 - x0 - tw) / step + 1;
    if (visible < 1) {
        visible = 1;
    }
    int focus = refs_focus(refs, n);
    if (focus >= 0) {
        if (focus < st->scroll) {
            st->scroll = focus;
        }
        if (focus >= st->scroll + visible) {
            st->scroll = focus - visible + 1;
        }
    }
    float target = (float)(st->scroll * step);
    st->pos = motion_snap ? target : launcher_ease(st->pos, target);
    int scroll_px = (int)lroundf(st->pos);
    for (int i = 0; i < n; i++) {
        int target_x = x0 + (i - st->scroll) * step;
        slot_add(target_x, y, tw, th, refs[i].row, refs[i].index);
        int x = x0 + i * step - scroll_px;
        if (x + tw < x0 - step || x > x1) {
            continue;   // Off screen: not drawn, and its cover not read.
        }
        home_item_t *h = &rows[refs[i].row].items[refs[i].index];
        bool pending;
        surface_t *image = item_image(menu, h, tw, th, lookups_left, &pending);
        launcher_draw_tile(x, y, tw, th, image, pending, h->title, false, behind);
        if (i == focus) {
            launcher_note_focused_cover(image, x, y, tw, th);
            launcher_draw_focus_ring_tile(target_x, y, tw, th);
        }
        if (rows[refs[i].row].kind != ROW_FAVORITES && launcher_is_favorite(menu, item_path(h))) {
            launcher_draw_favorite_star(x, y, tw);
        }
    }
}

/* Cartridge: a header bar; the game to resume and the recently played stand as cartridges in a band (the first one
   larger), the focused game's title under it, Favorites below. Every shade comes from the theme. */
#define CART_BAND_TOP       (LAUNCHER_HEADER_BAR_H + 14)
#define CART_BAND_H         (160)
#define CART_PAD_X          (12)    /* Cartridge body around its label */
#define CART_PAD_TOP        (12)
#define CART_PAD_BOTTOM     (16)
#define CART_GAP            (18)
#define CART_LIFT           (8)     /* The focused cartridge stands this much higher */
#define CART_ROW_W          (87)
#define CART_ROW_H          (61)
#define CART_ROW_STEP       (97)

static int cart_scroll = 0;
static float cart_scroll_pos = 0.0f;
static strip_t cart_row_strip;

static void draw_cartridge (menu_t *menu, int *lookups_left) {
    const theme_t *t = theme_current();
    color_t band_color = color_mix(t->bg, t->panel, 60);
    color_t body_color = color_mix(t->panel, t->text_muted, 35);
    int y0 = CART_BAND_TOP, y1 = CART_BAND_TOP + CART_BAND_H;
    launcher_fill_rounded(LAUNCHER_X0, y0, LAUNCHER_X1, y1, 6, band_color);

    game_ref_t band[MAX_ROW_ITEMS + 1];
    int band_n = refs_add(band, 0, MAX_ROW_ITEMS + 1, row_of_kind(ROW_RESUME));
    bool has_resume = band_n > 0;
    band_n = refs_add(band, band_n, MAX_ROW_ITEMS + 1, row_of_kind(ROW_RECENT));

    // Where each cartridge goes along the band (the one to resume is larger), and the scroll that keeps the
    // focused one in view.
    int xs[MAX_ROW_ITEMS + 1], ws[MAX_ROW_ITEMS + 1];
    int x = 0, focus = refs_focus(band, band_n);
    for (int i = 0; i < band_n; i++) {
        bool big = has_resume && i == 0;
        ws[i] = (big ? LAUNCHER_COVER_W : 133) + 2 * CART_PAD_X;
        xs[i] = x;
        x += ws[i] + CART_GAP;
    }
    int view_x0 = LAUNCHER_X0 + 24, view_w = LAUNCHER_WIDTH - 48;
    if (focus >= 0) {
        if (xs[focus] < cart_scroll) {
            cart_scroll = xs[focus];
        }
        if (xs[focus] + ws[focus] - cart_scroll > view_w) {
            cart_scroll = xs[focus] + ws[focus] - view_w;
        }
    }
    cart_scroll_pos = motion_snap ? (float)cart_scroll : launcher_ease(cart_scroll_pos, (float)cart_scroll);
    int drawn = (int)lroundf(cart_scroll_pos);

    int clip_x1 = launcher_px(LAUNCHER_X1 - 8);
    rdpq_set_scissor(0, y0 - CART_LIFT, clip_x1 < 1 ? 1 : clip_x1, y1);
    for (int i = 0; i < band_n; i++) {
        bool big = has_resume && i == 0;
        int lw = big ? LAUNCHER_COVER_W : 133, lh = big ? LAUNCHER_COVER_H : 94;
        int bh = lh + CART_PAD_TOP + CART_PAD_BOTTOM;
        int lift = (i == focus) ? CART_LIFT : 0;
        int bx = view_x0 + xs[i] - drawn;
        int target_bx = view_x0 + xs[i] - cart_scroll;
        int by = y1 - bh - lift;
        slot_add(target_bx + CART_PAD_X, by + CART_PAD_TOP, lw, lh, band[i].row, band[i].index);
        if (bx < LAUNCHER_X0 + 8 || bx > LAUNCHER_X1) {
            continue;   // Past the band's ends: not drawn, and its cover not read.
        }
        // The cartridge's body: rounded at the top, standing on the band's floor.
        launcher_fill_rounded(bx, by, bx + ws[i], by + 16, 8, body_color);
        launcher_fill_rounded(bx, by + 8, bx + ws[i], y1, 0, body_color);
        home_item_t *h = &rows[band[i].row].items[band[i].index];
        bool pending;
        surface_t *image = item_image(menu, h, lw, lh, lookups_left, &pending);
        launcher_draw_tile(bx + CART_PAD_X, by + CART_PAD_TOP, lw, lh, image, pending, h->title, false, body_color);
        if (i == focus) {
            launcher_note_focused_cover(image, bx + CART_PAD_X, by + CART_PAD_TOP, lw, lh);
            launcher_draw_focus_ring_tile(target_bx + CART_PAD_X, by + CART_PAD_TOP, lw, lh);
        }
    }
    clip_none();

    // The focused game's name and where it's from, under the band.
    home_item_t *f = focused_item();
    if (f) {
        int ty = y1 + 12;
        launcher_title(LAUNCHER_X0 + 24, ty, LAUNCHER_WIDTH - 48, ALIGN_LEFT, STL_DEFAULT, f->title);
        char line[200], details[160];
        library_details_for_path(menu, item_path(f), details, sizeof(details));
        snprintf(line, sizeof(line), "%s%s%s", row_label(rows[row_selected].kind), details[0] ? "  -  " : "", details);
        launcher_game_details(LAUNCHER_X0 + 24, ty + 38, LAUNCHER_WIDTH - 48, ALIGN_LEFT, STL_GRAY, line);
    }

    int fav = row_of_kind(ROW_FAVORITES);
    if (fav >= 0) {
        int ly = y1 + 80;
        game_ref_t refs[MAX_ROW_ITEMS];
        int n = refs_add(refs, 0, MAX_ROW_ITEMS, fav);
        launcher_text(LAUNCHER_X0, ly, 0, ALIGN_LEFT, refs_focus(refs, n) >= 0 ? STL_DEFAULT : STL_GRAY, "Favorites");
        clip_content();
        draw_strip(menu, refs, n, LAUNCHER_X0, LAUNCHER_X1, ly + 24, CART_ROW_W, CART_ROW_H, CART_ROW_STEP,
            &cart_row_strip, t->bg, lookups_left);
        clip_none();
    }
}

/** @brief Draw Home in one of the new styles (not Classic). */
static void draw_styled (menu_t *menu, int style, int *lookups_left) {
    slot_total = 0;
    switch (style) {
        case LAUNCHER_HOME_CARTRIDGE:
            draw_cartridge(menu, lookups_left);
            break;
        case LAUNCHER_HOME_SPOTLIGHT:
            draw_spotlight(menu, lookups_left);
            break;
        default:
            draw_minimal(menu, lookups_left);
            break;
    }
}

/** @brief Input for the new styles: the d-pad moves between slots. */
static void process_styled (menu_t *menu, int style) {
    int dx = menu->actions.go_left ? -1 : (menu->actions.go_right ? 1 : 0);
    int dy = menu->actions.go_up ? -1 : (menu->actions.go_down ? 1 : 0);
    if (dx || dy) {
        if (slot_move(dx, dy)) {
            sound_play_effect(SFX_TICK);
        } else if (style == LAUNCHER_HOME_MINIMAL && dx) {
            // Left/right jump between the lists.
            int r = row_selected + dx;
            if (r >= 0 && r < row_count) {
                row_selected = r;
                sound_play_effect(SFX_TICK);
            }
        }
        return;
    }
    home_item_t *f = focused_item();
    if (menu->actions.enter && f) {
        launch(menu, f);
    } else if (menu->actions.trigger_z && f) {
        launcher_game_menu_show(menu, path_clone(item_path(f)));
    }
}

/** @brief The header and footer a style draws with (set before the header is drawn). */
static void set_style_chrome (int style) {
    switch (style) {
        case LAUNCHER_HOME_CARTRIDGE:
            launcher_set_header_style(LAUNCHER_HEADER_BAR);
            launcher_hide_footer_line();
            break;
        case LAUNCHER_HOME_MINIMAL:
            launcher_set_header_style(LAUNCHER_HEADER_NONE);
            launcher_hide_footer_line();
            break;
        case LAUNCHER_HOME_SPOTLIGHT:
            launcher_set_header_style(LAUNCHER_HEADER_CORNER);
            launcher_hide_footer_line();
            break;
        default:
            break;
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    launcher_covers_begin_frame();
    if (row_count > 0) {
        set_style_chrome(menu->settings.launcher_home_style);
    }
    launcher_draw_header(menu, LAUNCHER_TAB_HOME);

    if (reveal == REVEAL_HIDDEN && reveal_frames == 0) {
        reveal_since_ms = get_ticks_ms();
    }
    int lookups_left = (reveal == REVEAL_HIDDEN) ? REVEAL_LOOKUPS : 1;
    covers_pending = false;
    if (row_count == 0) {
        launcher_text(LAUNCHER_X0, 200, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "Games you play and favorite will show up here");
        launcher_text(LAUNCHER_X0, 224, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "Press R to browse your library");
        launcher_draw_footer(hints_empty, HINT_COUNT(hints_empty));
    } else if (menu->settings.launcher_home_style != LAUNCHER_HOME_CLASSIC) {
        draw_styled(menu, menu->settings.launcher_home_style, &lookups_left);
        motion_snap = false;
        home_background_work(menu, &lookups_left);
        launcher_draw_footer(hints_home, HINT_COUNT(hints_home));
    } else {
        // Keep the selected row on screen; rows above or below scroll out of the content area.
        int row_top = CONTENT_TOP;
        for (int r = 0; r < row_selected; r++) {
            row_top += (rows[r].kind == ROW_RESUME) ? RESUME_CARD_H + ROW_GAP : ROW_H;
        }
        int row_bottom = row_top + ((rows[row_selected].kind == ROW_RESUME) ? RESUME_CARD_H : ROW_H);
        if (row_top - scroll_y < CONTENT_TOP) {
            scroll_y = row_top - CONTENT_TOP;
        }
        if (row_bottom - scroll_y > CONTENT_BOTTOM) {
            scroll_y = row_bottom - CONTENT_BOTTOM;
        }

        // The page glides to its scroll position too.
        scroll_y_pos = motion_snap ? (float)scroll_y : launcher_ease(scroll_y_pos, (float)scroll_y);
        int drawn_scroll = (int)lroundf(scroll_y_pos);
        scroll_y_ahead = drawn_scroll - scroll_y;

        // The left edge stays at 0: covers are copied in COPY mode, where any other left bound
        // hangs the RDP (a hardware bug). The right edge clips rows gliding sideways.
        int clip_x1 = launcher_px(LAUNCHER_X1 + LAUNCHER_RING + 2);
        rdpq_set_scissor(0, CONTENT_TOP - LAUNCHER_RING - 2,
            clip_x1 > display_get_width() ? display_get_width() : (clip_x1 < 1 ? 1 : clip_x1), CONTENT_BOTTOM + 2);
        int y = CONTENT_TOP - drawn_scroll;
        for (int r = 0; r < row_count; r++) {
            bool focused = (r == row_selected);
            int height = (rows[r].kind == ROW_RESUME) ? RESUME_CARD_H + ROW_GAP : ROW_H;
            if (y + height < CONTENT_TOP - 10 || y > CONTENT_BOTTOM) {
                y += height;    // Off screen: skip drawing (and loading its covers).
                continue;
            }
            if (rows[r].kind == ROW_RESUME) {
                y = draw_resume(menu, &rows[r], y, focused, &lookups_left);
            } else {
                y = draw_row(menu, &rows[r], y, focused, &lookups_left);
            }
        }
        rdpq_set_scissor(0, 0, display_get_width(), display_get_height());
        motion_snap = false;
        home_background_work(menu, &lookups_left);
        launcher_draw_footer(hints_home, HINT_COUNT(hints_home));
    }

    launcher_menu_draw();

    uint64_t now_ms = get_ticks_ms();
    uint64_t frame_ms = reveal_last_ms ? now_ms - reveal_last_ms : 0;
    reveal_last_ms = now_ms;
    if (reveal == REVEAL_HIDDEN) {
        // Shown once every cover is drawn and frames have come quickly for a while: right after the
        // startup work, frames are still uneven (music refilling and so on) and the fade would stutter.
        reveal_frames++;
        // The check for newly added ROMs reads folder listings, ~80-130 ms at a time: done now, behind black,
        // it can't make a visible frame long later (the library warms after the first frame, hence > 1).
        bool checked = library_new_games_checked();
        if (reveal_frames > 1 && !checked) {
            library_check_new_games(REVEAL_CHECK_US);
            checked = library_new_games_checked();
        } else if (reveal_frames > 1 && !folders_ready) {
            // Then the other folders are listed and sorted, one a frame, so the grid needn't do it later.
            folders_ready = view_library_prepare_folders(menu);
        }
        checked = checked && folders_ready;
        reveal_steady = (frame_ms > 0 && frame_ms <= REVEAL_STEADY_MS && checked) ? reveal_steady + 1 : 0;
        bool timed_out = (now_ms - reveal_since_ms) >= REVEAL_TIMEOUT_MS;
        if ((reveal_steady >= REVEAL_STEADY_FRAMES && !covers_pending) || timed_out) {
            reveal = REVEAL_FADING;
            sound_bgm_end_load();   // Back to the short queue, so sound effects keep up.
            reveal_fade_ms = 0.0f;
            fade_started_ms = now_ms;
            perf_log_event("startup Home hidden for %lu ms (%d frames, %s)", (unsigned long)(now_ms - reveal_since_ms),
                reveal_frames, timed_out ? "timed out" : "covers in, frames steady");
        }
    } else if (reveal == REVEAL_FADING) {
        reveal_fade_ms += (frame_ms < REVEAL_STEP_MS) ? frame_ms : REVEAL_STEP_MS;
        fade_frames++;
        fade_slowest_ms = (frame_ms > fade_slowest_ms) ? frame_ms : fade_slowest_ms;
    }
    if (reveal == REVEAL_HIDDEN) {
        launcher_draw_screen_fade(255);
    } else if (reveal == REVEAL_FADING) {
        // Black to Home, easing out (quick at first, settling gently).
        float t = reveal_fade_ms / REVEAL_FADE_MS;
        if (t >= 1.0f) {
            reveal = REVEAL_SHOWN;
            perf_log_event("startup fade-in: %d frames over %lu ms, slowest frame %lu ms", fade_frames,
                (unsigned long)(now_ms - fade_started_ms), (unsigned long)fade_slowest_ms);
        } else {
            float e = (1.0f - t) * (1.0f - t);
            launcher_draw_screen_fade((int)(255 * e + 0.5f));
        }
    }

    rdpq_detach_show();
}


void view_home_init (menu_t *menu) {
    packs_open = false;     // Leaving the launcher closes them.
    if (reveal == REVEAL_HIDDEN) {
        // The startup work behind black makes frames of up to ~440 ms, longer than the menu's usual ~140 ms of
        // music queued ahead (the music stuttered): every buffer is kept full, as for a game load, until Home shows.
        sound_bgm_begin_load();
    }
    launcher_menu_init();
    build_rows(menu);
    motion_snap = true;
    launcher_focus_reset();
}

void view_home_display (menu_t *menu, surface_t *display) {
    launcher_set_widescreen(menu->settings.launcher_widescreen);
    launcher_set_large_text(menu->settings.launcher_large_text);
    launcher_set_cartridge_art(menu->settings.launcher_cartridge_art ||
        menu->settings.launcher_home_style == LAUNCHER_HOME_CARTRIDGE);
    launcher_track_idle(menu);
    static int last_style = -1;
    if (menu->settings.launcher_home_style != last_style) {
        last_style = menu->settings.launcher_home_style;
        motion_snap = true;     // A new style is laid out in place, not glided into.
        launcher_focus_reset();
    }
    if (launcher_take_favorites_changed()) {
        build_rows(menu);
    }

    if (reveal != REVEAL_HIDDEN) {
        process(menu);  // Nothing to act on before Home can be seen.
    }

    perf_log_draw_begin();
    draw(menu, display);
}
