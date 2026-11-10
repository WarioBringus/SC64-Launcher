/**
 * @file menu.c
 * @brief Menu system implementation
 * @ingroup menu
 */

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <libdragon.h>

#include "actions.h"
#include "boot/boot.h"
#include "flashcart/flashcart.h"
#include "fonts.h"
#include "background_music.h"
#include "hdmi.h"
#include "launcher_ui.h"
#include "library.h"
#include "menu_state.h"
#include "menu.h"
#include "perf_log.h"
#include "audio_player.h"
#include "png_decoder.h"
#include "settings.h"
#include "sound.h"
#include "theme.h"
#include "tour.h"
#include "usb_comm.h"
#include "utils/fs.h"
#include "views/views.h"

#define MENU_DIRECTORY              "/menu"
#define MENU_SETTINGS_FILE          "config.ini"
#define MENU_CUSTOM_FONT_FILE       "custom.font64"
#define MENU_CUSTOM_BGM_FILE        "custom.wav64"
#define MENU_ROM_LOAD_HISTORY_FILE  "history.ini"
#define MENU_MUSIC_DIRECTORY        "music"

#define MENU_CACHE_DIRECTORY        "cache"
#define BACKGROUND_CACHE_FILE       "background.data"

#define FPS_LIMIT                   (60.0f)

static menu_t *menu;

static bool interlaced = true;

/**
 * @brief Initialize the menu system.
 * 
 * @param boot_params Pointer to the boot parameters structure.
 */
static void menu_init (boot_params_t *boot_params) {
    menu = calloc(1, sizeof(menu_t));
    assert(menu != NULL);

    menu->boot_params = boot_params;

    menu->mode = MENU_MODE_NONE;
    menu->next_mode = MENU_MODE_STARTUP;

    menu->flashcart_err = flashcart_init(&menu->storage_prefix);
    if (menu->flashcart_err != FLASHCART_OK) {
        menu->next_mode = MENU_MODE_FAULT;
    }

    joypad_init();
    timer_init();
    rtc_init();
    rspq_init();
    rdpq_init();
    dfs_init(DFS_DEFAULT_LOCATION);

    actions_init();
    sound_init_default();
    sound_init_sfx();
    sound_init_bgm();

    hdmi_clear_game_id();

    path_t *path = path_init(menu->storage_prefix, MENU_DIRECTORY);

    directory_create(path_get(path));

    path_push(path, MENU_SETTINGS_FILE);
    settings_init(path_get(path));
    settings_load(&menu->settings);
    perf_log_init(menu->settings.launcher_perf_log, menu->storage_prefix);
    theme_set(theme_find(menu->settings.theme));
    path_pop(path);

    path_push(path, MENU_ROM_LOAD_HISTORY_FILE);
    bookkeeping_init(path_get(path));
    bookkeeping_load(&menu->bookkeeping);
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    menu->launcher_return_mode = MENU_MODE_NONE;
    path_pop(path);

    // Background music: tracks are the .wav64 files in /menu/music (created if missing).
    path_push(path, MENU_MUSIC_DIRECTORY);
    directory_create(path_get(path));
    sound_bgm_set_order(menu->settings.bgm_order);
    sound_bgm_set_volume(menu->settings.bgm_volume);
    sound_bgm_load_folder(path_get(path));
    path_pop(path);

    // Force interlacing off in VI settings for TVs and other devices that struggle with interlaced video input.
    interlaced = !menu->settings.force_progressive_scan;

    resolution_t resolution = {
        .width = 640,
        .height = 480,
        .interlaced = interlaced ? INTERLACE_HALF : INTERLACE_OFF,
    };

    // Three framebuffers with the Expansion Pak: with two, a frame finishing just after a vertical blank
    // waits a whole extra one before the next can start (the performance log showed frames alternating
    // between 16.7 and 33 ms). Without it, memory is too tight, so two.
    display_init(resolution, DEPTH_16_BPP, is_memory_expanded() ? 3 : 2, GAMMA_NONE, interlaced ? FILTERS_DISABLED : FILTERS_RESAMPLE);
    
    if (menu->settings.pal60_enabled) { // it is not given that hardware VI mods understand the output
        tv_type_t tv_type = get_tv_type();
        if (tv_type == TV_PAL) {
            // Set VI timing so it will use 60Hz signal.
            vi_set_timing_preset(&VI_TIMING_PAL60);

            // FIXME: timeout and restore to PAL 50Hz if not shown, 
            // this should be added as a button confirm, or reset combo, rather than re-setting via manual edit of the INI?.
            //vi_set_timing_preset(&VI_TIMING_PAL);
        }
    }
    
    display_set_fps_limit(FPS_LIMIT);

    path_push(path, MENU_CUSTOM_FONT_FILE);
    fonts_init(path_get(path));
    path_pop(path);

    // path_push(path, MENU_CUSTOM_BGM_FILE);
    // bgm_init(path_get(path));
    // path_pop(path);

    path_push(path, MENU_CACHE_DIRECTORY);
    directory_create(path_get(path));

    path_push(path, BACKGROUND_CACHE_FILE);
    ui_components_background_init(path_get(path));

    path_free(path);

    sound_use_sfx(menu->settings.soundfx_enabled);
    sound_use_bgm(menu->settings.bgm_enabled);

    menu->browser.directory = path_init(menu->storage_prefix, menu->settings.default_directory);
    if (!directory_exists(path_get(menu->browser.directory))) {
        path_free(menu->browser.directory);
        menu->browser.directory = path_init(menu->storage_prefix, "/");
    }

    debugf("N64FlashcartMenu debugging...\n");
}

