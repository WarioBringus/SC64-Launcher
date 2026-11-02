/**
 * @file home.c
 * @brief Launcher home screen: resume the last game, recently played and favorites.
 * @ingroup view
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "views.h"
#include "../bookkeeping.h"
#include "../launcher_ui.h"
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

static const launcher_hint_t hints_home[] = {
    { LAUNCHER_BUTTON_A, "A", "Play" },
    { LAUNCHER_BUTTON_OTHER, "R", "Library" },
    { LAUNCHER_BUTTON_START, "S", "Menu" },
};

static const launcher_hint_t hints_favorites[] = {
    { LAUNCHER_BUTTON_A, "A", "Play" },
    { LAUNCHER_BUTTON_OTHER, "Z", "Remove" },
    { LAUNCHER_BUTTON_OTHER, "R", "Library" },
    { LAUNCHER_BUTTON_START, "S", "Menu" },
};

static const launcher_hint_t hints_empty[] = {
    { LAUNCHER_BUTTON_OTHER, "R", "Library" },
    { LAUNCHER_BUTTON_START, "S", "Menu" },
};

#define HINT_COUNT(h) ((int)(sizeof(h) / sizeof((h)[0])))

typedef enum {
    ROW_RESUME,
    ROW_RECENT,
    ROW_FAVORITES,
} row_kind_t;

typedef struct {
    bool favorite;              /**< Index refers to favorite_items rather than history_items */
    int index;                  /**< Index into the bookkeeping list */
    bookkeeping_item_t *item;
    char *title;
} home_item_t;

typedef struct {
    row_kind_t kind;
    home_item_t items[MAX_ROW_ITEMS];
    int count;
    int selected;
    int scroll;
} home_row_t;

/* ROM path -> cover path, kept across visits so covers are only looked up once. */
typedef struct {
    char *rom_path;
    char *cover_path;
} cover_lookup_t;

static home_row_t rows[3];
static int row_count = 0;
static int row_selected = 0;
static cover_lookup_t cover_lookups[COVER_CACHE_SIZE];
static int cover_lookup_next = 0;


static void free_rows (void) {
    for (int r = 0; r < row_count; r++) {
        for (int i = 0; i < rows[r].count; i++) {
            free(rows[r].items[i].title);
        }
        rows[r].count = 0;
    }
    row_count = 0;
}

static void add_item (home_row_t *row, bookkeeping_item_t *item, bool favorite, int index) {
    if (item->bookkeeping_type == BOOKKEEPING_TYPE_EMPTY || !item->primary_path || row->count >= MAX_ROW_ITEMS) {
        return;
    }
    home_item_t *h = &row->items[row->count++];
    h->favorite = favorite;
    h->index = index;
    h->item = item;
    h->title = library_title_from_path(item->primary_path);
}

