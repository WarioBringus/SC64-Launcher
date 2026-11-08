/**
 * @file library_view.c
 * @brief Launcher library: games of one category as a grid of covers or a carousel.
 * @ingroup view
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "views.h"
#include "../launcher_ui.h"
#include "../library.h"
#include "../settings.h"
#include "../sound.h"
#include "../theme.h"

#define MAX_REMEMBERED_CATEGORIES   (32)
#define TITLE_EXTRA_HIGHLIGHT_ONLY  (48)

#define CAROUSEL_CENTER_Y           (214)   /* Vertical center of the covers */
#define CAROUSEL_SIDE_SCALE         (0.66f) /* Size of the covers beside the highlighted one */
#define CAROUSEL_SPACING            (14)
#define CAROUSEL_REACH              (5)     /* Covers considered on each side (off-screen ones are skipped) */
#define CAROUSEL_EASE               (0.35f) /* Fraction of the remaining distance moved per frame */
#define CAROUSEL_FAST_STEP          (5)
#define CAROUSEL_TITLE_Y            (334)

#define GROUP_BADGE_HEIGHT          (64)
#define GROUP_BADGE_MIN_WIDTH       (84)
#define GROUP_BADGE_PADDING         (28)
#define GROUP_BADGE_MS              (800)

#define INFO_LINE_Y                 (398)   /* Game details under the grid, above the hints */
#define NEW_GAMES_CHECK_US          (2000)  /* Time per frame spent looking for new ROMs */
#define PICKER_TOP                  (56)
#define PICKER_BOTTOM               (412)
#define PICKER_FIRST_ROW_Y          (100)
#define PICKER_ROW_H                (26)
#define PICKER_ROWS                 ((PICKER_BOTTOM - PICKER_FIRST_ROW_Y - 8) / PICKER_ROW_H)


static const launcher_hint_t hints_grid[] = {
    { LAUNCHER_BUTTON_A, "A", "Open" },
    { LAUNCHER_BUTTON_OTHER, "L", "Home" },
    { LAUNCHER_BUTTON_C_UP_DOWN, "C", "Jump" },
    { LAUNCHER_BUTTON_C_LEFT_RIGHT, "C", "Size" },
    { LAUNCHER_BUTTON_OTHER, "R", "View" },
    { LAUNCHER_BUTTON_OTHER, "Z", "Game" },
    { LAUNCHER_BUTTON_START, "S", "Menu" },
};

static const launcher_hint_t hints_carousel[] = {
    { LAUNCHER_BUTTON_A, "A", "Open" },
    { LAUNCHER_BUTTON_OTHER, "L", "Home" },
    { LAUNCHER_BUTTON_C_LEFT_RIGHT, "C", "Jump" },
    { LAUNCHER_BUTTON_OTHER, "R", "View" },
    { LAUNCHER_BUTTON_OTHER, "Z", "Game" },
    { LAUNCHER_BUTTON_START, "S", "Menu" },
};

static const launcher_hint_t hints_empty[] = {
    { LAUNCHER_BUTTON_OTHER, "L", "Home" },
    { LAUNCHER_BUTTON_START, "S", "Menu" },
};

#define HINT_COUNT(h) ((int)(sizeof(h) / sizeof((h)[0])))

static int category_index = 0;
static bool category_restored = false;
static int selections[MAX_REMEMBERED_CATEGORIES];
static int top_row = 0;
static float grid_scroll = 0.0f;    /* Drawn scroll position in pixels, gliding toward top_row */
static bool motion_snap = true;     /* Next grid frame appears in place (screen opened, layout or list changed) */
static float carousel_position = 0.0f;
static char group_badge[48] = "";
static uint64_t group_badge_until_ms = 0;
static int current_sort = LAUNCHER_SORT_TITLE;

/* Filter (for this session): a field and the group label it keeps, or no filter. */
static int filter_field = -1;
static char filter_value[64] = "";

/* Filter picker: first a field, then one of its groups. */
static const char *picker_field_names[] = { "No filter", "Genre", "Developer", "Year", "Players" };
static const int picker_field_ids[] = { -1, LAUNCHER_SORT_GENRE, LAUNCHER_SORT_DEVELOPER, LAUNCHER_SORT_YEAR, LAUNCHER_SORT_PLAYERS };
#define PICKER_FIELD_COUNT (5)
static bool picker_open = false;
static int picker_stage = 0;
static int picker_sel = 0;
static int picker_top = 0;
static int picker_field = -1;
static library_group_t *picker_groups = NULL;
static int picker_group_count = 0;

/* Hidden games list (Start > Hidden games): A shows a game again. */
static bool hidden_open = false;
static int hidden_sel = 0;
static int hidden_top = 0;

static const launcher_hint_t hints_hidden[] = {
    { LAUNCHER_BUTTON_A, "A", "Show again" },
    { LAUNCHER_BUTTON_B, "B", "Back" },
};

/* Folders list (Start > Library > Folders): A shows or hides a folder, C up/down moves it. */
static bool folders_open = false;
static bool folders_dirty = false;
static int folders_sel = 0;
static int folders_top = 0;
static char folders_category[64];   /* Category shown when the list opened, to come back to */
static uint64_t folders_refused_until_ms = 0;

static const launcher_hint_t hints_folders[] = {
    { LAUNCHER_BUTTON_A, "A", "Show / Hide" },
    { LAUNCHER_BUTTON_C_UP_DOWN, "C", "Move" },
    { LAUNCHER_BUTTON_B, "B", "Done" },
};

/* Search (Start > Search): letters typed on an on-screen keyboard narrow the folder as you go. */
#define SEARCH_MAX                  (24)
#define SEARCH_TOP                  (240)
#define SEARCH_FIRST_KEY_Y          (282)
#define SEARCH_KEY_W                (40)
#define SEARCH_KEY_H                (26)
#define SEARCH_KEY_GAP              (6)
#define SEARCH_ROW_H                (32)
#define SEARCH_COLS                 (10)
#define SEARCH_ROWS                 (4)
#define SEARCH_SPACE_COL            (6)     /* Last row: Space and Delete take two columns each */
#define SEARCH_DELETE_COL           (8)
static const char *search_rows[SEARCH_ROWS] = { "1234567890", "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ" };
static bool search_open = false;
static int search_row = 1;
static int search_col = 0;
static char search_text[SEARCH_MAX + 1] = "";

static const launcher_hint_t hints_search[] = {
    { LAUNCHER_BUTTON_A, "A", "Type" },
    { LAUNCHER_BUTTON_B, "B", "Delete" },
    { LAUNCHER_BUTTON_START, "S", "Done" },
};

static const launcher_hint_t hints_picker[] = {
    { LAUNCHER_BUTTON_A, "A", "Choose" },
    { LAUNCHER_BUTTON_B, "B", "Back" },
    { LAUNCHER_BUTTON_C_UP_DOWN, "C", "Page" },
};

typedef struct {
    int cols;
    int gap;
    int w;
    int h;
    int x0;
    int rows;
    int row_h;
} grid_geometry_t;


static grid_geometry_t grid_geometry (int tile_size, bool portrait) {
    static const int gaps[] = { 10, 14, 18 };
    static const int landscape_widths[] = { 87, 133, 158 };
    static const int portrait_heights[] = { 84, 100, 115 };
    grid_geometry_t g;

    g.gap = gaps[tile_size];
    if (portrait) {
        g.h = portrait_heights[tile_size];
        g.w = (g.h * LAUNCHER_COVER_H) / LAUNCHER_COVER_W;
        g.cols = (LAUNCHER_WIDTH + g.gap) / (g.w + g.gap);
    } else {
        // Tile sizes are fixed (they match the cover packs); a wider screen fits more columns.
        g.w = landscape_widths[tile_size];
        g.h = (g.w * LAUNCHER_COVER_H) / LAUNCHER_COVER_W;
        g.cols = (LAUNCHER_WIDTH + g.gap) / (g.w + g.gap);
    }
    g.row_h = g.h + launcher_title_space() + g.gap;
    g.rows = (LAUNCHER_CONTENT_Y1 - LAUNCHER_CONTENT_Y + g.gap) / g.row_h;
    if (g.rows < 1) {
        g.rows = 1;
    }
    g.x0 = LAUNCHER_X0 + (LAUNCHER_WIDTH - (g.cols * g.w + (g.cols - 1) * g.gap)) / 2;
    return g;
}

static int *current_selection (void) {
    return &selections[category_index % MAX_REMEMBERED_CATEGORIES];
}