/**
 * @brief Deinitialize the menu system.
 * 
 * @param menu Pointer to the menu structure.
 */
static void menu_deinit (menu_t *menu) {

    sound_deinit();
    
    launcher_covers_reset();
    library_free();
    path_free(menu->load.launcher_rom_path);

    ui_components_background_free();
    ui_components_file_list_free();
    rspq_wait();  // Execute deferred callbacks (e.g., display list freeing) before closing RSPQ

    hdmi_send_game_id(menu->boot_params);

    path_free(menu->load.disk_slots.primary.disk_path);
    path_free(menu->load.rom_path);
    for (int i = 0; i < menu->browser.entries; i++) {
        free(menu->browser.list[i].name);
    }
    free(menu->browser.list);
    path_free(menu->browser.directory);
    free(menu);

    display_close();

    rspq_wait();
    rdpq_close();
    rspq_close();
    rtc_close();
    timer_close();
    joypad_close();

    flashcart_deinit();
}

/**
 * @brief View structure containing initialization and display functions.
 */
typedef const struct {
    menu_mode_t id; /**< View ID */
    void (*init) (menu_t *menu); /**< Initialization function */
    void (*show) (menu_t *menu, surface_t *display); /**< Display function */
} view_t;

static view_t menu_views[] = {
    { MENU_MODE_STARTUP, view_startup_init, view_startup_display },
    { MENU_MODE_BROWSER, view_browser_init, view_browser_display },
    { MENU_MODE_FILE_INFO, view_file_info_init, view_file_info_display },
    { MENU_MODE_SYSTEM_INFO, view_system_info_init, view_system_info_display },
    { MENU_MODE_IMAGE_VIEWER, view_image_viewer_init, view_image_viewer_display },
    { MENU_MODE_TEXT_VIEWER, view_text_viewer_init, view_text_viewer_display },
    { MENU_MODE_MUSIC_PLAYER, view_music_player_init, view_music_player_display },
    { MENU_MODE_CREDITS, view_credits_init, view_credits_display },
    { MENU_MODE_SETTINGS_EDITOR, view_settings_init, view_settings_display },
    { MENU_MODE_RTC, view_rtc_init, view_rtc_display },
    { MENU_MODE_CONTROLLER_PAKFS, view_controller_pakfs_init, view_controller_pakfs_display },
    { MENU_MODE_CONTROLLER_PAK_DUMP_INFO, view_controller_pak_dump_info_init, view_controller_pak_dump_info_display },
    { MENU_MODE_CONTROLLER_PAK_DUMP_NOTE_INFO, view_controller_pak_note_dump_info_init, view_controller_pak_note_dump_info_display },
    { MENU_MODE_FLASHCART, view_flashcart_info_init, view_flashcart_info_display },
    { MENU_MODE_LOAD_ROM, view_load_rom_init, view_load_rom_display },
    { MENU_MODE_LOAD_DISK, view_load_disk_init, view_load_disk_display },
    { MENU_MODE_LOAD_EMULATOR, view_load_emulator_init, view_load_emulator_display },
    { MENU_MODE_ERROR, view_error_init, view_error_display },
    { MENU_MODE_FAULT, view_fault_init, view_fault_display },
    { MENU_MODE_FAVORITE, view_favorite_init, view_favorite_display },
    { MENU_MODE_HISTORY, view_history_init, view_history_display },
    { MENU_MODE_DATEL_CODE_EDITOR, view_datel_code_editor_init, view_datel_code_editor_display },
    { MENU_MODE_EXTRACT_FILE, view_extract_file_init, view_extract_file_display },
    { MENU_MODE_LIBRARY, view_library_init, view_library_display },
    { MENU_MODE_HOME, view_home_init, view_home_display },
    { MENU_MODE_ABOUT, view_about_init, view_about_display },
    { MENU_MODE_ATTRACT, view_attract_init, view_attract_display }
};

