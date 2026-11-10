/**
 * @file tour.c
 * @brief Screenshot tour (see tour.h). Each scene: the settings set, the screen given time to settle (glides done,
 * covers loaded), then the framebuffer written raw to menu/tour/NNN.raw, listed in menu/tour/index.txt.
 * pc-tools/tour/tour_png.py turns them into PNGs. The player's settings are put back at the end.
 * @ingroup menu
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "launcher_ui.h"
#include "settings.h"
#include "theme.h"
#include "tour.h"
#include "utils/fs.h"

#define TOUR_TRIGGER        "menu/screenshot-tour.txt"
#define TOUR_DIR            "menu/tour"
#define SETTLE_FRAMES       (75)    /* A scene's frames before it's saved: glides finished, covers in */
#define FIRST_FRAMES        (200)   /* ...for a screen just entered or resized (covers read from the card) */
#define STARTUP_FRAMES      (400)   /* The first: Home fades in once its covers are loaded (up to ~3.3 s) */
#define QUICK_AT            (20)    /* The Quick menu opens this many frames into its scene */

typedef enum {
    SCENE_HOME,
    SCENE_LIBRARY,
    SCENE_SETTINGS,
} scene_screen_t;

typedef struct {
    uint8_t screen;
    uint8_t theme;
    uint8_t style;      /**< Home style */
    uint8_t wide;
    uint8_t lists;      /**< Bits: 1 Resume, 2 Recently played, 4 Favorites */
    uint8_t layout;     /**< Library layout */
    uint8_t quick;      /**< The Quick menu open */
} scene_t;

static bool active = false;
static scene_t *scenes = NULL;
static int scene_count = 0;
static int scene_at = 0;
static int frame_in_scene = 0;
static int frames_needed = 0;
static settings_t saved;
static FILE *index_file = NULL;
static const char *prefix = "";

static const char *style_names[] = { "binder", "spotlight", "cartridge", "minimal" };
static const char *layout_names[] = { "grid", "carousel", "shelf" };

static void add (scene_screen_t screen, int theme, int style, bool wide, int lists, int layout, bool quick) {
    scenes[scene_count++] = (scene_t) { screen, theme, style, wide, lists, layout, quick };
}

/** @brief Every scene, in an order that changes the screen size and the screen as rarely as possible. */
static void build_scenes (void) {
    int themes = theme_count();
    int max = 4 * themes * 2 + 6 * 4 * 3 + 3 * themes * 2 + themes * 3 + 8;
    scenes = calloc(max, sizeof(scene_t));
    int castle = theme_find("Castle Sky"), peach = theme_find("Peach Castle");
    for (int wide = 1; wide >= 0; wide--) {
        // Home: every style in every theme, all lists on.
        for (int style = 0; style < 4; style++) {
            for (int t = 0; t < themes; t++) {
                add(SCENE_HOME, t, style, wide, 7, 0, false);
            }
        }
        // Home: the other list combinations, on a dark and a light theme (16:9), on the dark one at 4:3.
        for (int style = 0; style < 4; style++) {
            for (int lists = 1; lists < 7; lists++) {
                add(SCENE_HOME, castle, style, wide, lists, 0, false);
                if (wide) {
                    add(SCENE_HOME, peach, style, wide, lists, 0, false);
                }
            }
        }
        // The Library: every layout in every theme.
        for (int layout = 0; layout < 3; layout++) {
            for (int t = 0; t < themes; t++) {
                add(SCENE_LIBRARY, t, 0, wide, 7, layout, false);
            }
        }
        if (wide) {
            // The Quick menu on Home and in the Library, and Settings, in every theme.
            for (int t = 0; t < themes; t++) {
                add(SCENE_HOME, t, 0, wide, 7, 0, true);
            }
            for (int t = 0; t < themes; t++) {
                add(SCENE_LIBRARY, t, 0, wide, 7, 0, true);
            }
            for (int t = 0; t < themes; t++) {
                add(SCENE_SETTINGS, t, 0, wide, 7, 0, false);
            }
        }
    }
    assert(scene_count <= max);
}

void tour_start (menu_t *menu) {
    char path[256];
    prefix = menu->storage_prefix;
    snprintf(path, sizeof(path), "%s%s", prefix, TOUR_TRIGGER);
    if (!file_exists(path)) {
        return;
    }
    remove(path);   // Runs once, even if it's cut short.
    snprintf(path, sizeof(path), "%s%s", prefix, TOUR_DIR);
    directory_create(path);
    snprintf(path, sizeof(path), "%s%s/index.txt", prefix, TOUR_DIR);
    index_file = fopen(path, "w");
    saved = menu->settings;
    build_scenes();
    if (index_file) {
        fprintf(index_file, "# screenshot tour: %d scenes, 640x480 RGBA 5551 big-endian, one .raw each\n", scene_count);
        fflush(index_file);
    }
    active = true;
    scene_at = -1;
    frame_in_scene = 0;
    frames_needed = 0;
}