/** @brief Show another folder (Start > Folder), starting where it was last left. */
static void set_category (menu_t *menu, int index) {
    if (index < 0 || index >= library_category_count()) {
        return;
    }
    category_index = index;
    top_row = 0;
    carousel_position = (float)(*current_selection());
    sound_play_effect(SFX_CURSOR);

    // Remember it, so the library opens here next time (saved when leaving the library).
    const char *name = library_category_name(category_index);
    if (name) {
        free(menu->settings.launcher_category);
        menu->settings.launcher_category = strdup(name);
    }
}

/** @brief Whether two games fall in the same group for the current sort. */
static bool same_group (library_game_t *a, library_game_t *b) {
    char ga[48], gb[48];
    library_game_group(a, current_sort, ga, sizeof(ga));
    library_game_group(b, current_sort, gb, sizeof(gb));
    return strcmp(ga, gb) == 0;
}

/** @brief First game of the group containing sel. */
static int group_start (library_category_t *c, int sel) {
    while (sel > 0 && same_group(&c->games[sel - 1], &c->games[sel])) {
        sel--;
    }
    return sel;
}

/**
 * @brief Move to the first game of the next group, or back to the start of this group / the previous one.
 * Groups are letters when sorted by title, otherwise genres, developers, years or player counts.
 */
static int jump_group (library_category_t *c, int sel, int direction) {
    if (direction > 0) {
        for (int i = sel + 1; i < c->shown; i++) {
            if (!same_group(&c->games[i], &c->games[sel])) {
                return i;
            }
        }
        return sel;
    }
    int start = group_start(c, sel);
    if (start < sel || start == 0) {
        return start;
    }
    return group_start(c, start - 1);
}

static void show_group_badge (library_category_t *c, int sel) {
    library_game_group(&c->games[sel], current_sort, group_badge, sizeof(group_badge));
    group_badge_until_ms = get_ticks_ms() + GROUP_BADGE_MS;
}

/**
 * @brief Bring a category into the current sort order. A new order starts back at its first game.
 */
static void apply_sort (library_category_t *c, int index) {
    // A new sort or filter starts at the first game; hiding a game keeps the place.
    bool new_order = c->sort != current_sort || c->filter_field != filter_field ||
        (filter_field >= 0 && strcmp(c->filter_value, filter_value) != 0);
    if (library_arrange_category(c, current_sort, filter_field, filter_value)) {
        int *sel = &selections[index % MAX_REMEMBERED_CATEGORIES];
        if (new_order) {
            *sel = 0;
            if (index == category_index) {
                top_row = 0;
                carousel_position = 0.0f;
            }
        } else if (*sel >= c->shown) {
            *sel = c->shown > 0 ? c->shown - 1 : 0;
        }
    }
}

static void picker_close (void) {
    picker_open = false;
    free(picker_groups);
    picker_groups = NULL;
    picker_group_count = 0;
}

static void picker_show_fields (void) {
    picker_close();
    picker_open = true;
    picker_stage = 0;
    picker_sel = 0;
    for (int i = 0; i < PICKER_FIELD_COUNT; i++) {
        if (picker_field_ids[i] == filter_field) {
            picker_sel = i;
        }
    }
    picker_top = 0;
}

static void picker_process (menu_t *menu, library_category_t *c) {
    int count = (picker_stage == 0) ? PICKER_FIELD_COUNT : picker_group_count;
    int step = menu->actions.go_fast ? PICKER_ROWS : 1;

    if (menu->actions.go_up && picker_sel > 0) {
        picker_sel = (picker_sel - step < 0) ? 0 : picker_sel - step;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_down && picker_sel < count - 1) {
        picker_sel = (picker_sel + step > count - 1) ? count - 1 : picker_sel + step;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        if (picker_stage == 1) {
            picker_show_fields();
        } else {
            picker_close();
        }
        return;
    } else if (menu->actions.enter) {
        sound_play_effect(SFX_ENTER);
        if (picker_stage == 0) {
            if (picker_field_ids[picker_sel] < 0 || !c) {
                filter_field = -1;
                filter_value[0] = '\0';
                picker_close();
                return;
            }
            picker_field = picker_field_ids[picker_sel];
            free(picker_groups);
            picker_group_count = library_category_groups(c, picker_field, &picker_groups);
            picker_stage = 1;
            picker_sel = 0;
            picker_top = 0;
        } else if (picker_group_count > 0) {
            filter_field = picker_field;
            snprintf(filter_value, sizeof(filter_value), "%s", picker_groups[picker_sel].label);
            picker_close();
        }
        return;
    }

    if (picker_sel < picker_top) {
        picker_top = picker_sel;
    }
    if (picker_sel >= picker_top + PICKER_ROWS) {
        picker_top = picker_sel - PICKER_ROWS + 1;
    }
}

static void picker_draw (void) {
    color_t line = theme_current()->line;
    launcher_fill_rounded(LAUNCHER_X0 - 2, PICKER_TOP - 2, LAUNCHER_X1 + 2, PICKER_BOTTOM + 2, 14, line);
    launcher_fill_rounded(LAUNCHER_X0, PICKER_TOP, LAUNCHER_X1, PICKER_BOTTOM, 12, theme_current()->panel);

    char title[96];
    if (picker_stage == 0) {
        snprintf(title, sizeof(title), "Filter");
    } else {
        snprintf(title, sizeof(title), "Filter");
        for (int i = 0; i < PICKER_FIELD_COUNT; i++) {
            if (picker_field_ids[i] == picker_field) {
                snprintf(title, sizeof(title), "Filter by %s", picker_field_names[i]);
            }
        }
    }
    launcher_title(LAUNCHER_X0 + 20, PICKER_TOP + 10, LAUNCHER_WIDTH - 40, ALIGN_LEFT, STL_DEFAULT, title);

    int count = (picker_stage == 0) ? PICKER_FIELD_COUNT : picker_group_count;
    if (count == 0) {
        launcher_text(LAUNCHER_X0, PICKER_FIRST_ROW_Y + 40, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "No game details here");
        return;
    }
    for (int r = 0; r < PICKER_ROWS; r++) {
        int i = picker_top + r;
        if (i >= count) {
            break;
        }
        int y = PICKER_FIRST_ROW_Y + r * PICKER_ROW_H;
        if (i == picker_sel) {
            launcher_fill_rounded(LAUNCHER_X0 + 12, y - 3, LAUNCHER_X1 - 12, y + PICKER_ROW_H - 5, 6, theme_current()->highlight);
        }
        if (picker_stage == 0) {
            launcher_name(LAUNCHER_X0 + 24, y, LAUNCHER_WIDTH / 2, ALIGN_LEFT, STL_DEFAULT, picker_field_names[i]);
            if (picker_field_ids[i] >= 0 && picker_field_ids[i] == filter_field) {
                launcher_name(LAUNCHER_X0 + LAUNCHER_WIDTH / 2, y, LAUNCHER_WIDTH / 2 - 24, ALIGN_RIGHT, STL_ACCENT, filter_value);
            }
        } else {
            launcher_name(LAUNCHER_X0 + 24, y, LAUNCHER_WIDTH - 110, ALIGN_LEFT, STL_DEFAULT, picker_groups[i].label);
            launcher_text(LAUNCHER_X1 - 84, y, 60, ALIGN_RIGHT, STL_GRAY, "%d", picker_groups[i].count);
        }
    }
    if (picker_top + PICKER_ROWS < count) {
        launcher_small(LAUNCHER_X0, PICKER_BOTTOM - 22, LAUNCHER_WIDTH - 24, ALIGN_RIGHT, STL_GRAY, "more below");
    }
}

static void search_apply (void) {
    bool any = false;
    for (const char *p = search_text; *p; p++) {
        any |= (*p != ' ');
    }
    if (any) {
        filter_field = LAUNCHER_FILTER_SEARCH;
        snprintf(filter_value, sizeof(filter_value), "%s", search_text);
    } else if (filter_field == LAUNCHER_FILTER_SEARCH) {
        filter_field = -1;
        filter_value[0] = '\0';
    }
}

static void search_show (void) {
    search_open = true;
    if (filter_field != LAUNCHER_FILTER_SEARCH) {
        search_text[0] = '\0';
    }
}

/** @brief Keep the cursor on the first column of the last row's wide keys. */
static void search_snap (void) {
    if (search_row == SEARCH_ROWS - 1) {
        if (search_col == SEARCH_SPACE_COL + 1) {
            search_col = SEARCH_SPACE_COL;
        } else if (search_col == SEARCH_DELETE_COL + 1) {
            search_col = SEARCH_DELETE_COL;
        }
    }
}

static void search_delete (void) {
    size_t len = strlen(search_text);
    if (len > 0) {
        search_text[len - 1] = '\0';
        search_apply();
    }
}

