/**
 * @file library_view.c
 * @brief Launcher library: games of one category as a grid of covers or a carousel.
 * @ingroup view
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "views.h"
#include "../launcher_ui.h"
#include "../library.h"
#include "../settings.h"
#include "../sound.h"
#include "../theme.h"

#define MAX_REMEMBERED_CATEGORIES   (32)
#define TITLE_EXTRA_HIGHLIGHT_ONLY  (48)

#define CAROUSEL_CENTER_Y           (222)   /* Vertical center of the covers */
#define CAROUSEL_SIDE_SCALE         (0.66f) /* Size of the covers beside the highlighted one */
#define CAROUSEL_SPACING            (14)
#define CAROUSEL_REACH              (4)     /* Covers drawn on each side */
#define CAROUSEL_EASE               (0.35f) /* Fraction of the remaining distance moved per frame */
#define CAROUSEL_FAST_STEP          (5)
#define CAROUSEL_TITLE_Y            (330)

#define CATEGORY_PILL_Y             (LAUNCHER_SUBHEADER_Y - 2)

static const launcher_hint_t hints_grid[] = {
    { LAUNCHER_BUTTON_A, "A", "Open" },
    { LAUNCHER_BUTTON_B, "B", "Home" },
    { LAUNCHER_BUTTON_OTHER, "L R", "Category" },
    { LAUNCHER_BUTTON_OTHER, "C", "Size" },
    { LAUNCHER_BUTTON_OTHER, "Z", "Layout" },
    { LAUNCHER_BUTTON_START, "Start", "Menu" },
};

static const launcher_hint_t hints_carousel[] = {
    { LAUNCHER_BUTTON_A, "A", "Open" },
    { LAUNCHER_BUTTON_B, "B", "Home" },
    { LAUNCHER_BUTTON_OTHER, "L R", "Category" },
    { LAUNCHER_BUTTON_OTHER, "Z", "Layout" },
    { LAUNCHER_BUTTON_START, "Start", "Menu" },
};

static const launcher_hint_t hints_empty[] = {
    { LAUNCHER_BUTTON_B, "B", "Home" },
    { LAUNCHER_BUTTON_START, "Start", "Menu" },
};

#define HINT_COUNT(h) ((int)(sizeof(h) / sizeof((h)[0])))

static int category_index = 0;
static int selections[MAX_REMEMBERED_CATEGORIES];
static int top_row = 0;
static float carousel_position = 0.0f;

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
    static const int landscape_cols[] = { 6, 4, 3 };
    static const int portrait_heights[] = { 84, 100, 115 };
    grid_geometry_t g;

    g.gap = gaps[tile_size];
    if (portrait) {
        g.h = portrait_heights[tile_size];
        g.w = (g.h * LAUNCHER_COVER_H) / LAUNCHER_COVER_W;
        g.cols = (LAUNCHER_WIDTH + g.gap) / (g.w + g.gap);
    } else {
        g.cols = landscape_cols[tile_size];
        g.w = (LAUNCHER_WIDTH - g.gap * (g.cols - 1)) / g.cols;
        if (g.w > LAUNCHER_COVER_W) {
            g.w = LAUNCHER_COVER_W;
        }
        g.h = (g.w * LAUNCHER_COVER_H) / LAUNCHER_COVER_W;
    }
    g.row_h = g.h + LAUNCHER_TITLE_SPACE + g.gap;
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

static void switch_category (int delta) {
    int count = library_category_count();
    if (count < 2) {
        return;
    }
    category_index = (category_index + delta + count) % count;
    top_row = 0;
    carousel_position = (float)(*current_selection());
    sound_play_effect(SFX_CURSOR);
}