/**
 * @brief Whether a mode is one of the launcher's own views.
 */
/**
 * @brief Screens the launcher's covers stay loaded through: its own, and its Settings page (which needs neither
 * their memory nor the PNG decoder). Freeing them for Settings made the way back to Home read every cover pack
 * again: a 0.6 s fade with a 170 ms frame in the performance log.
 */
static bool menu_mode_keeps_covers (menu_mode_t mode) {
    return mode == MENU_MODE_LIBRARY || mode == MENU_MODE_HOME || mode == MENU_MODE_ABOUT || mode == MENU_MODE_ATTRACT ||
        mode == MENU_MODE_SETTINGS_EDITOR;
}

/** @brief Whether any button or direction is pressed this frame. */
static bool menu_any_action (menu_t *menu) {
    return menu->actions.go_up || menu->actions.go_down || menu->actions.go_left || menu->actions.go_right ||
        menu->actions.enter || menu->actions.back || menu->actions.options || menu->actions.settings ||
        menu->actions.lz_context || menu->actions.shoulder_l || menu->actions.shoulder_r || menu->actions.trigger_z;
}

static bool menu_mode_is_launcher (menu_mode_t mode) {
    return mode == MENU_MODE_LIBRARY || mode == MENU_MODE_HOME || mode == MENU_MODE_ABOUT || mode == MENU_MODE_ATTRACT;
}

/**
 * @brief Get the view structure for the specified menu mode.
 * 
 * @param id The menu mode ID.
 * @return view_t* Pointer to the view structure.
 */
static view_t *menu_get_view (menu_mode_t id) {
    for (int i = 0; i < sizeof(menu_views) / sizeof(view_t); i++) {
        if (menu_views[i].id == id) {
            return &menu_views[i];
        }
    }
    return NULL;
}

/** @brief The screen named in the performance log. */
static const char *perf_screen_name (menu_t *menu) {
    switch (menu->mode) {
        case MENU_MODE_HOME:
            switch (menu->settings.launcher_home_style) {
                case LAUNCHER_HOME_SPOTLIGHT: return "home-spotlight";
                case LAUNCHER_HOME_CARTRIDGE: return "home-cartridge";
                case LAUNCHER_HOME_MINIMAL: return "home-minimal";
                default: return "home-binder";
            }
        case MENU_MODE_ABOUT: return "game-info";
        case MENU_MODE_ATTRACT: return "screensaver";
        case MENU_MODE_LOAD_ROM: return "loading";
        case MENU_MODE_LIBRARY:
            switch (menu->settings.launcher_layout) {
                case LAUNCHER_LAYOUT_CAROUSEL: return "carousel";
                case LAUNCHER_LAYOUT_SHELF: return "shelf";
                default: return "grid";
            }
        default: return "other";
    }
}