static void search_process (menu_t *menu) {
    bool last_row = (search_row == SEARCH_ROWS - 1);
    int col = search_col;
    int row = search_row;
    if (menu->actions.go_left) {
        col -= (last_row && search_col >= SEARCH_DELETE_COL) ? 2 : 1;
    } else if (menu->actions.go_right) {
        col += (last_row && search_col >= SEARCH_SPACE_COL) ? 2 : 1;
    } else if (menu->actions.go_up) {
        row -= 1;
    } else if (menu->actions.go_down) {
        row += 1;
    }
    if ((col != search_col || row != search_row) && col >= 0 && col < SEARCH_COLS && row >= 0 && row < SEARCH_ROWS) {
        search_col = col;
        search_row = row;
        search_snap();
        sound_play_effect(SFX_CURSOR);
        return;
    }

    if (menu->actions.settings) {
        sound_play_effect(SFX_ENTER);
        search_open = false;
    } else if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        if (search_text[0]) {
            search_delete();
        } else {
            search_open = false;
        }
    } else if (menu->actions.enter) {
        size_t len = strlen(search_text);
        if (last_row && search_col >= SEARCH_DELETE_COL) {
            search_delete();
            sound_play_effect(SFX_EXIT);
        } else if (last_row && search_col >= SEARCH_SPACE_COL) {
            if (len > 0 && len < SEARCH_MAX && search_text[len - 1] != ' ') {
                search_text[len] = ' ';
                search_text[len + 1] = '\0';
            }
            sound_play_effect(SFX_TICK);
        } else if (len < SEARCH_MAX) {
            search_text[len] = search_rows[search_row][search_col];
            search_text[len + 1] = '\0';
            search_apply();
            sound_play_effect(SFX_TICK);
        }
    }
}

static void search_draw (library_category_t *c) {
    int top = SEARCH_TOP;
    int bottom = SEARCH_FIRST_KEY_Y + (SEARCH_ROWS - 1) * SEARCH_ROW_H + SEARCH_KEY_H + 12;
    launcher_fill_rounded(LAUNCHER_X0 - 2, top - 2, LAUNCHER_X1 + 2, bottom + 2, 14, theme_current()->line);
    launcher_fill_rounded(LAUNCHER_X0, top, LAUNCHER_X1, bottom, 12, theme_current()->panel);

    if (search_text[0]) {
        char typed[SEARCH_MAX + 2];
        snprintf(typed, sizeof(typed), "%s_", search_text);
        launcher_title(LAUNCHER_X0 + 20, top + 8, LAUNCHER_WIDTH - 200, ALIGN_LEFT, STL_DEFAULT, typed);
    } else {
        launcher_title(LAUNCHER_X0 + 20, top + 8, LAUNCHER_WIDTH - 200, ALIGN_LEFT, STL_GRAY, "Type part of a name");
    }
    int shown = c ? c->shown : 0;
    launcher_text(LAUNCHER_X1 - 180, top + 14, 160, ALIGN_RIGHT, STL_GRAY, shown == 1 ? "1 game" : "%d games", shown);

    int keys_w = SEARCH_COLS * SEARCH_KEY_W + (SEARCH_COLS - 1) * SEARCH_KEY_GAP;
    int x0 = LAUNCHER_X0 + (LAUNCHER_WIDTH - keys_w) / 2;
    for (int row = 0; row < SEARCH_ROWS; row++) {
        int y = SEARCH_FIRST_KEY_Y + row * SEARCH_ROW_H;
        for (int col = 0; col < SEARCH_COLS; col++) {
            char letter[2] = { 0, 0 };
            const char *label = letter;
            int span = 1;
            if (col < (int)strlen(search_rows[row])) {
                letter[0] = search_rows[row][col];
            } else if (col == SEARCH_SPACE_COL) {
                label = "Space";
                span = 2;
            } else if (col == SEARCH_DELETE_COL) {
                label = "Delete";
                span = 2;
            } else {
                continue;
            }
            int x = x0 + col * (SEARCH_KEY_W + SEARCH_KEY_GAP);
            int w = span * SEARCH_KEY_W + (span - 1) * SEARCH_KEY_GAP;
            bool on = (row == search_row && col == search_col);
            launcher_fill_rounded(x, y, x + w, y + SEARCH_KEY_H, 6, on ? theme_current()->accent : theme_current()->highlight);
            launcher_text(x, y + 4, w, ALIGN_CENTER, on ? STL_ON_ACCENT : STL_DEFAULT, "%s", label);
        }
    }
}

/** @brief Whether a game is a favorite, worked out again only when favorites change. */
static bool game_is_favorite (menu_t *menu, library_game_t *game) {
    uint32_t generation = launcher_favorites_generation();
    if (game->favorite_generation != generation) {
        path_t *path = library_game_path(game);
        game->favorite = launcher_is_favorite(menu, path);
        path_free(path);
        game->favorite_generation = generation;
    }
    return game->favorite;
}

static void hidden_process (menu_t *menu) {
    int count = library_hidden_count(menu);
    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        hidden_open = false;
        return;
    }
    if (count == 0) {
        return;
    }
    if (menu->actions.go_up && hidden_sel > 0) {
        hidden_sel--;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_down && hidden_sel < count - 1) {
        hidden_sel++;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.enter) {
        path_t *rom = path_create(library_hidden_at(hidden_sel));
        library_set_hidden(menu, rom, false);
        path_free(rom);
        sound_play_effect(SFX_ENTER);
        count--;
        if (hidden_sel >= count) {
            hidden_sel = count > 0 ? count - 1 : 0;
        }
    }
    if (hidden_sel < hidden_top) {
        hidden_top = hidden_sel;
    }
    if (hidden_sel >= hidden_top + PICKER_ROWS) {
        hidden_top = hidden_sel - PICKER_ROWS + 1;
    }
}

static void hidden_draw (menu_t *menu) {
    launcher_fill_rounded(LAUNCHER_X0 - 2, PICKER_TOP - 2, LAUNCHER_X1 + 2, PICKER_BOTTOM + 2, 14, theme_current()->line);
    launcher_fill_rounded(LAUNCHER_X0, PICKER_TOP, LAUNCHER_X1, PICKER_BOTTOM, 12, theme_current()->panel);
    launcher_title(LAUNCHER_X0 + 20, PICKER_TOP + 10, LAUNCHER_WIDTH - 40, ALIGN_LEFT, STL_DEFAULT, "Hidden games");
    int count = library_hidden_count(menu);
    if (count == 0) {
        launcher_text(LAUNCHER_X0, PICKER_FIRST_ROW_Y + 40, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "No hidden games");
        return;
    }
    for (int r = 0; r < PICKER_ROWS; r++) {
        int i = hidden_top + r;
        if (i >= count) {
            break;
        }
        int y = PICKER_FIRST_ROW_Y + r * PICKER_ROW_H;
        if (i == hidden_sel) {
            launcher_fill_rounded(LAUNCHER_X0 + 12, y - 3, LAUNCHER_X1 - 12, y + PICKER_ROW_H - 5, 6, theme_current()->highlight);
        }
        // "sd:/N64 USA/Game.n64" -> "Game" and its folder.
        const char *full = library_hidden_at(i);
        path_t *path = path_create(full);
        char *title = library_title_from_path(path);
        path_pop(path);
        launcher_name(LAUNCHER_X0 + 24, y, LAUNCHER_WIDTH - 200, ALIGN_LEFT, STL_DEFAULT, title);
        launcher_name(LAUNCHER_X1 - 180, y, 156, ALIGN_RIGHT, STL_GRAY, path_last_get(path));
        free(title);
        path_free(path);
    }
}

static void folders_show (void) {
    folders_open = true;
    folders_dirty = false;
    folders_sel = 0;
    folders_top = 0;
    const char *name = library_category_name(category_index);
    snprintf(folders_category, sizeof(folders_category), "%s", name ? name : "");
}

static void folders_close (menu_t *menu) {
    folders_open = false;
    if (!folders_dirty) {
        return;
    }
    library_folders_save();
    // Category numbers changed: stay on the same folder (and game) if it is still shown.
    int keep = *current_selection();
    memset(selections, 0, sizeof(selections));
    int found = library_category_find(folders_category);
    category_index = found >= 0 ? found : 0;
    if (found >= 0) {
        *current_selection() = keep;
    }
    top_row = 0;
    carousel_position = (float)(*current_selection());
    const char *name = library_category_name(category_index);
    if (name) {
        free(menu->settings.launcher_category);
        menu->settings.launcher_category = strdup(name);
    }
}