static void launch (menu_t *menu, library_game_t *game) {
    path_free(menu->load.launcher_rom_path);
    menu->load.launcher_rom_path = library_game_path(game);
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
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

    if (menu->actions.settings) {
        launcher_menu_show();
        return;
    }
    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_HOME;
        return;
    }
    if (menu->actions.trigger_z) {
        menu->settings.launcher_layout = (menu->settings.launcher_layout == LAUNCHER_LAYOUT_GRID)
            ? LAUNCHER_LAYOUT_CAROUSEL
            : LAUNCHER_LAYOUT_GRID;
        settings_save(&menu->settings);
        top_row = 0;
        carousel_position = (float)(*current_selection());
        sound_play_effect(SFX_SETTING);
        return;
    }
    if (menu->actions.shoulder_l) {
        switch_category(-1);
        return;
    }
    if (menu->actions.shoulder_r) {
        switch_category(1);
        return;
    }

    library_category_t *c = library_category(category_index);
    if (!c || c->count == 0) {
        return;
    }

    int *sel = current_selection();
    if (*sel >= c->count) {
        *sel = c->count - 1;
    }
    int previous = *sel;

    if (menu->settings.launcher_layout == LAUNCHER_LAYOUT_CAROUSEL) {
        int step = menu->actions.go_fast ? CAROUSEL_FAST_STEP : 1;
        if (menu->actions.go_left) {
            *sel = (*sel - step < 0) ? 0 : *sel - step;
        } else if (menu->actions.go_right) {
            *sel = (*sel + step > c->count - 1) ? c->count - 1 : *sel + step;
        }
        if (*sel != previous) {
            sound_play_effect(SFX_CURSOR);
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
            settings_save(&menu->settings);
            sound_play_effect(SFX_SETTING);
        }
        return;
    }

    grid_geometry_t g = grid_geometry(menu->settings.launcher_tile_size, c->portrait);
    int step = menu->actions.go_fast ? (g.cols * g.rows) : g.cols;

    if (menu->actions.go_left && *sel > 0) {
        *sel -= 1;
    } else if (menu->actions.go_right && *sel < c->count - 1) {
        *sel += 1;
    } else if (menu->actions.go_up) {
        if (*sel - step >= 0) {
            *sel -= step;
        } else if (menu->actions.go_fast) {
            *sel %= g.cols;
        }
    } else if (menu->actions.go_down) {
        if (*sel + step < c->count) {
            *sel += step;
        } else if ((*sel / g.cols) < ((c->count - 1) / g.cols)) {
            // A shorter last row: land on its last game.
            *sel = c->count - 1;
        }
    }

    if (*sel != previous) {
        sound_play_effect(SFX_CURSOR);
    }

    if (menu->actions.enter) {
        launch(menu, &c->games[*sel]);
    }
}

static void draw_category_tabs (void) {
    int count = library_category_count();
    if (count < 2) {
        const char *name = library_category_name(0);
        if (name) {
            launcher_name(LAUNCHER_X0, LAUNCHER_SUBHEADER_Y, 0, ALIGN_LEFT, STL_GRAY, name);
        }
        return;
    }
    int x = LAUNCHER_X0;
    for (int i = 0; i < count && x < LAUNCHER_X1 - 80; i++) {
        x += launcher_draw_pill(x, CATEGORY_PILL_Y, library_category_name(i), i == category_index) + 8;
    }
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

    // Ask for the highlighted cover first so it loads before its neighbours.
    bool pending;
    game_cover(menu, &c->games[sel], g.w, g.h, &lookups_left, &pending);

    int first = top_row * g.cols;
    int last = first + g.rows * g.cols;
    if (last > c->count) {
        last = c->count;
    }

    for (int i = first; i < last; i++) {
        int col = i % g.cols;
        int r = (i / g.cols) - top_row;
        int x = g.x0 + col * (g.w + g.gap);
        int y = LAUNCHER_CONTENT_Y + r * g.row_h;
        bool selected = (i == sel);

        surface_t *image = game_cover(menu, &c->games[i], g.w, g.h, &lookups_left, &pending);
        launcher_draw_tile(x, y, g.w, g.h, image, pending, c->games[i].title, selected, theme_current()->bg);

        if (selected || !highlight_only) {
            int extra = highlight_only ? TITLE_EXTRA_HIGHLIGHT_ONLY : (g.gap / 2) - 1;
            int tx = x - extra;
            int tw = g.w + extra * 2;
            if (tx < LAUNCHER_X0) {
                tw -= LAUNCHER_X0 - tx;
                tx = LAUNCHER_X0;
            }
            if (tx + tw > LAUNCHER_X1) {
                tw = LAUNCHER_X1 - tx;
            }
            launcher_name(tx, y + g.h + LAUNCHER_RING + 3, tw, ALIGN_CENTER, selected ? STL_DEFAULT : STL_GRAY, c->games[i].title);
        }
    }

    // Preload the row below so scrolling down shows covers straight away.
    for (int i = last; i < last + g.cols && i < c->count; i++) {
        game_cover(menu, &c->games[i], g.w, g.h, &lookups_left, &pending);
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
        *center_x = 320.0f + t * near;
    } else {
        *scale = CAROUSEL_SIDE_SCALE;
        *center_x = 320.0f + copysignf(near + (a - 1.0f) * (sw + CAROUSEL_SPACING), t);
    }
}

