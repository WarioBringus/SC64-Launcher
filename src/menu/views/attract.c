/**
 * @file attract.c
 * @brief Launcher attract mode: after a while idle, a slow slideshow of random games' covers.
 * @ingroup view
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "views.h"
#include "../launcher_ui.h"
#include "../library.h"
#include "../theme.h"

#define SLIDE_MS            (7000)  /* Time each game is shown */
#define SLIDE_IN_MS         (700)   /* Time a new cover takes to slide in */
#define COVER_CENTER_Y      (200)
#define LANDSCAPE_SCALE     (2.0f)
#define PORTRAIT_SCALE      (1.75f)
#define TITLE_Y             (340)
#define DETAILS_Y           (376)
#define HINT_Y              (430)

typedef struct {
    library_game_t *game;
    bool portrait;
} slide_t;

static slide_t current;
static slide_t next;
static bool have_current = false;
static uint64_t shown_at_ms = 0;
static menu_mode_t back_mode = MENU_MODE_HOME;
static bool seeded = false;


/** @brief A random game from the whole library (each game equally likely). */
static slide_t pick_game (void) {
    slide_t s = { NULL, false };
    int categories = library_category_count();
    int total = 0;
    for (int i = 0; i < categories; i++) {
        library_category_t *c = library_category(i);
        total += c ? c->count : 0;
    }
    if (total == 0) {
        return s;
    }
    int pick = rand() % total;
    for (int i = 0; i < categories; i++) {
        library_category_t *c = library_category(i);
        if (!c) {
            continue;
        }
        if (pick < c->count) {
            s.game = &c->games[pick];
            s.portrait = c->portrait;
            return s;
        }
        pick -= c->count;
    }
    return s;
}

static surface_t *slide_cover (menu_t *menu, slide_t *s, bool *pending) {
    *pending = false;
    if (!s->game) {
        return NULL;
    }
    const char *path = library_game_cover(menu, s->game);
    if (!path) {
        return NULL;
    }
    return launcher_cover_get(path, s->portrait ? LAUNCHER_COVER_H : LAUNCHER_COVER_W,
        s->portrait ? LAUNCHER_COVER_W : LAUNCHER_COVER_H, pending);
}

static bool any_input (menu_t *menu) {
    return menu->actions.go_up || menu->actions.go_down || menu->actions.go_left || menu->actions.go_right ||
        menu->actions.enter || menu->actions.back || menu->actions.options || menu->actions.settings ||
        menu->actions.lz_context || menu->actions.shoulder_l || menu->actions.shoulder_r || menu->actions.trigger_z;
}

static void draw_slide (menu_t *menu, slide_t *s, int offset_x) {
    bool pending;
    surface_t *image = slide_cover(menu, s, &pending);
    float scale = s->portrait ? PORTRAIT_SCALE : LANDSCAPE_SCALE;
    int w = (int)((s->portrait ? LAUNCHER_COVER_H : LAUNCHER_COVER_W) * scale);
    int h = (int)((s->portrait ? LAUNCHER_COVER_W : LAUNCHER_COVER_H) * scale);
    int x = LAUNCHER_CENTER_X - w / 2 + offset_x;
    int y = COVER_CENTER_Y - h / 2;
    if (image) {
        launcher_draw_cover_scaled(image, x, y, w, h, theme_current()->bg);
    } else {
        launcher_draw_tile(x, y, w, h, NULL, pending, s->game ? s->game->title : NULL, false, theme_current()->bg);
    }

    if (s->game) {
        launcher_title(LAUNCHER_X0 + offset_x, TITLE_Y, LAUNCHER_WIDTH, ALIGN_CENTER, STL_DEFAULT, s->game->title);
        char details[160];
        library_game_details(s->game, details, sizeof(details));
        if (details[0]) {
            launcher_small(LAUNCHER_X0 + offset_x, DETAILS_Y, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, details);
        }
    }
}

static void process (menu_t *menu) {
    if (any_input(menu)) {
        launcher_reset_idle();
        menu->next_mode = back_mode;
        return;
    }

    uint64_t now = get_ticks_ms();
    bool pending;
    if (!have_current) {
        current = pick_game();
        have_current = true;
        shown_at_ms = now;
        next = pick_game();
    }
    // Keep the next cover loading; move on once its time is up and it's ready.
    slide_cover(menu, &next, &pending);
    if (now - shown_at_ms > SLIDE_MS && !pending) {
        current = next;
        next = pick_game();
        shown_at_ms = now;
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    launcher_covers_begin_frame();
    launcher_draw_background();

    if (!current.game) {
        launcher_text(LAUNCHER_X0, 200, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "No games found");
    } else {
        // New covers slide in from the right, easing out.
        uint64_t elapsed = get_ticks_ms() - shown_at_ms;
        float t = elapsed >= SLIDE_IN_MS ? 1.0f : (float)elapsed / SLIDE_IN_MS;
        float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        int offset = (int)((1.0f - ease) * LAUNCHER_WIDTH / 2);
        draw_slide(menu, &current, offset);
    }

    if (menu->current_time >= 0) {
        char clock[16];
        struct tm *t = localtime(&menu->current_time);
        strftime(clock, sizeof(clock), "%I:%M %p", t);
        launcher_text(LAUNCHER_X0, LAUNCHER_HEADER_Y, LAUNCHER_WIDTH, ALIGN_RIGHT, STL_GRAY, "%s", clock[0] == '0' ? clock + 1 : clock);
    }
    launcher_small(LAUNCHER_X0, HINT_Y, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "Press any button");

    rdpq_detach_show();
}


void view_attract_init (menu_t *menu) {
    if (menu->launcher_return_mode == MENU_MODE_HOME || menu->launcher_return_mode == MENU_MODE_LIBRARY) {
        back_mode = menu->launcher_return_mode;
    }
    if (!seeded) {
        srand(TICKS_READ());
        seeded = true;
    }
    library_init(menu);
    have_current = false;
}

void view_attract_display (menu_t *menu, surface_t *display) {
    launcher_set_widescreen(menu->settings.launcher_widescreen);
    launcher_set_cartridge_art(menu->settings.launcher_cartridge_art);

    process(menu);

    draw(menu, display);
}