static void build_rows (menu_t *menu) {
    int previous_selected[3] = { 0, 0, 0 };
    for (int r = 0; r < row_count; r++) {
        previous_selected[rows[r].kind] = rows[r].selected;
    }
    free_rows();

    bookkeeping_t *b = &menu->bookkeeping;
    home_row_t built[3];
    memset(built, 0, sizeof(built));

    built[ROW_RESUME].kind = ROW_RESUME;
    add_item(&built[ROW_RESUME], &b->history_items[0], false, 0);

    built[ROW_RECENT].kind = ROW_RECENT;
    // With the resume card shown, the first history entry is already on screen.
    for (int i = menu->settings.launcher_home_resume ? 1 : 0; i < HISTORY_COUNT; i++) {
        add_item(&built[ROW_RECENT], &b->history_items[i], false, i);
    }

    built[ROW_FAVORITES].kind = ROW_FAVORITES;
    for (int i = 0; i < FAVORITES_COUNT; i++) {
        add_item(&built[ROW_FAVORITES], &b->favorite_items[i], true, i);
    }

    bool show[3] = {
        menu->settings.launcher_home_resume,
        menu->settings.launcher_home_recent,
        menu->settings.launcher_home_favorites,
    };
    for (int k = 0; k < 3; k++) {
        if (show[k] && built[k].count > 0) {
            rows[row_count] = built[k];
            rows[row_count].selected = previous_selected[k];
            if (rows[row_count].selected >= rows[row_count].count) {
                rows[row_count].selected = rows[row_count].count - 1;
            }
            rows[row_count].scroll = 0;
            row_count++;
        } else {
            for (int i = 0; i < built[k].count; i++) {
                free(built[k].items[i].title);
            }
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
    const char *rom = path_get(h->item->primary_path);
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
    slot->cover_path = library_find_cover(menu, h->item->primary_path, h->title);
    *pending = false;
    return slot->cover_path;
}

static surface_t *item_image (menu_t *menu, home_item_t *h, int w, int hgt, int *lookups_left, bool *pending) {
    const char *path = item_cover(menu, h, lookups_left, pending);
    if (!path) {
        return NULL;
    }
    return launcher_cover_get(path, w, hgt, pending);
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
    launcher_leave(menu, (h->item->bookkeeping_type == BOOKKEEPING_TYPE_DISK) ? MENU_MODE_LOAD_DISK : MENU_MODE_LOAD_ROM);
}

static void process (menu_t *menu) {
    if (launcher_menu_process(menu)) {
        return;
    }

    if (menu->actions.settings) {
        launcher_menu_show();
        return;
    }
    if (menu->actions.shoulder_r) {
        sound_play_effect(SFX_CURSOR);
        menu->next_mode = MENU_MODE_LIBRARY;
        return;
    }
    if (row_count == 0) {
        return;
    }

    home_row_t *row = &rows[row_selected];

    if (menu->actions.go_up && row_selected > 0) {
        row_selected--;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_down && row_selected < row_count - 1) {
        row_selected++;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_left && row->selected > 0) {
        row->selected--;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_right && row->selected < row->count - 1) {
        row->selected++;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.enter && row->count > 0) {
        launch(menu, &row->items[row->selected]);
    } else if (menu->actions.trigger_z && row->kind == ROW_FAVORITES && row->count > 0) {
        bookkeeping_favorite_remove(&menu->bookkeeping, row->items[row->selected].index);
        build_rows(menu);
        sound_play_effect(SFX_SETTING);
    }
}

static int draw_resume (menu_t *menu, home_row_t *row, int y, bool focused, int *lookups_left) {
    home_item_t *h = &row->items[0];
    if (focused) {
        launcher_draw_ring(LAUNCHER_X0, y, LAUNCHER_X1, y + RESUME_CARD_H, CARD_RADIUS);
    }
    launcher_fill_rounded(LAUNCHER_X0, y, LAUNCHER_X1, y + RESUME_CARD_H, CARD_RADIUS, theme_current()->panel);

    bool pending;
    int cover_y = y + (RESUME_CARD_H - LAUNCHER_COVER_H) / 2;
    surface_t *image = item_image(menu, h, LAUNCHER_COVER_W, LAUNCHER_COVER_H, lookups_left, &pending);
    launcher_draw_tile(RESUME_COVER_X, cover_y, LAUNCHER_COVER_W, LAUNCHER_COVER_H, image, pending, h->title, false, theme_current()->panel);

    // The folder the game lives in, e.g. "N64 USA".
    char folder[64] = "";
    path_t *dir = path_clone(h->item->primary_path);
    path_pop(dir);
    snprintf(folder, sizeof(folder), "%s", path_last_get(dir));
    path_free(dir);

    int text_w = LAUNCHER_X1 - RESUME_TEXT_X - 14;
    launcher_text(RESUME_TEXT_X, y + 28, text_w, ALIGN_LEFT, STL_ACCENT, "Resume");
    char where[96];
    snprintf(where, sizeof(where), "Last played  -  %s", folder);
    launcher_title(RESUME_TEXT_X, y + 50, text_w, ALIGN_LEFT, STL_DEFAULT, h->title);
    launcher_name(RESUME_TEXT_X, y + 84, text_w, ALIGN_LEFT, STL_GRAY, where);
    return y + RESUME_CARD_H + ROW_GAP;
}

static int draw_row (menu_t *menu, home_row_t *row, int y, bool focused, int *lookups_left) {
    launcher_text(LAUNCHER_X0, y, 0, ALIGN_LEFT, focused ? STL_DEFAULT : STL_GRAY, "%s",
        row->kind == ROW_FAVORITES ? "Favorites" : "Recently played");

    // Keep the highlighted tile inside the visible part of the row.
    if (row->selected < row->scroll) {
        row->scroll = row->selected;
    }
    if (row->selected >= row->scroll + ROW_VISIBLE_TILES) {
        row->scroll = row->selected - ROW_VISIBLE_TILES + 1;
    }

    bool highlight_only = menu->settings.launcher_titles_highlight_only;
    int tile_y = y + ROW_LABEL_H;
    for (int i = row->scroll; i < row->count && i < row->scroll + ROW_VISIBLE_TILES; i++) {
        home_item_t *h = &row->items[i];
        int x = LAUNCHER_X0 + (i - row->scroll) * ROW_TILE_STEP;
        bool selected = focused && (i == row->selected);
        bool pending;
        surface_t *image = item_image(menu, h, ROW_TILE_W, ROW_TILE_H, lookups_left, &pending);
        launcher_draw_tile(x, tile_y, ROW_TILE_W, ROW_TILE_H, image, pending, h->title, selected, theme_current()->bg);
        if (selected || !highlight_only) {
            int extra = selected && highlight_only ? 60 : 3;
            int tx = x - extra;
            int tw = ROW_TILE_W + extra * 2;
            if (tx < LAUNCHER_X0) {
                tw -= LAUNCHER_X0 - tx;
                tx = LAUNCHER_X0;
            }
            if (tx + tw > LAUNCHER_X1) {
                tw = LAUNCHER_X1 - tx;
            }
            launcher_name(tx, tile_y + ROW_TILE_H + LAUNCHER_RING + 3, tw, ALIGN_CENTER, selected ? STL_DEFAULT : STL_GRAY, h->title);
        }
    }
    return y + ROW_H;
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    launcher_covers_begin_frame();
    launcher_draw_header(menu, LAUNCHER_TAB_HOME);

    int lookups_left = 1;
    if (row_count == 0) {
        launcher_text(LAUNCHER_X0, 200, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "Games you play and favorite will show up here");
        launcher_text(LAUNCHER_X0, 224, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "Press R to browse your library");
        launcher_draw_footer(hints_empty, HINT_COUNT(hints_empty));
    } else {
        int y = LAUNCHER_SUBHEADER_Y + 2;
        for (int r = 0; r < row_count; r++) {
            bool focused = (r == row_selected);
            if (rows[r].kind == ROW_RESUME) {
                y = draw_resume(menu, &rows[r], y, focused, &lookups_left);
            } else {
                y = draw_row(menu, &rows[r], y, focused, &lookups_left);
            }
        }
        if (rows[row_selected].kind == ROW_FAVORITES) {
            launcher_draw_footer(hints_favorites, HINT_COUNT(hints_favorites));
        } else {
            launcher_draw_footer(hints_home, HINT_COUNT(hints_home));
        }
    }

    launcher_menu_draw();

    rdpq_detach_show();
}


void view_home_init (menu_t *menu) {
    launcher_menu_init();
    build_rows(menu);
}

void view_home_display (menu_t *menu, surface_t *display) {
    launcher_set_widescreen(menu->settings.launcher_widescreen);

    process(menu);

    draw(menu, display);
}