static void draw_carousel (menu_t *menu, library_category_t *c, int sel) {
    int cw = c->portrait ? LAUNCHER_COVER_H : LAUNCHER_COVER_W;
    int ch = c->portrait ? LAUNCHER_COVER_W : LAUNCHER_COVER_H;
    int sw = (int)(cw * CAROUSEL_SIDE_SCALE);
    int lookups_left = 1;
    bool pending;

    // Ease toward the selection; snap when close.
    carousel_position += (sel - carousel_position) * CAROUSEL_EASE;
    if (fabsf(sel - carousel_position) < 0.02f) {
        carousel_position = (float)sel;
    }

    // Highlighted cover first so it loads before its neighbours.
    game_cover(menu, &c->games[sel], cw, ch, &lookups_left, &pending);

    // Far covers first so nearer ones draw on top.
    for (int reach = CAROUSEL_REACH + 1; reach >= 0; reach--) {
        for (int side = -1; side <= 1; side += 2) {
            if (reach == 0 && side == 1) {
                continue;
            }
            int i = (int)roundf(carousel_position) + side * reach;
            if (i < 0 || i >= c->count) {
                continue;
            }
            float center_x, scale;
            carousel_slot(i - carousel_position, cw, sw, &center_x, &scale);
            int w = (int)(cw * scale);
            int h = (int)(ch * scale);
            int x = (int)center_x - w / 2;
            int y = CAROUSEL_CENTER_Y - h / 2;
            if (x + w < 0 || x > 640) {
                continue;
            }

            surface_t *image = game_cover(menu, &c->games[i], cw, ch, &lookups_left, &pending);
            bool settled = (i == sel) && (carousel_position == (float)sel);
            if (settled) {
                launcher_draw_tile(x, y, w, h, image, pending, c->games[i].title, true, theme_current()->bg);
            } else if (image) {
                launcher_draw_cover_scaled(image, x, y, w, h, theme_current()->bg);
            } else {
                launcher_draw_tile(x, y, w, h, NULL, true, NULL, false, theme_current()->bg);
            }
        }
    }

    char position[64];
    snprintf(position, sizeof(position), "%s  -  %d of %d", c->name, sel + 1, c->count);
    launcher_name(LAUNCHER_X0, CAROUSEL_TITLE_Y, LAUNCHER_WIDTH, ALIGN_CENTER, STL_DEFAULT, c->games[sel].title);
    launcher_name(LAUNCHER_X0, CAROUSEL_TITLE_Y + 24, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, position);
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
        draw_category_tabs();
        int *sel = current_selection();
        if (c->count == 0) {
            launcher_text(LAUNCHER_X0, 200, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "This folder has no games");
        } else {
            if (*sel >= c->count) {
                *sel = c->count - 1;
            }
            if (menu->settings.launcher_layout == LAUNCHER_LAYOUT_CAROUSEL) {
                draw_carousel(menu, c, *sel);
            } else {
                launcher_text(LAUNCHER_X0, LAUNCHER_SUBHEADER_Y, LAUNCHER_WIDTH, ALIGN_RIGHT, STL_GRAY, "%d / %d", *sel + 1, c->count);
                draw_grid(menu, c, *sel);
            }
        }
        if (menu->settings.launcher_layout == LAUNCHER_LAYOUT_CAROUSEL) {
            launcher_draw_footer(hints_carousel, HINT_COUNT(hints_carousel));
        } else {
            launcher_draw_footer(hints_grid, HINT_COUNT(hints_grid));
        }
    }

    launcher_menu_draw();

    rdpq_detach_show();
}


void view_library_init (menu_t *menu) {
    library_init(menu);
    launcher_menu_init();
    if (category_index >= library_category_count()) {
        category_index = 0;
        top_row = 0;
    }
    carousel_position = (float)(*current_selection());
}

void view_library_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);
}