bool tour_active (void) {
    return active;
}

/** @brief The tour's over: the player's settings, theme and screen size back, and Home. */
static void finish (menu_t *menu) {
    launcher_menu_close();
    bool resized = (menu->settings.launcher_widescreen != saved.launcher_widescreen);
    menu->settings = saved;
    theme_set(theme_find(menu->settings.theme));
    settings_save_later(&menu->settings);   // In case a screen wrote a setting meanwhile: the player's go back on the card.
    if (resized) {
        launcher_covers_reset();
    }
    if (index_file) {
        fprintf(index_file, "# finished\n");
        fclose(index_file);
        index_file = NULL;
    }
    free(scenes);
    scenes = NULL;
    active = false;
    menu->next_mode = MENU_MODE_HOME;
}

/** @brief A game for the Quick menu's This game section: the last one played. */
static path_t *some_game (menu_t *menu) {
    bookkeeping_item_t *item = &menu->bookkeeping.history_items[0];
    if (item->bookkeeping_type == BOOKKEEPING_TYPE_EMPTY || !item->primary_path) {
        return NULL;
    }
    return path_clone(item->primary_path);
}

bool tour_before_frame (menu_t *menu) {
    if (!active) {
        return false;
    }
    memset(&menu->actions, 0, sizeof(menu->actions));
    launcher_reset_idle();      // No attract mode while it runs.
    if (scene_at >= 0 && frame_in_scene < frames_needed) {
        frame_in_scene++;
        scene_t *s = &scenes[scene_at];
        if (s->quick && frame_in_scene == QUICK_AT && menu->mode == menu->next_mode) {
            path_t *rom = some_game(menu);
            if (rom || s->screen == SCENE_LIBRARY) {
                launcher_menu_show(menu, rom);
            }
        }
        return false;
    }

    // The next scene, once startup is over and Home is up.
    if (scene_at < 0 && (menu->mode != MENU_MODE_HOME || menu->next_mode != MENU_MODE_HOME)) {
        return false;
    }
    scene_at++;
    if (scene_at >= scene_count) {
        finish(menu);
        return false;
    }
    scene_t *s = &scenes[scene_at];
    scene_t *before = (scene_at > 0) ? &scenes[scene_at - 1] : NULL;
    launcher_menu_close();
    theme_set(s->theme);
    bool resized = (menu->settings.launcher_widescreen != (bool)s->wide);
    menu->settings.launcher_widescreen = s->wide;
    menu->settings.launcher_home_style = s->style;
    menu->settings.launcher_home_resume = (s->lists & 1) != 0;
    menu->settings.launcher_home_recent = (s->lists & 2) != 0;
    menu->settings.launcher_home_favorites = (s->lists & 4) != 0;
    menu->settings.launcher_layout = s->layout;
    menu_mode_t mode = (s->screen == SCENE_LIBRARY) ? MENU_MODE_LIBRARY :
        ((s->screen == SCENE_SETTINGS) ? MENU_MODE_SETTINGS_EDITOR : MENU_MODE_HOME);
    bool entering = !before || before->screen != s->screen || resized || menu->mode != mode;
    if (resized) {
        launcher_covers_reset();
    }
    frame_in_scene = 0;
    frames_needed = (scene_at == 0) ? STARTUP_FRAMES : (entering ? FIRST_FRAMES : SETTLE_FRAMES);
    if (menu->mode != mode) {
        menu->next_mode = mode;     // Entered at the end of this frame.
        return false;
    }
    return true;    // The same screen, started over with the new settings.
}

void tour_after_frame (menu_t *menu, surface_t *display) {
    if (!active || scene_at < 0 || scene_at >= scene_count || frame_in_scene != frames_needed || !display) {
        return;
    }
    frame_in_scene++;   // Saved once.
    rspq_wait();        // The frame finished drawing.
    scene_t *s = &scenes[scene_at];
    char path[256];
    snprintf(path, sizeof(path), "%s%s/%03d.raw", prefix, TOUR_DIR, scene_at + 1);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(display->buffer, display->stride, display->height, f);
        fclose(f);
    }
    if (index_file) {
        const theme_t *t = theme_at(s->theme);
        const char *screen = (s->screen == SCENE_HOME) ? "home" : ((s->screen == SCENE_LIBRARY) ? "library" : "settings");
        fprintf(index_file, "%03d\t%s\t%s\t%s\t%s\tlists=%s%s%s\t%s%s\n", scene_at + 1, screen,
            s->screen == SCENE_HOME ? style_names[s->style] : (s->screen == SCENE_LIBRARY ? layout_names[s->layout] : "-"),
            t ? t->name : "?", s->wide ? "16:9" : "4:3",
            (s->lists & 1) ? "R" : "", (s->lists & 2) ? "P" : "", (s->lists & 4) ? "F" : "",
            s->quick ? "quick-menu" : "-", f ? "" : "\t(not saved)");
        fflush(index_file);
    }
}