/** @brief Whether anything is pressed or held this frame (the log waits for a rest to write). */
static bool perf_input (menu_t *menu) {
    JOYPAD_PORT_FOREACH (port) {
        if (joypad_get_buttons_held(port).raw || joypad_get_direction(port, JOYPAD_2D_ANY) != JOYPAD_8WAY_NONE) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Run the menu system.
 * 
 * @param boot_params Pointer to the boot parameters structure.
 */
void menu_run (boot_params_t *boot_params) {
    menu_init(boot_params);

    bool held_input = false;     // Input read while the picture was held, for the frame it starts
    uint64_t held_poll_ms = 0;
    while (true) {
        if (!held_input && !tour_active() && launcher_frame_held()) {
            // Nothing moves on screen: the last frame stays on the TV. Only look for a button press.
            uint64_t now = get_ticks_ms();
            if (now - held_poll_ms >= 8) {
                held_poll_ms = now;
                actions_update(menu);
                held_input = menu_any_action(menu);
            }
            if (!held_input) {
                sound_poll();
                png_decoder_poll();
                usb_comm_poll(menu);
                continue;
            }
        }
        surface_t *display = display_try_get();

        if (display != NULL) {
            if (held_input) {
                held_input = false;     // Already read.
            } else {
                actions_update(menu);
            }

            view_t *view = menu_get_view(menu->mode);
            if (tour_before_frame(menu) && view && view->init) {
                view->init(menu);   // The screenshot tour's next scene, on the same screen.
            }
            perf_log_frame_begin(perf_screen_name(menu), perf_input(menu),
                menu->actions.go_up || menu->actions.go_down || menu->actions.go_left || menu->actions.go_right);
            if (view && view->show) {
                view->show(menu, display);
                static int startup_stage = 0;
                if (startup_stage == 1) {
                    // Startup work that can wait until the launcher is on screen.
                    sound_bgm_keep_alive();
                    fonts_load_default();
                    sound_bgm_keep_alive();
                    view_library_warm(menu);
                    startup_stage = 2;
                }
                if (startup_stage == 0 && (menu->mode == MENU_MODE_HOME || menu->mode == MENU_MODE_LIBRARY)) {
                    startup_stage = 1;
                }
            } else {
                rdpq_attach_clear(display, NULL);
                rdpq_detach_wait();
                display_show(display);
            }

            perf_log_frame_end();
            tour_after_frame(menu, display);

            if (menu->mode == MENU_MODE_BOOT) {
                break;
            }

            while (menu->mode != menu->next_mode) {
                // Screens opened from the launcher go back to it rather than to the file browser.
                if (menu->next_mode == MENU_MODE_BROWSER && menu->launcher_return_mode != MENU_MODE_NONE && menu->settings.launcher_enabled) {
                    menu->next_mode = menu->launcher_return_mode;
                }
                // Free cover images when leaving the launcher (other screens need the memory and the PNG decoder).
                if (menu_mode_keeps_covers(menu->mode) && !menu_mode_keeps_covers(menu->next_mode)) {
                    perf_log_leave();
                    launcher_covers_reset();
                }
                // Screens of the original menu use the main font, loaded after startup.
                if (!menu_mode_is_launcher(menu->next_mode) && menu->next_mode != MENU_MODE_STARTUP) {
                    fonts_load_default();
                }

                menu->mode = menu->next_mode;

                view_t *next_view = menu_get_view(menu->next_mode);
                if (next_view && next_view->init) {
                    next_view->init(menu);
                }
            }

            time(&menu->current_time);
        }

        uint64_t polls_started = get_ticks_us();
        sound_poll();

        png_decoder_poll();

        usb_comm_poll(menu);
        // Only the polls right after a drawn frame: the loop also polls over and over while it waits
        // for a free framebuffer, and that is waiting, not work.
        if (display != NULL) {
            perf_log_add(PERF_POLLS, (uint32_t)(get_ticks_us() - polls_started));
        }
    }

    menu_deinit(menu);

    while (exception_reset_time() > 0) {
        // Do nothing if reset button was pressed
    }
}
