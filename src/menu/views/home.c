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
#include "../fonts.h"
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

static launcher_hint_t hints_home[] = {
    { LAUNCHER_BUTTON_A, "A", "Play" },     /* "Details" without Quick launch (set as it's drawn) */
    { LAUNCHER_BUTTON_OTHER, "R", "Library" },
    { LAUNCHER_BUTTON_START, "S", "Menu" },
};

static launcher_hint_t hints_home_favorite[] = {
    { LAUNCHER_BUTTON_A, "A", "Play" },
    { LAUNCHER_BUTTON_OTHER, "Z", "Move" },
    { LAUNCHER_BUTTON_OTHER, "R", "Library" },
    { LAUNCHER_BUTTON_START, "S", "Menu" },
};

static const launcher_hint_t hints_moving[] = {
    { LAUNCHER_BUTTON_LEFT_RIGHT, "", "Move" },
    { LAUNCHER_BUTTON_A, "A", "Place" },
    { LAUNCHER_BUTTON_B, "B", "Cancel" },
};

static const launcher_hint_t hints_tabs[] = {
    { LAUNCHER_BUTTON_A, "A", "Open" },
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
#define REVEAL_STEADY_MS    (34)    /* The newer styles take ~23-30 ms a frame: steady, but not under 25 */
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
static void process_binder(menu_t *menu);
static int slot_toward(int dx, int dy);
static bool binder_rail;     /* Binder Mosaic: the tabs have the focus */

/* Moving a favorite: Z picks up the highlighted one, the d-pad or stick moves it, A or Z places it, B puts it back. */
static bool moving = false;
static bookkeeping_item_t moving_backup[FAVORITES_COUNT];   /* The order before, for B */
static int moving_from = 0;
static int spot_cols = 1;       /* Games across Spotlight's column: up/down move a held favorite a whole row there */

static int row_of_kind(row_kind_t kind);
static home_item_t *focused_item(void);

static void moving_end (menu_t *menu, bool keep) {
    moving = false;
    launcher_set_focus_moving(false);
    if (keep) {
        bookkeeping_save(&menu->bookkeeping);   // One write, when it's placed.
        launcher_favorites_touched();
        sound_play_effect(SFX_SETTING);
    } else {
        memcpy(menu->bookkeeping.favorite_items, moving_backup, sizeof(moving_backup));
        int r = row_of_kind(ROW_FAVORITES);
        build_rows(menu);
        r = row_of_kind(ROW_FAVORITES);
        if (r >= 0) {
            row_selected = r;
            rows[r].selected = (moving_from < rows[r].count) ? moving_from : 0;
        }
        sound_play_effect(SFX_EXIT);
    }
}

/** @brief Z on a favorite: it's picked up (needs at least two to move between). */
static bool moving_start (menu_t *menu) {
    home_item_t *f = focused_item();
    int r = row_of_kind(ROW_FAVORITES);
    if (!f || !f->favorite || r < 0 || row_selected != r || rows[r].count < 2) {
        return false;
    }
    memcpy(moving_backup, menu->bookkeeping.favorite_items, sizeof(moving_backup));
    moving_from = rows[r].selected;
    moving = true;
    launcher_set_focus_moving(true);
    sound_play_effect(SFX_POP);
    return true;
}

/** @brief While moving: any direction moves the held favorite one place, the others making room. */
static void moving_process (menu_t *menu) {
    if (menu->actions.enter || menu->actions.trigger_z) {
        moving_end(menu, true);
        return;
    }
    if (menu->actions.back) {
        moving_end(menu, false);
        return;
    }
    int step = (menu->actions.go_left || menu->actions.go_up) ? -1 : ((menu->actions.go_right || menu->actions.go_down) ? 1 : 0);
    // Spotlight lays favorites out in rows of spot_cols: up/down there move the game a whole row.
    if (menu->settings.launcher_home_style == LAUNCHER_HOME_SPOTLIGHT && (menu->actions.go_up || menu->actions.go_down)) {
        step *= spot_cols;
    }
    home_row_t *row = &rows[row_selected];
    int i = row->selected, j = i + step;
    // Binder Mosaic's tiles aren't in one line: the game goes to the tile in the direction pressed.
    if (menu->settings.launcher_home_style == LAUNCHER_HOME_BINDER && step) {
        int dx = menu->actions.go_left ? -1 : (menu->actions.go_right ? 1 : 0);
        int dy = menu->actions.go_up ? -1 : (menu->actions.go_down ? 1 : 0);
        j = slot_toward(dx, dy);
        j = (j < 0) ? i : j;
    }
    j = (j < 0) ? 0 : ((j >= row->count) ? row->count - 1 : j);
    if (!step || j == i) {
        return;
    }
    // The game moves to j, the ones in between each move one place to make room: neighbouring slots and titles
    // trade places one step at a time (the row's entries point at their favorites slots).
    bookkeeping_item_t *items = menu->bookkeeping.favorite_items;
    int dir = (j > i) ? 1 : -1;
    for (int k = i; k != j; k += dir) {
        bookkeeping_item_t swap = items[row->items[k].index];
        items[row->items[k].index] = items[row->items[k + dir].index];
        items[row->items[k + dir].index] = swap;
        char *title = row->items[k].title;
        row->items[k].title = row->items[k + dir].title;
        row->items[k + dir].title = title;
    }
    row->selected = j;
    sound_play_effect(SFX_TICK);
}

static void process (menu_t *menu) {
    if (menu->actions.go_up || menu->actions.go_down || menu->actions.go_left || menu->actions.go_right ||
        menu->actions.enter || menu->actions.back || menu->actions.settings || menu->actions.lz_context ||
        menu->actions.shoulder_r) {
        home_last_input_ms = get_ticks_ms();
    }
    if (launcher_menu_process(menu)) {
        return;
    }
    if (moving) {
        moving_process(menu);
        return;
    }
    bool on_tabs = (menu->settings.launcher_home_style == LAUNCHER_HOME_BINDER && binder_rail);
    if (menu->actions.trigger_z && row_count > 0 && !on_tabs && moving_start(menu)) {
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
    if (menu->settings.launcher_home_style != LAUNCHER_HOME_BINDER) {
        process_styled(menu, menu->settings.launcher_home_style);
        return;
    }
    process_binder(menu);
}

/** @brief Work done while Home is on screen: the library's covers and the cover packs, read ahead. */
#define LAUNCH_AHEAD_IDLE_MS (300)  /* The focused game's hi-res cover is read once the controller rests this long */

static void home_background_work (menu_t *menu, int *lookups_left) {
    launcher_covers_pin(false);     // Read-ahead isn't Home's own.
    if (reveal == REVEAL_SHOWN && get_ticks_ms() - home_last_input_ms >= LAUNCH_AHEAD_IDLE_MS) {
        launcher_covers_read_ahead_launch();    // So a launch needn't wait for the card.
    }
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

/**
 * @brief An off-screen Home cover is still asked for (read in the background, a cover every few frames), so it
 * belongs to this Home visit and stays in memory: scrolling to Favorites, or coming back from the library, then
 * finds it ready.
 */
static void keep_loaded (menu_t *menu, home_item_t *h, int w, int hgt, int *lookups_left) {
    if (reveal != REVEAL_SHOWN) {
        return;     // The startup Home's own covers first: these reads held up its reveal to the 3 s limit.
    }
    bool pending;
    bool shown_pending = covers_pending;    // Off screen: the startup Home doesn't wait for it.
    launcher_covers_background(true);
    item_image(menu, h, w, hgt, lookups_left, &pending);
    launcher_covers_background(false);
    covers_pending = shown_pending;
}

/* ------------------------------------------------------------------ Home styles other than Binder Mosaic */

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

/** @brief The nearest slot in a direction from the focused one, or NULL. */
static const slot_t *slot_find (int dx, int dy) {
    const slot_t *c = NULL;
    for (int i = 0; i < slot_total && !c; i++) {
        if (is_focus(slots[i].row, slots[i].index)) {
            c = &slots[i];
        }
    }
    if (!c) {
        return NULL;
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
    return best;
}

/** @brief The game in the selected row in a direction from the focused one, or -1. */
static int slot_toward (int dx, int dy) {
    const slot_t *to = slot_find(dx, dy);
    return (to && to->row == row_selected) ? to->index : -1;
}

/** @brief Move the focus to the nearest slot in a direction; false when there is none. */
static bool slot_move (int dx, int dy) {
    const slot_t *best = slot_find(dx, dy);
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
    (void)t;    // The panel is drawn with the background (set_style_chrome), under the header.
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
        y += launcher_title_wrapped(cx, y, tw, 2, STL_ON_FEATURE, f->title) + 4;
        char details[160], line[200];
        library_details_for_path(menu, item_path(f), details, sizeof(details));
        snprintf(line, sizeof(line), "%s%s%s", row_label(rows[row_selected].kind), details[0] ? "  -  " : "", details);
        y += launcher_game_details_wrapped(cx, y, tw, 2, STL_ON_FEATURE, line) + 10;
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
    spot_cols = cols;
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
                keep_loaded(menu, &rows[r].items[i], tw, th, lookups_left);
                continue;   // Off screen: not drawn.
            }
            home_item_t *h = &rows[r].items[i];
            bool pending;
            surface_t *image = item_image(menu, h, tw, th, lookups_left, &pending);
            launcher_draw_tile(x, iy, tw, th, image, pending, h->title, false, t->bg);
            if (is_focus(r, i)) {
                launcher_draw_focus_ring_cover(x, target_y, tw, th, image);
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
            keep_loaded(menu, &rows[refs[i].row].items[refs[i].index], tw, th, lookups_left);
            continue;   // Off screen: not drawn.
        }
        home_item_t *h = &rows[refs[i].row].items[refs[i].index];
        bool pending;
        surface_t *image = item_image(menu, h, tw, th, lookups_left, &pending);
        launcher_draw_tile(x, y, tw, th, image, pending, h->title, false, behind);
        if (i == focus) {
            launcher_note_focused_cover(image, x, y, tw, th);
            launcher_draw_focus_ring_cover(target_x, y, tw, th, image);
        }
        if (rows[refs[i].row].kind != ROW_FAVORITES && launcher_is_favorite(menu, item_path(h))) {
            launcher_draw_favorite_star(x, y, tw);
        }
    }
}

/** @brief Every Home game in order (Resume, Recently played, Favorites), as (row, index) pairs; the count. */
static int home_games (game_ref_t *refs, int max) {
    int n = 0;
    for (int r = 0; r < row_count; r++) {
        for (int i = 0; i < rows[r].count && n < max; i++) {
            refs[n++] = (game_ref_t) { r, i };
        }
    }
    return n;
}

/** @brief A colour eased toward a target each frame (a style's background follows the focused game's art). */
typedef struct {
    float c[3];
    bool set;
    color_t target;
} eased_color_t;

static color_t eased_color_step (eased_color_t *e, color_t fallback) {
    if (!e->set) {
        e->target = fallback;
        e->c[0] = fallback.r;
        e->c[1] = fallback.g;
        e->c[2] = fallback.b;
        e->set = true;
    } else if (motion_snap) {
        e->c[0] = e->target.r;
        e->c[1] = e->target.g;
        e->c[2] = e->target.b;
    } else {
        e->c[0] = launcher_ease(e->c[0], e->target.r);
        e->c[1] = launcher_ease(e->c[1], e->target.g);
        e->c[2] = launcher_ease(e->c[2], e->target.b);
    }
    return RGBA32((int)e->c[0], (int)e->c[1], (int)e->c[2], 0xFF);
}

/** @brief The focused game's art colour, when its image has loaded (else the target stays). */
static void eased_color_from (eased_color_t *e, surface_t *image, bool pending, color_t bg, int pct) {
    color_t c;
    if (image && !pending && launcher_cover_color(image, &c)) {
        e->target = color_mix(bg, c, pct);
    }
}

/*
 * Cartridge: one big cartridge with the focused game's label, its title and details beside it, on a slanted band in
 * the label's colour that slides in from the left as the focus moves (the screen otherwise in the theme). Below, every
 * game in one row of small cartridges, a label where each list starts: left/right along it, C left/right to the next
 * list.
 */
#define CART_TOP            (LAUNCHER_SUBHEADER_Y + 4)
#define CART_ZOOM           (1.0f)  /* The label at the showcase pack's own size: drawn 1:1, the fast way */
#define CART_PAD            (14)    /* Cartridge body around its label */
#define CART_GRIP           (34)    /* The cartridge's lower part, under the label */
#define CART_BAND_Y1        (296)   /* The band's foot, above the row */
#define CART_BAND_TOP_W     (150)   /* How far past the cartridge the band reaches, at the top and at its foot */
#define CART_BAND_FOOT_W    (70)
#define CART_BAND_EDGE      (26)    /* Its brighter edge */
#define CART_BAND_SLIDE     (150)   /* A new game's band slides in from this far left */
#define CART_LABELS_Y       (302)
#define CART_STRIP_Y        (322)
#define CART_ROW_W          (133)
#define CART_ROW_H          (94)
#define CART_ROW_STEP       (145)
#define CART_BAND_TINT      (62)    /* Percent of the label's colour in the band; the edge has more */
#define CART_EDGE_TINT      (92)

static strip_t cart_row_strip;
static eased_color_t cart_band = { .set = false };
static eased_color_t cart_edge = { .set = false };
static float cart_band_dx = 0.0f;       /* The band's slide, easing back to 0 */
static int cart_text_right = 0;         /* The right end of what's beside the cart, last frame */
static float cart_band_reach = -1.0f;   /* The band's foot, eased toward past cart_text_right */
static home_item_t *cart_band_item = NULL;

static int cart_body_w (void) {
    return (int)(LAUNCHER_SHOWCASE_W * CART_ZOOM) + 2 * CART_PAD;
}

/** @brief This frame's band (drawn with the background, under the header): where it is and its colours. */
static void cart_background (void) {
    const theme_t *t = theme_current();
    home_item_t *f = focused_item();
    if (f != cart_band_item) {
        if (cart_band_item && !motion_snap) {
            cart_band_dx = -(float)CART_BAND_SLIDE;    // A new game: the band slides in again.
        }
        cart_band_item = f;
    }
    cart_band_dx = motion_snap ? 0.0f : launcher_ease(cart_band_dx, 0.0f);
    color_t band = eased_color_step(&cart_band, color_mix(t->bg, t->feature, CART_BAND_TINT));
    color_t edge = eased_color_step(&cart_edge, color_mix(t->bg, t->feature, CART_EDGE_TINT));
    int right = LAUNCHER_X0 + 4 + cart_body_w();
    int dx = (int)lroundf(cart_band_dx);
    // The foot just past the text and the A pill beside the cart (the slant leans right above it).
    int want = right + CART_BAND_FOOT_W;
    want = (cart_text_right + 14 > want) ? cart_text_right + 14 : want;
    int max_foot = LAUNCHER_SCREEN_W - CART_BAND_TOP_W + CART_BAND_FOOT_W - CART_BAND_EDGE;
    want = (want > max_foot) ? max_foot : want;
    cart_band_reach = (cart_band_reach < 0.0f || motion_snap) ? (float)want : launcher_ease(cart_band_reach, (float)want);
    int foot = (int)lroundf(cart_band_reach);
    launcher_set_background_band(foot + (CART_BAND_TOP_W - CART_BAND_FOOT_W) + dx, foot + dx, CART_BAND_Y1, band, edge,
        CART_BAND_EDGE);
}

static void draw_cartridge (menu_t *menu, int *lookups_left) {
    const theme_t *t = theme_current();
    color_t body = color_mix(t->panel, t->text_muted, 35);
    int lw = (int)(LAUNCHER_SHOWCASE_W * CART_ZOOM), lh = (int)(LAUNCHER_SHOWCASE_H * CART_ZOOM);
    int bx = LAUNCHER_X0 + 4, by = CART_TOP;
    int bw = cart_body_w(), bh = lh + CART_PAD + CART_GRIP;
    home_item_t *f = focused_item();

    // The cartridge: its body, grip lines in the lower part, and the label.
    launcher_fill_rounded(bx, by, bx + bw, by + bh, 12, body);
    color_t ridge = color_mix(body, t->bg, 35);
    for (int k = 0; k < 3; k++) {
        int ry = by + CART_PAD + lh + 9 + k * 7;
        launcher_fill_rounded(bx + bw / 2 - 46, ry, bx + bw / 2 + 46, ry + 3, 1, ridge);
    }
    if (f) {
        bool pending;
        float zoom;
        // 1:1 from the showcase pack: a shrunk hi-res cover is drawn in small pieces, which kept Home at ~43 fps.
        surface_t *image = item_image(menu, f, LAUNCHER_SHOWCASE_W, LAUNCHER_SHOWCASE_H, lookups_left, &pending);
        zoom = 1.0f;
        int iw = (image && !pending) ? (int)(image->width * zoom) : lw;
        int ih = (image && !pending) ? (int)(image->height * zoom) : lh;
        iw = (iw > lw) ? lw : iw;
        ih = (ih > lh) ? lh : ih;
        int ix = bx + CART_PAD + (lw - iw) / 2, iy = by + CART_PAD + (lh - ih) / 2;
        launcher_draw_tile_zoomed(ix, iy, iw, ih, image, pending, f->title, false, body, zoom);
        launcher_note_focused_cover_zoomed(image, ix, iy, iw, ih, zoom);
        eased_color_from(&cart_band, image, pending, t->bg, CART_BAND_TINT);
        eased_color_from(&cart_edge, image, pending, t->bg, CART_EDGE_TINT);

        // Beside it: which list, the title, the details and the A prompt.
        int tx = bx + bw + 26, tw = LAUNCHER_X1 - tx;
        tw = (tw > 250) ? 250 : tw;     // A column, not the rest of the screen: the band reaches just past it.
        int y = by + 4;
        launcher_text_sized(LAUNCHER_SIZE_BODY, tx, y, tw, 22, ALIGN_LEFT, STL_DEFAULT, row_label(rows[row_selected].kind));
        y += 26;
        int title_w = launcher_text_sized_width(LAUNCHER_SIZE_TITLE, f->title);
        y += launcher_title_wrapped(tx, y, tw, 3, STL_DEFAULT, f->title) + 6;
        char details[160];
        library_details_for_path(menu, item_path(f), details, sizeof(details));
        int details_w = 0;
        if (details[0]) {
            details_w = tw;     // Wrapped to the column
            y += launcher_game_details_wrapped(tx, y, tw, 3, STL_DEFAULT, details) + 12;
        }
        bool resume = (rows[row_selected].kind == ROW_RESUME);
        int pill_w = launcher_draw_prompt(tx, y, LAUNCHER_BUTTON_A, "A", resume ? "Resume" : "Play", t->bg);
        int widest = (title_w > tw) ? tw : title_w;
        widest = (details_w > widest) ? details_w : widest;
        widest = (pill_w > widest) ? pill_w : widest;
        cart_text_right = tx + widest;
    }

    // Every game in one row; a label (and a divider) where each list starts.
    game_ref_t refs[ROW_KINDS * MAX_ROW_ITEMS];
    int n = home_games(refs, ROW_KINDS * MAX_ROW_ITEMS);
    clip_content();
    draw_strip(menu, refs, n, LAUNCHER_X0, LAUNCHER_X1, CART_STRIP_Y, CART_ROW_W, CART_ROW_H, CART_ROW_STEP,
        &cart_row_strip, t->bg, lookups_left);
    int scroll_px = (int)lroundf(cart_row_strip.pos);
    for (int i = 0; i < n; i++) {
        if (refs[i].index != 0) {
            continue;
        }
        int x = LAUNCHER_X0 + i * CART_ROW_STEP - scroll_px;
        if (i > 0) {
            int lx = x - (CART_ROW_STEP - CART_ROW_W) / 2 - 1;
            ui_components_box_draw(launcher_px(lx), CART_LABELS_Y + 2, launcher_px(lx) + 1, CART_STRIP_Y + CART_ROW_H,
                t->line);
        }
        if (x > LAUNCHER_X1 || x + 200 < LAUNCHER_X0) {
            continue;
        }
        bool here = (refs[i].row == row_selected);
        launcher_text_sized(LAUNCHER_SIZE_BODY, x, CART_LABELS_Y, 0, 20, ALIGN_LEFT, here ? STL_ACCENT : STL_GRAY,
            row_label(rows[refs[i].row].kind));
    }
    clip_none();
}

/** @brief Cartridge's input: left/right along the one row, C left/right to the next or previous list. */
static bool process_cartridge (menu_t *menu) {
    if (!menu->actions.go_left && !menu->actions.go_right) {
        return menu->actions.go_up || menu->actions.go_down;   // Up/down do nothing here.
    }
    int step = menu->actions.go_right ? 1 : -1;
    if (menu->actions.go_fast) {
        int r = row_selected + step;
        if (r >= 0 && r < row_count) {
            row_selected = r;
            rows[r].selected = 0;
            sound_play_effect(SFX_TICK);
        }
        return true;
    }
    game_ref_t refs[ROW_KINDS * MAX_ROW_ITEMS];
    int n = home_games(refs, ROW_KINDS * MAX_ROW_ITEMS);
    int k = refs_focus(refs, n) + step;
    if (k >= 0 && k < n) {
        row_selected = refs[k].row;
        rows[row_selected].selected = refs[k].index;
        sound_play_effect(SFX_TICK);
    }
    return true;
}

/*
 * Minimal: the game names are the menu, gliding up and down through a lane across the screen in the focused cover's
 * colour; the focused name is large (the display font) with its details under it, the others smaller and greyer the
 * further they are. The focused game's cover, large, on the right. Up/down go through every game; left/right jump
 * between the lists.
 */
#define TYPE_CENTER_Y       (204)   /* The lane's middle */
#define TYPE_LANE_H         (100)   /* Room for the name and two lines of details (4:3 wraps them) */
#define TYPE_LANE_BAR       (8)
#define TYPE_COVER_ZOOM     (1.4f)  /* The hi-res pack's own size: drawn 1:1, the fast way (a shrunk cover kept ~43 fps) */
#define TYPE_TOP            (60)    /* The list's area, under the header and above the hints */
#define TYPE_SECTION_GAP    (0.9f)  /* Extra room before each list, in names */
#define TYPE_HEADING_AT     (0.75f) /* Its heading, this far before the list's first name */
#define TYPE_BOTTOM         (408)
#define TYPE_LANE_TINT      (40)
#define TYPE_GLIDE          (0.16f) /* Of the way to the focused game each frame: a slower glide than the default */

static float type_pos = 0.0f;       /* Drawn place in the list (with the room before each list), gliding */

/** @brief A game's place in the list counting the room before each list (the lists are in order). */
static float type_place (const game_ref_t *refs, int k) {
    int lists = 0;
    for (int r = 0; r <= refs[k].row; r++) {
        lists += (rows[r].count > 0);
    }
    return (float)k + TYPE_SECTION_GAP * (float)lists;
}

/** @brief How far from the lane's middle a name sits, by its distance in the list. */
static float type_offset (float d) {
    static const float steps[] = { 0.0f, 76.0f, 114.0f, 144.0f, 170.0f };
    float a = fabsf(d);
    int i = (int)a;
    float v = (i >= 4) ? steps[4] + (a - 4.0f) * 24.0f : steps[i] + (steps[i + 1] - steps[i]) * (a - (float)i);
    return (d < 0.0f) ? -v : v;
}

/* Font sizes, smallest first, and the box each is drawn in. */
static const launcher_text_size_t type_sizes[] = { LAUNCHER_SIZE_BODY, LAUNCHER_SIZE_LARGE, LAUNCHER_SIZE_TITLE,
    LAUNCHER_SIZE_DISPLAY_MID, LAUNCHER_SIZE_DISPLAY };
static const float type_px[] = { 15.0f, 18.0f, 22.0f, 27.0f, 32.0f };
static const int type_box[] = { 22, 26, 30, 36, 42 };
#define TYPE_SIZES          (5)

/** @brief A name's text size by its distance from the lane: 32 px in it, 22 one away, 18 two, 15 beyond. */
static float type_size_for (float d) {
    static const float at[] = { 32.0f, 22.0f, 18.0f, 15.0f };
    float a = fabsf(d);
    int i = (int)a;
    return (i >= 3) ? 15.0f : at[i] + (at[i + 1] - at[i]) * (a - (float)i);
}

/**
 * @brief One name, its middle at mid, in the nearest of the five sizes to its size by distance: it steps up through
 * them as it glides in (crisp; blending two sizes looked doubled). Its colour goes from grey to white as it comes into
 * the lane. Too long for a size: the largest that fits.
 */
static void type_draw_name (int x, int mid, int width, float d, const char *title, bool held) {
    float size = type_size_for(d);
    int fit = TYPE_SIZES - 1;
    while (fit > 0 && launcher_text_sized_width(type_sizes[fit], title) > width) {
        fit--;
    }
    size = (size > type_px[fit]) ? type_px[fit] : size;
    int near = 0;
    for (int i = 1; i <= fit; i++) {
        near = (fabsf(type_px[i] - size) < fabsf(type_px[near] - size)) ? i : near;
    }
    float bright = 1.0f - fabsf(d);     // 1 in the lane, 0 from one name away
    bright = (bright < 0.0f) ? 0.0f : bright;
    // Grey away from the lane, the colour text takes on the theme's feature colour in it; the favorite being moved
    // pulses in the cursor's colour.
    color_t white = theme_current()->on_feature, grey = THEME_TEXT_GRAY;
    color_t c = RGBA32(grey.r + (int)((white.r - grey.r) * bright), grey.g + (int)((white.g - grey.g) * bright),
        grey.b + (int)((white.b - grey.b) * bright), 0xFF);
    int raise = (int)lroundf(16.0f * bright);  // In the lane, lifted to leave room for its details
    if (held) {
        // The favorite being moved sits on its own box that flips between white text on black and black text on
        // white (a hard flip: a fade passes through grey on grey). Its text always has the most contrast there is,
        // whatever the theme: black or white alone vanished on some themes' lanes (Banana, Pocket Green...).
        bool light = ((get_ticks_ms() / 400) % 2) == 0;
        color_t box = light ? RGBA32(0xF0, 0xF0, 0xF0, 0xFF) : RGBA32(0x14, 0x14, 0x14, 0xFF);
        c = light ? RGBA32(0x14, 0x14, 0x14, 0xFF) : RGBA32(0xF0, 0xF0, 0xF0, 0xFF);
        int tw = launcher_text_sized_width(type_sizes[near], title);
        tw = (tw > width) ? width : tw;
        int by = mid - type_box[near] / 2 - raise;
        launcher_fill_rounded(x - 8, by - 1, x + tw + 10, by + type_box[near] + 1, 8, box);
    }
    fonts_set_fade_color(c);
    launcher_text_sized(type_sizes[near], x, mid - type_box[near] / 2 - raise, width, type_box[near], ALIGN_LEFT, STL_FADE,
        title);
}

static void draw_minimal (menu_t *menu, int *lookups_left) {
    const theme_t *t = theme_current();
    fonts_load_display();
    game_ref_t refs[ROW_KINDS * MAX_ROW_ITEMS];
    int n = home_games(refs, ROW_KINDS * MAX_ROW_ITEMS);
    int sel = refs_focus(refs, n);
    if (sel < 0) {
        return;
    }
    float target = type_place(refs, sel);
    if (motion_snap) {
        type_pos = target;
    } else {
        type_pos += (target - type_pos) * TYPE_GLIDE;
        type_pos = (fabsf(target - type_pos) < 0.005f) ? target : type_pos;
    }

    int cover_w = (int)(LAUNCHER_SHOWCASE_W * TYPE_COVER_ZOOM);
    int cover_h = (int)(LAUNCHER_SHOWCASE_H * TYPE_COVER_ZOOM);
    int cover_x = LAUNCHER_X1 - cover_w - 6;
    int cover_y = TYPE_CENTER_Y - cover_h / 2 - 6;
    int list_x = LAUNCHER_X0 + 14;
    int list_w = cover_x - 24 - list_x;

    // The lane across the screen, in the focused cover's colour, with a bar of it at the left edge.
    // The lane in the theme's own feature colour, as Spotlight's panel (the cover's colour changed it every game).
    color_t lane = t->feature;
    color_t bar = launcher_standout_color(lane);    // The second accent is the feature colour in some themes
    int ly0 = TYPE_CENTER_Y - TYPE_LANE_H / 2;
    ui_components_box_draw(0, ly0, display_get_width(), ly0 + TYPE_LANE_H, lane);
    ui_components_box_draw(0, ly0, launcher_px(TYPE_LANE_BAR * (moving ? 2 : 1)), ly0 + TYPE_LANE_H,
        moving ? launcher_moving_color(lane) : bar);    // Wider and pulsing while a favorite is being moved

    bool hires = launcher_covers_have_pack(menu->storage_prefix, LAUNCHER_HIRES_W, LAUNCHER_HIRES_H);
    int clip_x1 = launcher_px(cover_x - 10);
    rdpq_set_scissor(0, TYPE_TOP, clip_x1 < 1 ? 1 : clip_x1, TYPE_BOTTOM);
    for (int k = 0; k < n; k++) {
        float d = type_place(refs, k) - type_pos;
        if (refs[k].index == 0 && fabsf(d - TYPE_HEADING_AT) < 4.6f) {
            // Where a list starts: its name and a line, the focused game's list in the accent colour.
            int hy = TYPE_CENTER_Y + (int)lroundf(type_offset(d - TYPE_HEADING_AT)) - 10;
            bool here = (refs[k].row == row_selected);
            int w = launcher_text_sized(LAUNCHER_SIZE_BODY, list_x, hy, 0, 20, ALIGN_LEFT, here ? STL_ACCENT : STL_GRAY,
                row_label(rows[refs[k].row].kind));
            ui_components_box_draw(launcher_px(list_x + w + 10), hy + 10, launcher_px(list_x + list_w), hy + 11, t->line);
        }
        if (fabsf(d) > 3.6f) {
            continue;
        }
        home_item_t *h = &rows[refs[k].row].items[refs[k].index];
        int mid = TYPE_CENTER_Y + (int)lroundf(type_offset(d));
        type_draw_name(list_x, mid, list_w, d, h->title, moving && k == sel);
        if (abs(k - sel) == 1) {
            keep_loaded(menu, h, hires ? LAUNCHER_HIRES_W : LAUNCHER_SHOWCASE_W, hires ? LAUNCHER_HIRES_H : LAUNCHER_SHOWCASE_H,
                lookups_left);
        }
    }
    clip_none();

    home_item_t *f = focused_item();
    if (f) {
        char details[160];
        library_details_for_path(menu, item_path(f), details, sizeof(details));
        float off = fabsf(target - type_pos);
        if (details[0] && off < 0.5f) {
            int dy = (int)lroundf(type_offset(target - type_pos));
            color_t c = t->on_feature;
            c.a = (uint8_t)(255.0f * (1.0f - off * 2.0f));
            fonts_set_fade_color(c);
            // Whole fields to a line, up to two lines (4:3 is narrower), never cut short.
            launcher_game_details_wrapped(list_x, TYPE_CENTER_Y + 6 + dy, list_w, 2, STL_FADE, details);
        }
        bool pending;
        float zoom;
        surface_t *image = big_cover(menu, f, TYPE_COVER_ZOOM, lookups_left, &pending, &zoom);
        launcher_draw_tile_zoomed(cover_x, cover_y, cover_w, cover_h, image, pending, f->title, false, t->bg, zoom);
        launcher_note_focused_cover_zoomed(image, cover_x, cover_y, cover_w, cover_h, zoom);
        char where[96], folder[64];
        item_folder(f, folder, sizeof(folder));
        snprintf(where, sizeof(where), "%s   %d of %d", folder, sel + 1, n);
        launcher_name(cover_x, cover_y + cover_h + 10, cover_w, ALIGN_RIGHT, STL_GRAY, where);
    }
}

/** @brief Minimal's input: up/down through every game, left/right to the next or previous list. */
static bool process_minimal (menu_t *menu) {
    game_ref_t refs[ROW_KINDS * MAX_ROW_ITEMS];
    int n = home_games(refs, ROW_KINDS * MAX_ROW_ITEMS);
    int sel = refs_focus(refs, n);
    if ((menu->actions.go_left || menu->actions.go_right) && row_count > 1) {
        int r = row_selected + (menu->actions.go_right ? 1 : -1);
        if (r >= 0 && r < row_count) {
            row_selected = r;
            rows[r].selected = 0;
            sound_play_effect(SFX_TICK);
        }
        return true;
    }
    int next = menu->actions.go_up ? sel - 1 : (menu->actions.go_down ? sel + 1 : -2);
    if (next == -2) {
        return false;
    }
    if (next >= 0 && next < n) {
        row_selected = refs[next].row;
        rows[row_selected].selected = refs[next].index;
        sound_play_effect(SFX_TICK);
    }
    return true;
}

/** @brief Draw Home in one of the slot-based styles (not Binder Mosaic). */
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
    if ((style == LAUNCHER_HOME_MINIMAL && process_minimal(menu)) || (style == LAUNCHER_HOME_CARTRIDGE && process_cartridge(menu))) {
        return;
    }
    int dx = menu->actions.go_left ? -1 : (menu->actions.go_right ? 1 : 0);
    int dy = menu->actions.go_up ? -1 : (menu->actions.go_down ? 1 : 0);
    if (dx || dy) {
        if (slot_move(dx, dy)) {
            sound_play_effect(SFX_TICK);
        }
        return;
    }
    home_item_t *f = focused_item();
    if (menu->actions.enter && f) {
        launch(menu, f);
    }
}

/** @brief The header and footer a style draws with (set before the header is drawn). */
static void set_style_chrome (int style) {
    switch (style) {
        case LAUNCHER_HOME_CARTRIDGE:
            cart_background();
            launcher_hide_footer_line();
            break;
        case LAUNCHER_HOME_MINIMAL:
        case LAUNCHER_HOME_BINDER:      // Its wedge runs under the hints: no line across it.
            launcher_hide_footer_line();
            break;
        case LAUNCHER_HOME_SPOTLIGHT: {
            // The panel under the header: Home / Library sit on it in its text colour.
            const theme_t *t = theme_current();
            launcher_set_background_band(LAUNCHER_SCREEN_W * SPOT_TOP_SHARE / 100, LAUNCHER_SCREEN_W * SPOT_BOTTOM_SHARE / 100,
                display_get_height(), t->feature, t->feature, 0);
            launcher_set_header_on_feature(true);
            launcher_hide_footer_line();
            break;
        }
        default:
            break;
    }
}

/*
 * Binder Mosaic: Home's lists are binder tabs down the left (hidden when only one list is on); the open list is a
 * page beside them, laid out as a mosaic. Its first game is big, the next ones fill the space beside it and the rest
 * go small beneath; the selected game's full name and details sit under the big tile. A list of one game is a single
 * large card (Resume's always is). Every tile is a cover pack's own size, drawn 1:1.
 */
#define BINDER_RAIL_W_WIDE  (147)   /* The tabs' width: "Recently played" with room around it */
#define BINDER_RAIL_W_43    (132)
#define BINDER_RAIL_GAP_WIDE (12)
#define BINDER_RAIL_GAP_43  (10)
#define BINDER_TAB_H        (40)
#define BINDER_TAB_GAP      (6)
#define BINDER_TAB_RADIUS   (8)
#define BINDER_PAD          (12)    /* Inside the page */
#define BINDER_GAP          (12)    /* After the big tile, and between medium tiles */
#define BINDER_SMALL_GAP    (8)     /* At least this between small tiles (spread to fill their space) */
#define BINDER_MED_W        (133)   /* The medium pack */
#define BINDER_MED_H        (94)
#define BINDER_SMALL_W      (87)    /* The small pack */
#define BINDER_SMALL_H      (61)
#define BINDER_SLIDE        (24)    /* A newly opened page slides this far into place */
#define BINDER_DETAILS_LINE (20)
#define BINDER_WEDGE_Y      (262)   /* The lower-left wedge: from the left edge here... */
#define BINDER_WEDGE_FOOT_W (250)   /* ...to this far along the foot of the screen */
#define BINDER_WEDGE_EDGE   (12)    /* Its darker edge */

typedef struct {
    int x, y, w, h;
    bool big;
} binder_cell_t;

typedef struct {
    row_kind_t kind;
    int row;                    /**< Into rows; -1 when the list is on but has no games yet */
} binder_tab_t;

static bool binder_rail = false;            /* The tabs have the focus, not the page */
static row_kind_t binder_kind = ROW_RESUME; /* The open tab */
static float binder_slide = 0.0f;
static int binder_scroll = 0;               /* The tiles beside the big one: how far scrolled... */
static float binder_scroll_pos = 0.0f;      /* ...and drawn, gliding there */

static bool binder_wide (void) {
    return LAUNCHER_SCREEN_W > 640;
}

/** @brief The tabs: each list that's on, in order (Recently played and Favorites even while they're empty). */
static int binder_tabs (menu_t *menu, binder_tab_t *tabs) {
    bool on[ROW_KINDS] = { menu->settings.launcher_home_resume, menu->settings.launcher_home_recent,
        menu->settings.launcher_home_favorites };
    int n = 0;
    for (int k = 0; k < ROW_KINDS; k++) {
        int r = row_of_kind((row_kind_t)k);
        if (on[k] && (r >= 0 || k != ROW_RESUME)) {
            tabs[n++] = (binder_tab_t) { (row_kind_t)k, r };
        }
    }
    return n;
}

/** @brief The open tab (the selected list's when the open one went away), its list made the selected row. */
static int binder_current (binder_tab_t *tabs, int n) {
    int cur = -1;
    for (int i = 0; i < n && cur < 0; i++) {
        cur = (tabs[i].kind == binder_kind) ? i : cur;
    }
    for (int i = 0; i < n && cur < 0; i++) {
        cur = (tabs[i].row == row_selected) ? i : cur;
    }
    cur = (cur < 0) ? 0 : cur;
    binder_kind = tabs[cur].kind;
    if (tabs[cur].row >= 0) {
        row_selected = tabs[cur].row;
    }
    return cur;
}

static void binder_open (binder_tab_t tab, int direction) {
    binder_kind = tab.kind;
    if (tab.row >= 0) {
        row_selected = tab.row;
    }
    binder_slide = (float)(BINDER_SLIDE * direction);
    binder_scroll = 0;
    binder_scroll_pos = 0.0f;
}

/** @brief The page's edges: beside the tabs, or the whole width when there are none. */
static void binder_page (bool rail, int *x0, int *x1) {
    *x0 = LAUNCHER_X0 + (rail ? (binder_wide() ? BINDER_RAIL_W_WIDE + BINDER_RAIL_GAP_WIDE : BINDER_RAIL_W_43 + BINDER_RAIL_GAP_43) : 0);
    *x1 = LAUNCHER_X1;
}

/** @brief The big tile's size: the hi-res pack at 16:9, the showcase pack at 4:3 (both drawn 1:1). */
static void binder_big_size (int *w, int *h) {
    *w = binder_wide() ? LAUNCHER_HIRES_W : LAUNCHER_SHOWCASE_W;
    *h = binder_wide() ? LAUNCHER_HIRES_H : LAUNCHER_SHOWCASE_H;
}

/**
 * @brief Where a list's n games go on a page from x0 to x1: the big tile, then the rest all one size in a column of
 * tiles beside it, row by row, running on past the page's foot (it scrolls). *cols: the tiles across.
 */
static int binder_layout (int n, int x0, int x1, binder_cell_t *cells, int *cols) {
    int ix0 = x0 + BINDER_PAD, iy0 = CONTENT_TOP + BINDER_PAD, ix1 = x1 - BINDER_PAD;
    int bw, bh;
    binder_big_size(&bw, &bh);
    *cols = 1;
    if (n <= 0) {
        return 0;
    }
    cells[0] = (binder_cell_t) { ix0, iy0, bw, bh, true };
    if (n == 1) {
        return 1;   // The details go beside it.
    }
    bool wide = binder_wide();
    int rx = ix0 + bw + BINDER_GAP;
    int tw = wide ? BINDER_MED_W : BINDER_SMALL_W, th = wide ? BINDER_MED_H : BINDER_SMALL_H;
    int gap = wide ? BINDER_GAP : BINDER_SMALL_GAP;
    int c_n = (ix1 - rx + gap) / (tw + gap);
    c_n = (c_n < 1) ? 1 : c_n;
    int g = (c_n > 1) ? (ix1 - rx - c_n * tw) / (c_n - 1) : gap;    // Spread to the page's right edge
    for (int k = 1; k < n; k++) {
        int c = (k - 1) % c_n, r = (k - 1) / c_n;
        cells[k] = (binder_cell_t) { rx + c * (tw + g), iy0 + r * (th + g), tw, th, false };
    }
    *cols = c_n;
    return n;
}

/** @brief A tile's cover: the big one from the hi-res pack when it fills a 16:9 big tile. */
static surface_t *binder_image (menu_t *menu, home_item_t *h, binder_cell_t *c, int *lookups_left, bool *pending,
    float *zoom) {
    if (c->big && binder_wide()) {
        return big_cover(menu, h, (float)LAUNCHER_HIRES_W / LAUNCHER_SHOWCASE_W, lookups_left, pending, zoom);
    }
    *zoom = 1.0f;
    return item_image(menu, h, c->w, c->h, lookups_left, pending);
}

/** @brief The other tabs' covers stay loaded, so opening one finds them ready. */
static void binder_keep_others (menu_t *menu, int open_row, int x0, int x1, int *lookups_left) {
    binder_cell_t cells[MAX_ROW_ITEMS];
    for (int r = 0; r < row_count; r++) {
        if (r == open_row) {
            continue;
        }
        int cols;
        int n = binder_layout(rows[r].count, x0, x1, cells, &cols);
        for (int i = 0; i < n; i++) {
            bool hires = cells[i].big && binder_wide() && launcher_covers_have_pack(menu->storage_prefix, LAUNCHER_HIRES_W,
                LAUNCHER_HIRES_H);
            keep_loaded(menu, &rows[r].items[i], hires ? LAUNCHER_HIRES_W : (cells[i].big && binder_wide() ?
                LAUNCHER_SHOWCASE_W : cells[i].w), hires ? LAUNCHER_HIRES_H : (cells[i].big && binder_wide() ?
                LAUNCHER_SHOWCASE_H : cells[i].h), lookups_left);
        }
    }
}

/** @brief A game's details line; Resume's adds the folder it's in. */
static void binder_details (menu_t *menu, home_item_t *h, bool resume, char *line, size_t size) {
    char details[160];
    library_details_for_path(menu, item_path(h), details, sizeof(details));
    if (resume) {
        char folder[64];
        item_folder(h, folder, sizeof(folder));
        snprintf(line, size, "%s%s%s", details, details[0] ? "   \xC2\xB7   " : "", folder);
    } else {
        snprintf(line, size, "%s", details);
    }
}

/** @brief The tabs down the left: the open one joins the page (or stands apart, ringed, while the tabs have the focus). */
static void binder_draw_tabs (binder_tab_t *tabs, int n, int cur, int page_x0) {
    const theme_t *t = theme_current();
    int rail_w = binder_wide() ? BINDER_RAIL_W_WIDE : BINDER_RAIL_W_43;
    for (int i = 0; i < n; i++) {
        int y = CONTENT_TOP + i * (BINDER_TAB_H + BINDER_TAB_GAP);
        bool open = (i == cur);
        int x1 = (open && !binder_rail) ? page_x0 + CARD_RADIUS : LAUNCHER_X0 + rail_w;
        if (open && binder_rail) {
            launcher_draw_focus_ring(LAUNCHER_X0, y, x1, y + BINDER_TAB_H, BINDER_TAB_RADIUS);
        }
        launcher_fill_rounded(LAUNCHER_X0, y, x1, y + BINDER_TAB_H, BINDER_TAB_RADIUS, open ? t->panel :
            color_mix(t->bg, t->panel, 45));
        if (open) {
            launcher_fill_rounded(LAUNCHER_X0, y + 8, LAUNCHER_X0 + 5, y + BINDER_TAB_H - 8, 2, t->accent);
        }
        launcher_text_sized(LAUNCHER_SIZE_BODY, LAUNCHER_X0 + 14, y + (BINDER_TAB_H - 20) / 2 + 1, rail_w - 18, 20,
            ALIGN_LEFT, open ? STL_DEFAULT : STL_GRAY, row_label(tabs[i].kind));
    }
}

static void draw_binder (menu_t *menu, int *lookups_left) {
    const theme_t *t = theme_current();
    slot_total = 0;
    binder_tab_t tabs[ROW_KINDS];
    int n = binder_tabs(menu, tabs);
    if (n == 0) {
        return;
    }
    int cur = binder_current(tabs, n);
    bool rail = (n > 1);
    binder_rail = binder_rail && rail;
    int x0, x1;
    binder_page(rail, &x0, &x1);
    // The lower left: a wedge in the theme's feature colour, behind the tabs and the page, with a darker edge.
    int foot = display_get_height();
    launcher_fill_slanted(0, BINDER_WEDGE_FOOT_W + BINDER_WEDGE_EDGE, BINDER_WEDGE_Y - BINDER_WEDGE_EDGE, foot,
        color_mix(t->feature, t->bg, 40));
    launcher_fill_slanted(0, BINDER_WEDGE_FOOT_W, BINDER_WEDGE_Y, foot, t->feature);
    if (rail) {
        binder_draw_tabs(tabs, n, cur, x0);
    }
    launcher_fill_rounded(x0, CONTENT_TOP, x1, CONTENT_BOTTOM, CARD_RADIUS, t->panel);

    binder_slide = motion_snap ? 0.0f : launcher_ease(binder_slide, 0.0f);
    int dy = (int)lroundf(binder_slide);
    int clip_x0 = launcher_px(x0), clip_x1 = launcher_px(x1);
    clip_x0 = clip_x0 < 0 ? 0 : clip_x0;
    clip_x1 = clip_x1 > display_get_width() ? display_get_width() : (clip_x1 < 1 ? 1 : clip_x1);
    rdpq_set_scissor(clip_x0, CONTENT_TOP, clip_x1, CONTENT_BOTTOM);

    int ix0 = x0 + BINDER_PAD, ix1 = x1 - BINDER_PAD, iy0 = CONTENT_TOP + BINDER_PAD, iy1 = CONTENT_BOTTOM - BINDER_PAD;
    if (tabs[cur].row < 0) {
        // A list that's on but empty says how it fills.
        bool fav = (tabs[cur].kind == ROW_FAVORITES);
        int my = (iy0 + iy1) / 2 - 30 + dy;
        launcher_text_sized(LAUNCHER_SIZE_TITLE, ix0, my, ix1 - ix0, 30, ALIGN_CENTER, STL_DEFAULT,
            fav ? "No favorites yet" : "Nothing played yet");
        launcher_text_sized(LAUNCHER_SIZE_BODY, ix0, my + 38, ix1 - ix0, 20, ALIGN_CENTER, STL_GRAY,
            fav ? "In the Library, press Start on a game: Add to favorites" : "Games you play will show up here");
        rdpq_set_scissor(0, 0, display_get_width(), display_get_height());
        binder_keep_others(menu, -1, x0, x1, lookups_left);
        return;
    }

    home_row_t *row = &rows[tabs[cur].row];
    binder_cell_t cells[MAX_ROW_ITEMS];
    int cols;
    int count = binder_layout(row->count, x0, x1, cells, &cols);
    bool page_focus = !binder_rail;
    // The column beside the big tile scrolls to keep the selected game in view.
    if (count > 1 && row->selected > 0) {
        binder_cell_t *sc = &cells[row->selected];
        if (sc->y - binder_scroll < iy0) {
            binder_scroll = sc->y - iy0;
        }
        if (sc->y + sc->h - binder_scroll > iy1) {
            binder_scroll = sc->y + sc->h - iy1;
        }
    }
    binder_scroll_pos = motion_snap ? (float)binder_scroll : launcher_ease(binder_scroll_pos, (float)binder_scroll);
    int scroll = (int)lroundf(binder_scroll_pos);
    for (int i = 0; i < count; i++) {
        binder_cell_t *c = &cells[i];
        home_item_t *h = &row->items[i];
        int y = c->y + dy - (c->big ? 0 : scroll);
        slot_add(c->x, c->y - (c->big ? 0 : binder_scroll), c->w, c->h, tabs[cur].row, i);
        if (y + c->h < CONTENT_TOP || y > CONTENT_BOTTOM) {
            continue;   // Scrolled out of the page.
        }
        bool pending;
        float zoom;
        surface_t *image = binder_image(menu, h, c, lookups_left, &pending, &zoom);
        launcher_draw_tile_zoomed(c->x, y, c->w, c->h, image, pending, h->title, false, t->panel, zoom);
        if (row->kind != ROW_FAVORITES && launcher_is_favorite(menu, item_path(h))) {
            launcher_draw_favorite_star(c->x, y, c->w);
        }
        if (i == row->selected) {
            launcher_note_focused_cover_zoomed(image, c->x, y, c->w, c->h, zoom);
            if (page_focus) {
                launcher_draw_focus_ring_cover_zoomed(c->x, y, c->w, c->h, image, zoom);
            }
        }
    }

    // The selected game's name and details: beside a lone game, else under the big tile.
    home_item_t *f = &row->items[row->selected];
    bool resume = (row->kind == ROW_RESUME);
    char line[240];
    binder_details(menu, f, resume, line, sizeof(line));
    if (count == 1) {
        int tx = cells[0].x + cells[0].w + BINDER_GAP + 4, tw = ix1 - tx, ty = iy0 + 4 + dy;
        launcher_text_sized(LAUNCHER_SIZE_BODY, tx, ty, tw, 20, ALIGN_LEFT, STL_ACCENT, row_label(row->kind));
        ty += 24;
        ty += launcher_title_wrapped(tx, ty, tw, 3, STL_DEFAULT, f->title) + 4;
        if (line[0]) {
            ty += launcher_game_details_wrapped(tx, ty, tw, 3, STL_GRAY, line) + 10;
        }
        int py = cells[0].y + cells[0].h - 24 + dy;
        launcher_draw_prompt(tx, ty > py ? ty : py, LAUNCHER_BUTTON_A, "A",
            resume ? "Resume" : (menu->settings.launcher_quick_launch ? "Play" : "Details"), t->bg);
    } else {
        int tx = cells[0].x, tw = cells[0].w, ty = cells[0].y + cells[0].h + BINDER_GAP + dy;
        ty += launcher_title_wrapped(tx, ty, tw, 2, STL_DEFAULT, f->title) + 2;
        int lines = (iy1 + dy - ty) / BINDER_DETAILS_LINE;
        if (line[0] && lines > 0) {
            launcher_game_details_wrapped(tx, ty, tw, lines > 3 ? 3 : lines, STL_GRAY, line);
        }
    }
    rdpq_set_scissor(0, 0, display_get_width(), display_get_height());
    binder_keep_others(menu, tabs[cur].row, x0, x1, lookups_left);
}

/** @brief Binder Mosaic's input: the d-pad moves around the page, left from its edge to the tabs, up/down past its
 * top or foot to the tab before or after; on the tabs, up/down opens one and right or A goes into its page. */
static void process_binder (menu_t *menu) {
    binder_tab_t tabs[ROW_KINDS];
    int n = binder_tabs(menu, tabs);
    if (n == 0) {
        return;
    }
    int cur = binder_current(tabs, n);
    bool empty = (tabs[cur].row < 0);
    binder_rail = (n > 1) && (binder_rail || empty);
    int dx = menu->actions.go_left ? -1 : (menu->actions.go_right ? 1 : 0);
    int dy = menu->actions.go_up ? -1 : (menu->actions.go_down ? 1 : 0);
    if (binder_rail) {
        if (dy && cur + dy >= 0 && cur + dy < n) {
            binder_open(tabs[cur + dy], dy);
            sound_play_effect(SFX_TICK);
        } else if ((dx > 0 || menu->actions.enter) && !empty) {
            binder_rail = false;
            sound_play_effect(SFX_TICK);
        }
        return;
    }
    if (dx || dy) {
        if (slot_move(dx, dy)) {
            sound_play_effect(SFX_TICK);
        } else if (dx < 0 && n > 1) {
            binder_rail = true;
            sound_play_effect(SFX_TICK);
        } else if (dy && cur + dy >= 0 && cur + dy < n && tabs[cur + dy].row >= 0) {
            binder_open(tabs[cur + dy], dy);
            sound_play_effect(SFX_TICK);
        }
        return;
    }
    home_item_t *f = focused_item();
    if (menu->actions.enter && f) {
        launch(menu, f);
    }
}

/** @brief The hints: Z Move on a favorite (at least two), and while one is moving, how to move and place it. */
static void draw_home_hints (menu_t *menu) {
    if (moving) {
        launcher_draw_footer(hints_moving, HINT_COUNT(hints_moving));
        return;
    }
    if (menu->settings.launcher_home_style == LAUNCHER_HOME_BINDER && binder_rail) {
        if (row_of_kind(binder_kind) >= 0) {
            launcher_draw_footer(hints_tabs, HINT_COUNT(hints_tabs));
        } else {
            launcher_draw_footer(hints_empty, HINT_COUNT(hints_empty));
        }
        return;
    }
    const char *play = menu->settings.launcher_quick_launch ? "Play" : "Details";
    home_item_t *f = focused_item();
    int r = row_of_kind(ROW_FAVORITES);
    if (f && f->favorite && r >= 0 && row_selected == r && rows[r].count > 1) {
        hints_home_favorite[0].label = play;
        launcher_draw_footer(hints_home_favorite, HINT_COUNT(hints_home_favorite));
        return;
    }
    hints_home[0].label = play;
    launcher_draw_footer(hints_home, HINT_COUNT(hints_home));
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    launcher_covers_begin_frame();
    launcher_covers_pin(true);      // Home's own covers (not the read-ahead below): kept over the library's.
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
    } else if (menu->settings.launcher_home_style != LAUNCHER_HOME_BINDER) {
        draw_styled(menu, menu->settings.launcher_home_style, &lookups_left);
        motion_snap = false;
        home_background_work(menu, &lookups_left);
        draw_home_hints(menu);
    } else {
        draw_binder(menu, &lookups_left);
        rdpq_set_scissor(0, 0, display_get_width(), display_get_height());
        motion_snap = false;
        home_background_work(menu, &lookups_left);
        draw_home_hints(menu);
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
                reveal_frames, !timed_out ? "covers in, frames steady" : (covers_pending ? "timed out, covers still loading" :
                "timed out, frames not steady"));
            perf_log_event("startup: last hidden frame %lu ms, %d quick frames in a row", (unsigned long)frame_ms, reveal_steady);
            heap_stats_t heap;
            sys_get_heap_stats(&heap);
            perf_log_event("after startup: heap %d KB free (%d KB of it in gaps between allocations)", heap.free / 1024, heap.fragmented / 1024);
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
    if (moving) {
        moving = false;
        launcher_set_focus_moving(false);
        memcpy(menu->bookkeeping.favorite_items, moving_backup, sizeof(moving_backup));
    }
    launcher_reset_idle();  // Time spent on other screens isn't idle time here (attract mode started on arrival).
    packs_open = false;     // Leaving the launcher closes them.
    launcher_covers_pin_new_set();
    if (reveal == REVEAL_HIDDEN) {
        // The startup work behind black makes frames of up to ~440 ms, longer than the menu's usual ~140 ms of
        // music queued ahead (the music stuttered): every buffer is kept full, as for a game load, until Home shows.
        sound_bgm_begin_load();
    }
    launcher_menu_init();
    build_rows(menu);
    motion_snap = true;
    launcher_focus_reset();
    binder_rail = false;
    binder_kind = (row_count > 0) ? rows[row_selected].kind : ROW_RESUME;
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
