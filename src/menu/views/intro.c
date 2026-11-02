/**
 * @file intro.c
 * @brief Launcher startup intro: a short title card in the theme's colors before Home or the Library.
 * @ingroup view
 */

#include "views.h"
#include "../launcher_ui.h"
#include "../theme.h"

#define INTRO_MS            (1600)  /* Whole intro, then on to the launcher */
#define BAR_MS              (450)   /* Accent bar grows out from the center */
#define TITLE_START_MS      (250)   /* Title revealed from the center outwards... */
#define TITLE_MS            (650)   /* ...over this long */
#define SUBTITLE_START_MS   (750)
#define BAR_WIDTH           (240)
#define BAR_HEIGHT          (4)
#define TITLE_Y             (196)
#define BAR_Y               (234)
#define SUBTITLE_Y          (246)
#define TITLE_HALF_WIDTH    (160)   /* Reveal width either side of the center when fully shown */

static uint64_t started_ms = 0;


/** @brief 0 before start, 1 after start + duration, eased out (fast, then settling) in between. */
static float ease_out (uint64_t elapsed, int start, int duration) {
    if (elapsed <= (uint64_t)start) {
        return 0.0f;
    }
    float t = (float)(elapsed - start) / duration;
    if (t >= 1.0f) {
        return 1.0f;
    }
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

static bool any_input (menu_t *menu) {
    return menu->actions.go_up || menu->actions.go_down || menu->actions.go_left || menu->actions.go_right ||
        menu->actions.enter || menu->actions.back || menu->actions.options || menu->actions.settings ||
        menu->actions.lz_context || menu->actions.shoulder_l || menu->actions.shoulder_r || menu->actions.trigger_z;
}

static void process (menu_t *menu) {
    if (any_input(menu) || get_ticks_ms() - started_ms >= INTRO_MS) {
        menu->next_mode = menu->settings.launcher_start_in_library ? MENU_MODE_LIBRARY : MENU_MODE_HOME;
    }
}

static void draw (surface_t *d) {
    rdpq_attach(d, NULL);
    launcher_draw_background();

    uint64_t elapsed = get_ticks_ms() - started_ms;

    float bar = ease_out(elapsed, 0, BAR_MS);
    int half = (int)(bar * BAR_WIDTH / 2);
    if (half > 0) {
        launcher_fill_rounded(LAUNCHER_CENTER_X - half, BAR_Y, LAUNCHER_CENTER_X + half, BAR_Y + BAR_HEIGHT, 2, theme_current()->accent);
    }

    // The title is drawn in full and clipped to a window that opens from the center.
    float reveal = ease_out(elapsed, TITLE_START_MS, TITLE_MS);
    int open = (int)(reveal * TITLE_HALF_WIDTH);
    if (open > 0) {
        int center = display_get_width() / 2;
        rdpq_set_scissor(center - open, 0, center + open, display_get_height());
        launcher_title(LAUNCHER_X0, TITLE_Y, LAUNCHER_WIDTH, ALIGN_CENTER, STL_DEFAULT, "SummerCart64");
        rdpq_set_scissor(0, 0, display_get_width(), display_get_height());
    }

    if (elapsed >= SUBTITLE_START_MS) {
        launcher_small(LAUNCHER_X0, SUBTITLE_Y, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "Launcher");
    }

    rdpq_detach_show();
}


void view_intro_init (menu_t *menu) {
    (void)menu;
    started_ms = get_ticks_ms();
}

void view_intro_display (menu_t *menu, surface_t *display) {
    launcher_set_widescreen(menu->settings.launcher_widescreen);

    process(menu);

    draw(display);
}