static void folders_process (menu_t *menu) {
    int count = library_folder_count();
    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        folders_close(menu);
        return;
    }
    if (count == 0) {
        return;
    }
    if (menu->actions.go_up && folders_sel > 0) {
        folders_sel = menu->actions.go_fast ? library_folder_move(folders_sel, -1) : folders_sel - 1;
        folders_dirty |= menu->actions.go_fast;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_down && folders_sel < count - 1) {
        folders_sel = menu->actions.go_fast ? library_folder_move(folders_sel, 1) : folders_sel + 1;
        folders_dirty |= menu->actions.go_fast;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.enter) {
        if (library_folder_set_hidden(folders_sel, !library_folder_hidden(folders_sel))) {
            folders_dirty = true;
            sound_play_effect(SFX_SETTING);
        } else {
            folders_refused_until_ms = get_ticks_ms() + 2000;
            sound_play_effect(SFX_ERROR);
        }
    }
    if (folders_sel < folders_top) {
        folders_top = folders_sel;
    }
    if (folders_sel >= folders_top + PICKER_ROWS) {
        folders_top = folders_sel - PICKER_ROWS + 1;
    }
}

static void folders_draw (menu_t *menu) {
    launcher_fill_rounded(LAUNCHER_X0 - 2, PICKER_TOP - 2, LAUNCHER_X1 + 2, PICKER_BOTTOM + 2, 14, theme_current()->line);
    launcher_fill_rounded(LAUNCHER_X0, PICKER_TOP, LAUNCHER_X1, PICKER_BOTTOM, 12, theme_current()->panel);
    launcher_title(LAUNCHER_X0 + 20, PICKER_TOP + 10, LAUNCHER_WIDTH - 40, ALIGN_LEFT, STL_DEFAULT, "Folders");
    int count = library_folder_count();
    for (int r = 0; r < PICKER_ROWS; r++) {
        int i = folders_top + r;
        if (i >= count) {
            break;
        }
        int y = PICKER_FIRST_ROW_Y + r * PICKER_ROW_H;
        if (i == folders_sel) {
            launcher_fill_rounded(LAUNCHER_X0 + 12, y - 3, LAUNCHER_X1 - 12, y + PICKER_ROW_H - 5, 6, theme_current()->highlight);
        }
        bool hidden = library_folder_hidden(i);
        launcher_name(LAUNCHER_X0 + 24, y, LAUNCHER_WIDTH - 200, ALIGN_LEFT, hidden ? STL_GRAY : STL_DEFAULT, library_folder_name(i));
        launcher_name(LAUNCHER_X1 - 180, y, 156, ALIGN_RIGHT, hidden ? STL_GRAY : STL_ACCENT, hidden ? "Hidden" : "Shown");
    }
    const char *note = NULL;
    if (get_ticks_ms() < folders_refused_until_ms) {
        note = "At least one folder stays shown";
    } else if (!menu->settings.launcher_categories_enabled) {
        note = "Folder categories are off: hidden folders are left out of All Games";
    }
    if (note) {
        launcher_small(LAUNCHER_X0 + 24, PICKER_BOTTOM - 22, LAUNCHER_WIDTH - 48, ALIGN_LEFT, STL_GRAY, note);
    }
}

static void launch (menu_t *menu, library_game_t *game) {
    path_free(menu->load.launcher_rom_path);
    menu->load.launcher_rom_path = library_game_path(game);
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    menu->load.quick_start = menu->settings.launcher_quick_launch;
    sound_play_effect(SFX_ENTER);
    launcher_leave(menu, MENU_MODE_LOAD_ROM);
}

/**
 * @brief Cover for a game, looking up at most one new cover path per frame
 * (each lookup reads the ROM header and checks a few files on the SD card).
 */
static surface_t *game_cover (menu_t *menu, library_game_t *game, int w, int h, int *lookups_left, bool *pending) {
    if (game->cover_state == LIBRARY_COVER_UNKNOWN) {
        if (*lookups_left <= 0) {
            *pending = true;
            return NULL;
        }
        (*lookups_left)--;
    }
    const char *path = library_game_cover(menu, game);
    if (!path) {
        *pending = false;
        return NULL;
    }
    return launcher_cover_get(path, w, h, pending);
}

static void process (menu_t *menu) {
    if (launcher_menu_process(menu)) {
        return;
    }

    // Layout changed from the Start menu: start the new one at the highlighted game.
    static int shown_layout = -1;
    if (menu->settings.launcher_layout != shown_layout) {
        shown_layout = menu->settings.launcher_layout;
        top_row = 0;
        carousel_position = (float)(*current_selection());
    }

    library_category_t *current = library_category(category_index);
    if (current) {
        apply_sort(current, category_index);
    }

    if (picker_open) {
        picker_process(menu, current);
        return;
    }
    if (search_open) {
        search_process(menu);
        return;
    }
    if (hidden_open) {
        hidden_process(menu);
        return;
    }
    if (folders_open) {
        folders_process(menu);
        return;
    }

    launcher_request_t request = launcher_take_request();
    if (request == LAUNCHER_REQUEST_HIDDEN) {
        hidden_open = true;
        hidden_sel = 0;
        hidden_top = 0;
        return;
    }
    if (request == LAUNCHER_REQUEST_FOLDERS) {
        folders_show();
        return;
    }
    if (request == LAUNCHER_REQUEST_CATEGORY) {
        set_category(menu, launcher_requested_category());
        // Say where you landed, in the badge used for letter jumps.
        const char *name = library_category_name(category_index);
        snprintf(group_badge, sizeof(group_badge), "%s", name ? name : "");
        group_badge_until_ms = get_ticks_ms() + GROUP_BADGE_MS;
        return;
    }
    if (request == LAUNCHER_REQUEST_FILTER) {
        picker_show_fields();
        return;
    }
    if (request == LAUNCHER_REQUEST_SEARCH) {
        search_show();
        return;
    }
    if (request == LAUNCHER_REQUEST_SURPRISE && current && current->shown > 0) {
        static bool seeded = false;
        if (!seeded) {
            srand(TICKS_READ());
            seeded = true;
        }
        *current_selection() = rand() % current->shown;
        snprintf(group_badge, sizeof(group_badge), "Surprise!");
        group_badge_until_ms = get_ticks_ms() + GROUP_BADGE_MS;
        sound_play_effect(SFX_ENTER);
        return;
    }

    if (menu->actions.settings || menu->actions.trigger_z) {
        int sel = *current_selection();
        path_t *rom = (current && sel < current->shown) ? library_game_path(&current->games[sel]) : NULL;
        if (menu->actions.settings) {
            launcher_menu_show(menu, rom);
        } else {
            launcher_game_menu_show(menu, rom);
        }
        return;
    }
    // R steps through the library's views: Grid, Carousel, Shelf.
    if (menu->actions.shoulder_r) {
        static const char *view_names[] = { "Grid", "Carousel", "Shelf" };
        menu->settings.launcher_layout = (menu->settings.launcher_layout + 1) % 3;
        snprintf(group_badge, sizeof(group_badge), "%s", view_names[menu->settings.launcher_layout]);
        group_badge_until_ms = get_ticks_ms() + GROUP_BADGE_MS;
        launcher_transition_fade();
        sound_play_effect(SFX_SWISH);
        return;     // Saved with the other view settings when the library is left.
    }

    // With a search showing, B clears it first.
    if (menu->actions.back && filter_field == LAUNCHER_FILTER_SEARCH) {
        sound_play_effect(SFX_EXIT);
        search_text[0] = '\0';
        search_apply();
        return;
    }

    // L (the Home tab is to the left) or B goes back to Home; R on Home comes here.
    if (menu->actions.back || menu->actions.shoulder_l) {
        sound_play_effect(menu->actions.back ? SFX_EXIT : SFX_SWISH);
        settings_save(&menu->settings);
        launcher_transition(menu->mode, MENU_MODE_HOME);
        menu->next_mode = MENU_MODE_HOME;
        return;
    }

    library_category_t *c = library_category(category_index);
    if (!c || c->shown == 0) {
        return;
    }
    apply_sort(c, category_index);

    int *sel = current_selection();
    if (*sel >= c->shown) {
        *sel = c->shown - 1;
    }
    int previous = *sel;

    if (menu->settings.launcher_layout == LAUNCHER_LAYOUT_CAROUSEL || menu->settings.launcher_layout == LAUNCHER_LAYOUT_SHELF) {
        if (menu->actions.go_fast && (menu->actions.go_left || menu->actions.go_right)) {
            *sel = jump_group(c, *sel, menu->actions.go_right ? 1 : -1);
            show_group_badge(c, *sel);
        } else if (menu->actions.go_left && *sel > 0) {
            *sel -= 1;
        } else if (menu->actions.go_right && *sel < c->shown - 1) {
            *sel += 1;
        }
        if (*sel != previous) {
            sound_play_effect(SFX_TICK);
        }
        if (menu->actions.enter) {
            launch(menu, &c->games[*sel]);
        }
        return;
    }

    if (menu->actions.go_fast && (menu->actions.go_left || menu->actions.go_right)) {
        int size = menu->settings.launcher_tile_size + (menu->actions.go_right ? 1 : -1);
        if (size >= LAUNCHER_TILE_SMALL && size <= LAUNCHER_TILE_LARGE) {
            menu->settings.launcher_tile_size = size;
            sound_play_effect(SFX_SETTING);
        }
        return;
    }
    if (menu->actions.go_fast && (menu->actions.go_up || menu->actions.go_down)) {
        *sel = jump_group(c, *sel, menu->actions.go_down ? 1 : -1);
        show_group_badge(c, *sel);
        if (*sel != previous) {
            sound_play_effect(SFX_TICK);
        }
        return;
    }

    grid_geometry_t g = grid_geometry(menu->settings.launcher_tile_size, c->portrait);
    int step = g.cols;

    if (menu->actions.go_left && *sel > 0) {
        *sel -= 1;
    } else if (menu->actions.go_right && *sel < c->shown - 1) {
        *sel += 1;
    } else if (menu->actions.go_up) {
        if (*sel - step >= 0) {
            *sel -= step;
        }
    } else if (menu->actions.go_down) {
        if (*sel + step < c->shown) {
            *sel += step;
        } else if ((*sel / g.cols) < ((c->shown - 1) / g.cols)) {
            // A shorter last row: land on its last game.
            *sel = c->shown - 1;
        }
    }

    if (*sel != previous) {
        sound_play_effect(SFX_TICK);
    }

    if (menu->actions.enter) {
        launch(menu, &c->games[*sel]);
    }
}

