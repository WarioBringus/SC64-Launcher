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
#define SLIDE_IN_MS         (900)   /* Time the covers take to move on: the old one off to the left as the new one comes in */
#define COVER_CENTER_Y      (196)
#define LANDSCAPE_SCALE     (2.0f)
#define PORTRAIT_SCALE      (1.75f)
#define TITLE_Y             (356)
#define DETAILS_Y           (392)
#define HINT_Y              (430)

typedef struct {
    library_game_t *game;
    bool portrait;
    bool small;                 /**< Its large cover didn't load: the 294x210 one is used */
} slide_t;

static slide_t current;
static slide_t next;
static slide_t previous;                /* Sliding off to the left while current comes in */
static bool have_previous = false;
static bool have_current = false;
static uint64_t shown_at_ms = 0;
static menu_mode_t back_mode = MENU_MODE_HOME;
static bool seeded = false;


/** @brief A random game from the whole library (each game equally likely). */
static slide_t pick_game (void) {
    slide_t s = { NULL, false, false };
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
            if (library_game_hidden(s.game)) {
                s.game = NULL;      // Hidden: the next pick tries again.
            }
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
    if (!s->small && launcher_covers_have_pack(menu->storage_prefix, LAUNCHER_WALL_W, LAUNCHER_WALL_H)) {
        surface_t *image = launcher_cover_get(path, LAUNCHER_WALL_W, LAUNCHER_WALL_H, pending);   // Large hi-res, 1:1
        if (image || *pending) {
            return image;
        }
        s->small = true;    // Not tried again for this slide.
        // It didn't load: a 420x300 cover needs ~190 KB in one piece, which a long-running menu's memory may not
        // have (the performance log). The 294x210 one, half that, stands in rather than leaving the wallpaper stuck.
    }
    if (launcher_covers_have_pack(menu->storage_prefix, LAUNCHER_HIRES_W, LAUNCHER_HIRES_H)) {
        return launcher_cover_get(path, LAUNCHER_HIRES_W, LAUNCHER_HIRES_H, pending);   // Hi-res, shown 1:1
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
    bool wall = launcher_covers_have_pack(menu->storage_prefix, LAUNCHER_WALL_W, LAUNCHER_WALL_H);
    if (image && (image->width >= LAUNCHER_HIRES_W - 2 || image->height >= LAUNCHER_HIRES_H - 2)) {
        // A hi-res cover: drawn at its own size, 1:1, never enlarged.
        w = image->width;
        h = image->height;
    } else if (!image && wall) {
        w = s->portrait ? LAUNCHER_WALL_H * LAUNCHER_WALL_H / LAUNCHER_WALL_W : LAUNCHER_WALL_W;
        h = LAUNCHER_WALL_H;
    } else if (!image && launcher_covers_have_pack(menu->storage_prefix, LAUNCHER_HIRES_W, LAUNCHER_HIRES_H)) {
        w = s->portrait ? LAUNCHER_HIRES_H * LAUNCHER_HIRES_H / LAUNCHER_HIRES_W : LAUNCHER_HIRES_W;
        h = LAUNCHER_HIRES_H;
    }
    int x = LAUNCHER_CENTER_X - w / 2 + offset_x;
    int y = COVER_CENTER_Y - h / 2;
    if (image && w == image->width && h == image->height) {
        launcher_draw_tile(x, y, w, h, image, false, NULL, false, theme_current()->bg);
    } else if (image) {
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

/** @brief A random game that isn't hidden (a few tries). */
static slide_t pick_visible_game (void) {
    slide_t s = { NULL, false, false };
    for (int attempt = 0; attempt < 16 && !s.game; attempt++) {
        s = pick_game();
    }
    return s;
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
        current = pick_visible_game();
        have_current = true;
        shown_at_ms = now;
        next = pick_visible_game();
    }
    // Keep the next cover loading; move on once its time is up and it's ready.
    slide_cover(menu, &next, &pending);
    if (now - shown_at_ms > SLIDE_MS && !pending) {
        previous = current;
        have_previous = true;
        current = next;
        next = pick_visible_game();
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
        // Like a carousel: the cover on show moves off to the left while the next comes in from the right and
        // settles in the centre, both a whole screen width apart, so neither pops on or off.
        uint64_t elapsed = get_ticks_ms() - shown_at_ms;
        float t = elapsed >= SLIDE_IN_MS ? 1.0f : (float)elapsed / SLIDE_IN_MS;
        float ease = t * t * (3.0f - 2.0f * t);
        int travel = LAUNCHER_SCREEN_W;
        if (t < 1.0f && have_previous) {
            draw_slide(menu, &previous, -(int)(ease * travel));
        }
        draw_slide(menu, &current, (int)((1.0f - ease) * travel));
    }

    if (menu->current_time >= 0) {
        char clock[16];
        launcher_clock_text(menu, clock, sizeof(clock));
        launcher_text(LAUNCHER_X0, LAUNCHER_HEADER_Y, LAUNCHER_WIDTH, ALIGN_RIGHT, STL_GRAY, "%s", clock);
    }
    // The song, top left level with the clock: the covers reach under the clock's corner as they move.
    launcher_draw_now_playing(LAUNCHER_X0, LAUNCHER_HEADER_Y + 2, LAUNCHER_WIDTH / 2, ALIGN_LEFT);
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
    have_previous = false;
}

void view_attract_display (menu_t *menu, surface_t *display) {
    launcher_set_widescreen(menu->settings.launcher_widescreen);
    launcher_set_cartridge_art(menu->settings.launcher_cartridge_art);

    process(menu);

    draw(menu, display);
}