/** @brief The folder being shown, small at the left of the line under the tabs (Start > Folder switches). */
static void draw_category_name (void) {
    const char *name = library_category_name(category_index);
    if (name) {
        launcher_name(LAUNCHER_X0, LAUNCHER_SUBHEADER_Y, LAUNCHER_WIDTH / 2, ALIGN_LEFT, STL_GRAY, name);
    }
}

/**
 * @brief The first game shown when returning to a category in the grid (top_row resets on switch).
 */
static int grid_first_visible (grid_geometry_t *g, int sel) {
    int row = sel / g->cols;
    int top = (row >= g->rows) ? row - g->rows + 1 : 0;
    return top * g->cols;
}

/**
 * @brief Load the covers a neighbouring category will show first, so switching to it is instant.
 */
static void preload_category_covers (menu_t *menu, int index, int *lookups_left);

static void preload_category (menu_t *menu, int index, int *lookups_left) {
    if (index == category_index || !library_category_is_quick(index)) {
        return;
    }
    preload_category_covers(menu, index, lookups_left);
}

static void preload_category_covers (menu_t *menu, int index, int *lookups_left) {
    if (menu->settings.launcher_layout == LAUNCHER_LAYOUT_SHELF) {
        return;     // The shelf reads its spines as they come into view.
    }
    library_category_t *c = library_category(index);
    if (!c || c->shown == 0) {
        return;
    }
    apply_sort(c, index);
    int sel = selections[index % MAX_REMEMBERED_CATEGORIES];
    if (sel >= c->shown) {
        sel = c->shown - 1;
    }
    bool pending;
    if (menu->settings.launcher_layout == LAUNCHER_LAYOUT_CAROUSEL) {
        int cw = c->portrait ? LAUNCHER_COVER_H : LAUNCHER_COVER_W;
        int ch = c->portrait ? LAUNCHER_COVER_W : LAUNCHER_COVER_H;
        for (int i = sel - 2; i <= sel + 2; i++) {
            if (i >= 0 && i < c->shown) {
                game_cover(menu, &c->games[i], cw, ch, lookups_left, &pending);
            }
        }
        return;
    }
    grid_geometry_t g = grid_geometry(menu->settings.launcher_tile_size, c->portrait);
    int first = grid_first_visible(&g, sel);
    for (int i = first; i < first + g.cols * g.rows && i < c->shown; i++) {
        game_cover(menu, &c->games[i], g.w, g.h, lookups_left, &pending);
    }
}

/**
 * @brief Load the covers shown after jumping to the next or previous group, so jumps land on ready covers.
 */
static void preload_group_jumps (menu_t *menu, library_category_t *c, int sel, int *lookups_left) {
    if (menu->settings.launcher_layout == LAUNCHER_LAYOUT_SHELF) {
        return;
    }
    bool pending;
    // Finding group edges scans the list, so only redo it when the selection, list or sort changes.
    static library_category_t *cached_category = NULL;
    static int cached_sel = -1;
    static int cached_sort = -1;
    static int targets[2];
    if (c != cached_category || sel != cached_sel || current_sort != cached_sort) {
        targets[0] = jump_group(c, sel, 1);
        targets[1] = jump_group(c, sel, -1);
        cached_category = c;
        cached_sel = sel;
        cached_sort = current_sort;
    }
    for (int t = 0; t < 2; t++) {
        int target = targets[t];
        if (target == sel) {
            continue;
        }
        if (menu->settings.launcher_layout == LAUNCHER_LAYOUT_CAROUSEL) {
            int cw = c->portrait ? LAUNCHER_COVER_H : LAUNCHER_COVER_W;
            int ch = c->portrait ? LAUNCHER_COVER_W : LAUNCHER_COVER_H;
            for (int i = target - 2; i <= target + 2; i++) {
                if (i >= 0 && i < c->shown) {
                    game_cover(menu, &c->games[i], cw, ch, lookups_left, &pending);
                }
            }
        } else {
            grid_geometry_t g = grid_geometry(menu->settings.launcher_tile_size, c->portrait);
            int row = target / g.cols;
            int top = (row < top_row) ? row : ((row >= top_row + g.rows) ? row - g.rows + 1 : top_row);
            for (int i = top * g.cols; i < (top + g.rows) * g.cols && i < c->shown; i++) {
                game_cover(menu, &c->games[i], g.w, g.h, lookups_left, &pending);
            }
        }
    }
}

static void preload_neighbours (menu_t *menu, int *lookups_left) {
    int count = library_category_count();
    if (count < 2) {
        return;
    }
    preload_category(menu, (category_index + 1) % count, lookups_left);
    preload_category(menu, (category_index + count - 1) % count, lookups_left);
}

#define LOOKAHEAD_COVERS    (72)    /* Covers kept ready around the page (the cache holds 96 with an Expansion Pak) */

/**
 * @brief While the page is complete, load rows above and below it, nearest first, so scrolling
 * (even fast) lands on covers already in memory. Returns whether any are still loading.
 */
static bool preload_rows_around (menu_t *menu, library_category_t *c, grid_geometry_t *g, int first, int last, int *lookups_left) {
    if (!is_memory_expanded()) {
        return false;   // 24 cover slots without an Expansion Pak: keep them for the page.
    }
    int rows = (LOOKAHEAD_COVERS / g->cols - g->rows) / 2;
    bool any_pending = false;
    bool pending;
    for (int r = 1; r <= rows; r++) {
        int below = last + (r - 1) * g->cols + g->cols;   // The row right below the page is preloaded already.
        for (int i = below; i < below + g->cols && i < c->shown; i++) {
            game_cover(menu, &c->games[i], g->w, g->h, lookups_left, &pending);
            any_pending |= pending;
        }
        int above = first - r * g->cols;
        for (int i = above; i < above + g->cols; i++) {
            if (i >= 0 && i < c->shown) {
                game_cover(menu, &c->games[i], g->w, g->h, lookups_left, &pending);
                any_pending |= pending;
            }
        }
    }
    return any_pending;
}

static void draw_grid (menu_t *menu, library_category_t *c, int sel) {
    grid_geometry_t g = grid_geometry(menu->settings.launcher_tile_size, c->portrait);
    bool highlight_only = menu->settings.launcher_titles_highlight_only;
    int lookups_left = 1;

    int row = sel / g.cols;
    if (row < top_row) {
        top_row = row;
    }
    if (row >= top_row + g.rows) {
        top_row = row - g.rows + 1;
    }

    // Ask for the highlighted cover first so it loads before its neighbours; while scrolling fast
    // it is the only one read, so frames stay on time.
    bool pending;
    game_cover(menu, &c->games[sel], g.w, g.h, &lookups_left, &pending);
    launcher_covers_hold(launcher_scrolling_fast(category_index * 100000 + sel));

    int first = top_row * g.cols;
    int last = first + g.rows * g.cols;
    if (last > c->shown) {
        last = c->shown;
    }

    // Glide to the scroll position; a different list, size or a jump of more than a screen appears in place.
    static library_category_t *motion_category = NULL;
    static int motion_tile_size = -1;
    static int motion_cols = -1;
    static int motion_shown = -1;
    if (c != motion_category || menu->settings.launcher_tile_size != motion_tile_size ||
        g.cols != motion_cols || c->shown != motion_shown) {
        motion_category = c;
        motion_tile_size = menu->settings.launcher_tile_size;
        motion_cols = g.cols;
        motion_shown = c->shown;
        motion_snap = true;
    }
    float target_scroll = (float)(top_row * g.row_h);
    if (motion_snap || fabsf(target_scroll - grid_scroll) > g.rows * g.row_h) {
        grid_scroll = target_scroll;
        launcher_focus_reset();
        motion_snap = false;
    } else {
        grid_scroll = launcher_ease(grid_scroll, target_scroll);
    }
    int scroll = (int)lroundf(grid_scroll);
    int draw_first = (scroll / g.row_h) * g.cols;
    int draw_last = ((scroll + g.rows * g.row_h + g.row_h - 1) / g.row_h) * g.cols;
    if (draw_last > c->shown) {
        draw_last = c->shown;
    }

    rdpq_set_scissor(0, LAUNCHER_CONTENT_Y - LAUNCHER_RING - 2, display_get_width(), LAUNCHER_CONTENT_Y1 + 2);
    int lift = launcher_focus_lift(category_index * 100000 + sel);

    // The selected game's title shows in full; titles in its row that it would run into wait.
    int sel_tx, sel_tw;
    int base_extra = highlight_only ? TITLE_EXTRA_HIGHLIGHT_ONLY : (g.gap / 2) - 1;
    launcher_title_box(c->games[sel].title, g.x0 + (sel % g.cols) * (g.w + g.gap) + g.w / 2, g.w + base_extra * 2, &sel_tx, &sel_tw);
    for (int i = draw_first; i < draw_last; i++) {
        int col = i % g.cols;
        int x = g.x0 + col * (g.w + g.gap);
        int y = LAUNCHER_CONTENT_Y + (i / g.cols) * g.row_h - scroll;
        bool selected = (i == sel);
        int tile_y = selected ? y - lift : y;

        surface_t *image = game_cover(menu, &c->games[i], g.w, g.h, &lookups_left, &pending);
        launcher_draw_tile(x, tile_y, g.w, g.h, image, pending, c->games[i].title, false, theme_current()->bg);
        if (selected) {
            launcher_note_focused_cover(image, x, tile_y, g.w, g.h);
        }
        if (game_is_favorite(menu, &c->games[i])) {
            launcher_draw_favorite_star(x, tile_y, g.w);
        }

        if (selected || !highlight_only) {
            int tx = x - base_extra;
            int tw = g.w + base_extra * 2;
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
            } else if (i / g.cols == sel / g.cols) {
                blocked = (tx < sel_tx + sel_tw + 6) && (tx + tw > sel_tx - 6);
            }
            if (!blocked) {
                launcher_game_name(tx, y + g.h + LAUNCHER_RING + 3, tw, ALIGN_CENTER, selected ? STL_DEFAULT : STL_GRAY, c->games[i].title);
            }
        }
    }

    // The ring glides to where the selected tile is headed.
    int sel_y = LAUNCHER_CONTENT_Y + (sel / g.cols - top_row) * g.row_h - lift;
    launcher_draw_focus_ring_tile(g.x0 + (sel % g.cols) * (g.w + g.gap), sel_y, g.w, g.h);
    rdpq_set_scissor(0, 0, display_get_width(), display_get_height());

    // Preload the row below so scrolling down shows covers straight away. While the screen is
    // still sliding in, read-ahead waits: a cover read per frame would make the slide stutter.
    bool sliding = launcher_transition_running();
    bool any_pending = sliding;
    for (int i = first; !sliding && i < last + g.cols && i < c->shown; i++) {
        game_cover(menu, &c->games[i], g.w, g.h, &lookups_left, &pending);
        any_pending |= pending;
    }

    // Once this screen is complete, get group jumps and then the categories either side ready,
    // and look for ROMs added since the cover index was built.
    if (!any_pending) {
        any_pending = preload_rows_around(menu, c, &g, first, last, &lookups_left);
    }
    if (!any_pending) {
        library_check_new_games(NEW_GAMES_CHECK_US);
        preload_group_jumps(menu, c, sel, &lookups_left);
        preload_neighbours(menu, &lookups_left);
    }
}

/**
 * @brief Horizontal center and scale of a carousel cover at offset t from the middle
 * (t may be fractional while sliding).
 */
static void carousel_slot (float t, int cw, int sw, float *center_x, float *scale) {
    float near = (cw / 2.0f) + CAROUSEL_SPACING + (sw / 2.0f);
    float a = fabsf(t);
    if (a <= 1.0f) {
        *scale = 1.0f - (1.0f - CAROUSEL_SIDE_SCALE) * a;
        *center_x = LAUNCHER_CENTER_X + t * near;
    } else {
        *scale = CAROUSEL_SIDE_SCALE;
        *center_x = LAUNCHER_CENTER_X + copysignf(near + (a - 1.0f) * (sw + CAROUSEL_SPACING), t);
    }
}

static void draw_carousel (menu_t *menu, library_category_t *c, int sel) {
    int cw = c->portrait ? LAUNCHER_SHOWCASE_H : LAUNCHER_SHOWCASE_W;   // As drawn...
    int ch = c->portrait ? LAUNCHER_SHOWCASE_W : LAUNCHER_SHOWCASE_H;
    int rw = c->portrait ? LAUNCHER_COVER_H : LAUNCHER_COVER_W;         // ...from full-size covers
    int rh = c->portrait ? LAUNCHER_COVER_W : LAUNCHER_COVER_H;
    int sw = (int)(cw * CAROUSEL_SIDE_SCALE);
    int lookups_left = 1;
    bool pending;

    motion_snap = true;     // Back in the grid, it appears in place.

    // Ease toward the selection; snap when close.
    carousel_position += (sel - carousel_position) * CAROUSEL_EASE;
    if (fabsf(sel - carousel_position) < 0.02f) {
        carousel_position = (float)sel;
    }

    // Highlighted cover first so it loads before its neighbours (the only one read while scrolling fast).
    game_cover(menu, &c->games[sel], rw, rh, &lookups_left, &pending);
    launcher_covers_hold(launcher_scrolling_fast(category_index * 100000 + sel));

    // Far covers first so nearer ones draw on top.
    for (int reach = CAROUSEL_REACH + 1; reach >= 0; reach--) {
        for (int side = -1; side <= 1; side += 2) {
            if (reach == 0 && side == 1) {
                continue;
            }
            int i = (int)roundf(carousel_position) + side * reach;
            if (i < 0 || i >= c->shown) {
                continue;
            }
            float center_x, scale;
            carousel_slot(i - carousel_position, cw, sw, &center_x, &scale);
            int w = (int)(cw * scale);
            int h = (int)(ch * scale);
            int x = (int)center_x - w / 2;
            int y = CAROUSEL_CENTER_Y - h / 2;
            if (x + w < 0 || x > LAUNCHER_SCREEN_W) {
                continue;
            }

            surface_t *image = game_cover(menu, &c->games[i], rw, rh, &lookups_left, &pending);
            bool settled = (i == sel) && (carousel_position == (float)sel);
            if (settled) {
                launcher_draw_tile_zoomed(x, y, w, h, image, pending, c->games[i].title, true, theme_current()->bg, LAUNCHER_SHOWCASE_ZOOM);
                if (game_is_favorite(menu, &c->games[i])) {
                    launcher_draw_favorite_star(x, y, w);
                }
            } else if (image) {
                launcher_draw_cover_scaled(image, x, y, w, h, theme_current()->bg);
            } else {
                launcher_draw_tile(x, y, w, h, NULL, true, NULL, false, theme_current()->bg);
            }
        }
    }

    char position[128];
    char group[48];
    library_game_group(&c->games[sel], current_sort, group, sizeof(group));
    if (current_sort == LAUNCHER_SORT_TITLE) {
        snprintf(position, sizeof(position), "%s  -  %d of %d", c->name, sel + 1, c->shown);
    } else {
        snprintf(position, sizeof(position), "%s  -  %s  -  %d of %d", c->name, group, sel + 1, c->shown);
    }
    if (!pending && carousel_position == (float)sel) {
        library_check_new_games(NEW_GAMES_CHECK_US);
        preload_group_jumps(menu, c, sel, &lookups_left);
        preload_neighbours(menu, &lookups_left);
    }

    launcher_title(LAUNCHER_X0, CAROUSEL_TITLE_Y, LAUNCHER_WIDTH, ALIGN_CENTER, STL_DEFAULT, c->games[sel].title);
    launcher_name(LAUNCHER_X0, CAROUSEL_TITLE_Y + 32, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, position);

    char details[160];
    library_game_details(&c->games[sel], details, sizeof(details));
    if (details[0]) {
        launcher_game_details(LAUNCHER_X0, CAROUSEL_TITLE_Y + 58, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, details);
    }
}


#define SHELF_BASE_Y        (314)   /* Top of the shelf plank: spines and the open box stand on it */
#define SHELF_SPINE_W       ((int)(LAUNCHER_SPINE_W * LAUNCHER_SHOWCASE_ZOOM))
#define SHELF_SPINE_H       ((int)(LAUNCHER_SPINE_H * LAUNCHER_SHOWCASE_ZOOM))
#define SHELF_DIVIDER_W     (4)     /* Card standing before each group, its label on a tab at the top */
#define SHELF_DIVIDER_STEP  (SHELF_DIVIDER_W + SHELF_GAP)
#define SHELF_LABEL_INSET   (6)     /* Tab below the top of the spines */
#define SHELF_MAX_DIVIDERS  (48)
#define SHELF_GAP           (3)
#define SHELF_OPEN_GAP      (10)    /* Space either side of the open box */
#define SHELF_TITLE_Y       (SHELF_BASE_Y + 26)
#define SHELF_DETAILS_Y     (SHELF_BASE_Y + 60)

static float shelf_position = 0.0f;     /* Drawn selection, gliding toward it (fractional while moving) */

/** @brief Whether a group begins at game i (a divider stands before it). */
static bool shelf_group_starts (library_category_t *c, int i) {
    return i == 0 || !same_group(&c->games[i - 1], &c->games[i]);
}

/** @brief Dividers standing between games a and b (a < b: those before a + 1 ... b). */
static int shelf_dividers_between (library_category_t *c, int a, int b) {
    int n = 0;
    for (int k = a + 1; k <= b; k++) {
        n += shelf_group_starts(c, k);
    }
    return n;
}

typedef struct {
    int x;
    int game;       /* First game of the group */
    bool current;   /* The open box's group */
} shelf_divider_t;

static void draw_shelf (menu_t *menu, library_category_t *c, int sel) {
    int cw = c->portrait ? LAUNCHER_SHOWCASE_H : LAUNCHER_SHOWCASE_W;   // As drawn...
    int ch = c->portrait ? LAUNCHER_SHOWCASE_W : LAUNCHER_SHOWCASE_H;
    int rw = c->portrait ? LAUNCHER_COVER_H : LAUNCHER_COVER_W;         // ...from full-size covers
    int rh = c->portrait ? LAUNCHER_COVER_W : LAUNCHER_COVER_H;
    int lookups_left = 1;
    bool pending;

    // A different folder, or a jump of more than a shelf's width, appears in place.
    static library_category_t *shelf_category = NULL;
    if (c != shelf_category || fabsf(sel - shelf_position) > 30.0f) {
        shelf_category = c;
        shelf_position = (float)sel;
        launcher_focus_reset();
    } else {
        shelf_position = launcher_ease(shelf_position, (float)sel);
    }
    motion_snap = true;     // Back in the grid, it appears in place.

    // The open box sits in the middle; the shelf glides to it (dividers passed on the way count too).
    int lift = launcher_focus_lift(category_index * 100000 + sel);
    int step = SHELF_SPINE_W + SHELF_GAP;
    int from = (int)floorf(shelf_position);
    float frac = shelf_position - from;
    float dividers_moved = (sel >= from) ? shelf_dividers_between(c, from, sel) : -shelf_dividers_between(c, sel, from);
    if (frac > 0.0f && from + 1 < c->shown) {
        dividers_moved -= frac * shelf_group_starts(c, from + 1);
    }
    int open_x = LAUNCHER_CENTER_X - cw / 2 + (int)lroundf((sel - shelf_position) * step + dividers_moved * SHELF_DIVIDER_STEP);

    // The open box's cover first; while scrolling fast it is the only cover read.
    surface_t *front = game_cover(menu, &c->games[sel], rw, rh, &lookups_left, &pending);
    bool front_pending = pending;
    launcher_covers_hold(launcher_scrolling_fast(category_index * 100000 + sel));

    // The shelf runs edge to edge. (Launcher scissors always start at x = 0: in COPY mode, one that
    // doesn't hangs the RDP, a hardware bug.)
    rdpq_set_scissor(0, LAUNCHER_CONTENT_Y - LAUNCHER_RING - 2, display_get_width(), SHELF_BASE_Y);

    int spine_y = SHELF_BASE_Y - SHELF_SPINE_H;
    shelf_divider_t left[SHELF_MAX_DIVIDERS], right[SHELF_MAX_DIVIDERS];
    int left_count = 0, right_count = 0;
    color_t divider_color = theme_current()->line;

    // Spines to the left, then to the right, until they leave the screen; a divider before each group.
    int x = open_x - SHELF_OPEN_GAP;
    int leftmost = sel;
    for (int i = sel; i >= 0 && x > 0; i--) {
        if (i < sel) {
            leftmost = i;
            x -= SHELF_SPINE_W;
            const char *path = library_game_cover(menu, &c->games[i]);
            launcher_draw_spine(x, spine_y, SHELF_SPINE_W, SHELF_SPINE_H, launcher_spine_get(path, &pending));
            x -= SHELF_GAP;
        }
        if (shelf_group_starts(c, i)) {
            x -= SHELF_DIVIDER_W;
            launcher_fill_rounded(x, spine_y, x + SHELF_DIVIDER_W, SHELF_BASE_Y, 1, divider_color);
            if (left_count < SHELF_MAX_DIVIDERS) {
                left[left_count] = (shelf_divider_t){ x, i, left_count == 0 };
                left_count++;
            }
            x -= SHELF_GAP;
        }
    }
    if (!shelf_group_starts(c, leftmost) && left_count < SHELF_MAX_DIVIDERS) {
        // The group at the left edge began off screen: its label still shows, held at the edge.
        left[left_count] = (shelf_divider_t){ -launcher_screen_width(), leftmost, left_count == 0 };
        left_count++;
    }
    x = open_x + cw + SHELF_OPEN_GAP;
    for (int i = sel + 1; i < c->shown && x < launcher_screen_width(); i++) {
        if (shelf_group_starts(c, i)) {
            launcher_fill_rounded(x, spine_y, x + SHELF_DIVIDER_W, SHELF_BASE_Y, 1, divider_color);
            if (right_count < SHELF_MAX_DIVIDERS) {
                right[right_count] = (shelf_divider_t){ x, i, false };
                right_count++;
            }
            x += SHELF_DIVIDER_STEP;
        }
        const char *path = library_game_cover(menu, &c->games[i]);
        launcher_draw_spine(x, spine_y, SHELF_SPINE_W, SHELF_SPINE_H, launcher_spine_get(path, &pending));
        x += step;
    }

    // Labels on tabs at the top, left to right. A tab that would run into the open box hangs to the
    // left of its divider instead; one that would cover the previous tab (a short group) is left out.
    int last_right = -1000;
    for (int n = 0; n < left_count + right_count; n++) {
        shelf_divider_t *d = (n < left_count) ? &left[left_count - 1 - n] : &right[n - left_count];
        char label[48];
        library_game_group(&c->games[d->game], current_sort, label, sizeof(label));
        int w = launcher_pill_width(label);
        int lx = d->x;
        if (lx < SHELF_GAP) {
            // A group running off the left edge keeps its label in view, while there's room before the next.
            int next_x = (n + 1 < left_count + right_count) ? ((n + 1 < left_count) ? left[left_count - 2 - n].x : right[n + 1 - left_count].x) : launcher_screen_width();
            lx = SHELF_GAP;
            if (lx + w > next_x - SHELF_GAP || (lx + w > open_x - SHELF_GAP && lx < open_x + cw + SHELF_GAP)) {
                continue;
            }
        }
        if (lx + w > open_x - SHELF_GAP && lx < open_x + cw + SHELF_GAP) {
            lx = d->x + SHELF_DIVIDER_W - w;
        }
        if (lx < last_right + SHELF_GAP * 2 || (lx + w > open_x - SHELF_GAP && lx < open_x + cw + SHELF_GAP)) {
            continue;
        }
        launcher_draw_pill(lx, spine_y + SHELF_LABEL_INSET, label, d->current);
        last_right = lx + w;
    }

    // The open box, lifted a little, with the selection ring.
    int open_y = SHELF_BASE_Y - ch - lift;
    launcher_draw_tile_zoomed(open_x, open_y, cw, ch, front, front_pending, c->games[sel].title, false, theme_current()->bg, LAUNCHER_SHOWCASE_ZOOM);
    launcher_note_focused_cover_zoomed(front, open_x, open_y, cw, ch, LAUNCHER_SHOWCASE_ZOOM);
    if (game_is_favorite(menu, &c->games[sel])) {
        launcher_draw_favorite_star(open_x, open_y, cw);
    }
    launcher_draw_focus_ring_tile(LAUNCHER_CENTER_X - cw / 2, SHELF_BASE_Y - ch - lift, cw, ch);
    rdpq_set_scissor(0, 0, display_get_width(), display_get_height());

    // The shelf itself.
    launcher_fill_rounded(0, SHELF_BASE_Y, launcher_screen_width(), SHELF_BASE_Y + 8, 0, theme_current()->line);
    launcher_fill_rounded(0, SHELF_BASE_Y + 8, launcher_screen_width(), SHELF_BASE_Y + 12, 0, theme_current()->panel);

    launcher_title(LAUNCHER_X0, SHELF_TITLE_Y, LAUNCHER_WIDTH, ALIGN_CENTER, STL_DEFAULT, c->games[sel].title);
    char details[160];
    library_game_details(&c->games[sel], details, sizeof(details));
    if (details[0]) {
        launcher_game_details(LAUNCHER_X0, SHELF_DETAILS_Y, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, details);
    }

    if (!front_pending && shelf_position == (float)sel) {
        library_check_new_games(NEW_GAMES_CHECK_US);
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    launcher_covers_begin_frame();
    launcher_draw_header(menu, LAUNCHER_TAB_LIBRARY);

    library_category_t *c = library_category(category_index);
    if (!c) {
        char message[128];
        snprintf(message, sizeof(message), "No games found in %s", menu->settings.launcher_library_root);
        launcher_name(LAUNCHER_X0, 200, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, message);
        launcher_text(LAUNCHER_X0, 224, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY,
            "Use Start > File browser to find your ROMs");
        launcher_draw_footer(hints_empty, HINT_COUNT(hints_empty));
    } else {
        draw_category_name();
        apply_sort(c, category_index);
        int *sel = current_selection();
        if (c->shown == 0) {
            if (filter_field == LAUNCHER_FILTER_SEARCH) {
                char message[96];
                snprintf(message, sizeof(message), "No games here match \"%s\"", filter_value);
                launcher_name(LAUNCHER_X0, 140, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, message);
            } else if (filter_field >= 0) {
                launcher_name(LAUNCHER_X0, 200, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "No games here match the filter");
                launcher_text(LAUNCHER_X0, 224, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "Start > Filter to change it");
            } else {
                launcher_text(LAUNCHER_X0, 200, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "This folder has no games");
            }
        } else {
            if (*sel >= c->shown) {
                *sel = c->shown - 1;
            }
            if (menu->settings.launcher_layout == LAUNCHER_LAYOUT_CAROUSEL) {
                draw_carousel(menu, c, *sel);
            } else if (menu->settings.launcher_layout == LAUNCHER_LAYOUT_SHELF) {
                char group[48];
                char counter[128];
                library_game_group(&c->games[*sel], current_sort, group, sizeof(group));
                snprintf(counter, sizeof(counter), "%s   %d / %d", group, *sel + 1, c->shown);
                launcher_name(LAUNCHER_X0 + 300, LAUNCHER_SUBHEADER_Y, LAUNCHER_WIDTH - 300, ALIGN_RIGHT, STL_GRAY, counter);
                draw_shelf(menu, c, *sel);
            } else {
                char group[48];
                char counter[128];
                library_game_group(&c->games[*sel], current_sort, group, sizeof(group));
                if (filter_field == LAUNCHER_FILTER_SEARCH) {
                    snprintf(counter, sizeof(counter), "Search: %s   %d / %d", filter_value, *sel + 1, c->shown);
                } else if (filter_field >= 0) {
                    snprintf(counter, sizeof(counter), "Filter: %s   %d / %d", filter_value, *sel + 1, c->shown);
                } else {
                    snprintf(counter, sizeof(counter), "%s   %d / %d", group, *sel + 1, c->shown);
                }
                launcher_name(LAUNCHER_X0 + 300, LAUNCHER_SUBHEADER_Y, LAUNCHER_WIDTH - 300, ALIGN_RIGHT, STL_GRAY, counter);
                draw_grid(menu, c, *sel);

                char details[160];
                library_game_details(&c->games[*sel], details, sizeof(details));
                if (details[0]) {
                    launcher_game_details(LAUNCHER_X0, INFO_LINE_Y, LAUNCHER_WIDTH, ALIGN_LEFT, STL_GRAY, details);
                }
            }
        }
        if (search_open) {
            search_draw(c);
            launcher_draw_footer(hints_search, HINT_COUNT(hints_search));
        } else if (hidden_open) {
            hidden_draw(menu);
            launcher_draw_footer(hints_hidden, HINT_COUNT(hints_hidden));
        } else if (folders_open) {
            folders_draw(menu);
            launcher_draw_footer(hints_folders, HINT_COUNT(hints_folders));
        } else if (picker_open) {
            picker_draw();
            launcher_draw_footer(hints_picker, HINT_COUNT(hints_picker));
        } else if (menu->settings.launcher_layout == LAUNCHER_LAYOUT_CAROUSEL || menu->settings.launcher_layout == LAUNCHER_LAYOUT_SHELF) {
            launcher_draw_footer(hints_carousel, HINT_COUNT(hints_carousel));
        } else {
            launcher_draw_footer(hints_grid, HINT_COUNT(hints_grid));
        }

        if (group_badge[0] && get_ticks_ms() < group_badge_until_ms) {
            // A pill wide enough for the group name ("M", "Racing", "1998"...).
            int w = (int)strlen(group_badge) * 14 + GROUP_BADGE_PADDING;
            if (w < GROUP_BADGE_MIN_WIDTH) {
                w = GROUP_BADGE_MIN_WIDTH;
            }
            if (w > LAUNCHER_WIDTH - 40) {
                w = LAUNCHER_WIDTH - 40;
            }
            int x0 = LAUNCHER_CENTER_X - w / 2;
            int y0 = 222 - GROUP_BADGE_HEIGHT / 2;
            launcher_fill_rounded(x0, y0, x0 + w, y0 + GROUP_BADGE_HEIGHT, 16, theme_current()->accent);
            launcher_title(x0 + 8, y0 + (GROUP_BADGE_HEIGHT - 30) / 2, w - 16, ALIGN_CENTER, STL_ON_ACCENT, group_badge);
        }
    }

    launcher_menu_draw();

    rdpq_detach_show();
}


static void restore_category (menu_t *menu) {
    if (!category_restored) {
        int found = library_category_find(menu->settings.launcher_category);
        if (found >= 0) {
            category_index = found;
        }
        category_restored = true;
    }
}

void view_library_warm (menu_t *menu) {
    library_init(menu);
    restore_category(menu);
    library_category_t *c = library_category(category_index);   // Lists the folder (a scan of the SD card).
    if (c) {
        apply_sort(c, category_index);
    }
}

void view_library_preload (menu_t *menu, int *lookups_left) {
    if (category_restored && category_index < library_category_count() && library_category_is_quick(category_index)) {
        preload_category_covers(menu, category_index, lookups_left);
    }
}

void view_library_init (menu_t *menu) {
    motion_snap = true;
    launcher_focus_reset();
    library_init(menu);
    launcher_favorites_touched();
    launcher_menu_init();
    restore_category(menu);
    if (category_index >= library_category_count()) {
        category_index = 0;
        top_row = 0;
    }
    carousel_position = (float)(*current_selection());
    shelf_position = carousel_position;
}

void view_library_display (menu_t *menu, surface_t *display) {
    current_sort = menu->settings.launcher_sort;
    launcher_set_widescreen(menu->settings.launcher_widescreen);
    launcher_set_large_text(menu->settings.launcher_large_text);
    launcher_set_cartridge_art(menu->settings.launcher_cartridge_art);
    launcher_track_idle(menu);

    // "Rescan folders" in the Start menu can change the number of categories.
    if (category_index >= library_category_count()) {
        category_index = 0;
        top_row = 0;
    }

    process(menu);

    draw(menu, display);
}
