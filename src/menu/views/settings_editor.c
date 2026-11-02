#include <stdbool.h>
#include "../sound.h"
#include "../settings.h"
#include "../fonts.h"
#include "../launcher_ui.h"
#include "../theme.h"
#include "views.h"

static bool show_message_reset_settings = false;

static const char *format_switch (bool state) {
    switch (state) {
        case true: return "On";
        case false: return "Off";
    }
}

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
static void set_loading_progress_bar_enabled_type (menu_t *menu, void *arg) {
    menu->settings.loading_progress_bar_enabled = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}
#endif

static void set_protected_entries_type (menu_t *menu, void *arg) {
    menu->settings.show_protected_entries = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);

    menu->browser.reload = true;
}

static void set_use_saves_folder_type (menu_t *menu, void *arg) {
    menu->settings.use_saves_folder = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_show_saves_folder_type (menu_t *menu, void *arg) {
    menu->settings.show_saves_folder = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);

    menu->browser.reload = true;
}

static void set_show_save_files_type (menu_t *menu, void *arg) {
    menu->settings.show_save_files = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);

    menu->browser.reload = true;
}

static void set_show_cheat_files_type (menu_t *menu, void *arg) {
    menu->settings.show_cheat_files = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);

    menu->browser.reload = true;
}
    
static void set_soundfx_enabled_type (menu_t *menu, void *arg) {
    menu->settings.soundfx_enabled = (bool)(uintptr_t)(arg);
    sound_use_sfx(menu->settings.soundfx_enabled);
    settings_save(&menu->settings);
}

static void set_bgm_enabled_type (menu_t *menu, void *arg) {
    menu->settings.bgm_enabled = (bool)(uintptr_t)(arg);
    sound_use_bgm(menu->settings.bgm_enabled);
    settings_save(&menu->settings);
}

static void set_bgm_order_type (menu_t *menu, void *arg) {
    menu->settings.bgm_order = (int)(uintptr_t)(arg);
    sound_bgm_set_order(menu->settings.bgm_order);
    settings_save(&menu->settings);
}

static void set_bgm_volume_type (menu_t *menu, void *arg) {
    menu->settings.bgm_volume = (int)(uintptr_t)(arg);
    sound_bgm_set_volume(menu->settings.bgm_volume);
    settings_save(&menu->settings);
}

static void next_bgm_track (menu_t *menu, void *arg) {
    (void)arg;
    (void)menu;
    sound_bgm_next();
}

static void set_pal60_type (menu_t *menu, void *arg) {
    bool pal60_try_enable = (bool)(uintptr_t)(arg);
    tv_type_t tv_type = get_tv_type();
    // FIXME: we can check it is supported by adding a warning and setting it, with a confirmation.
    if (pal60_try_enable && (tv_type == TV_PAL)) {
        //enable it without needing to reboot the console.
        // FIXME: Add message box to press a button as confirmation. 
        // Set VI timing so it will use 60Hz signal.
        vi_set_timing_preset(&VI_TIMING_PAL60);

        // FIXME: timeout and restore to PAL 50Hz if message not shown, 
        //vi_set_timing_preset(&VI_TIMING_PAL);
        
    }
    else if (!pal60_try_enable && (tv_type == TV_PAL)){
        //disable it without needing to reboot the console.
        // Set VI timing so it will use 50Hz signal.
        vi_set_timing_preset(&VI_TIMING_PAL);
        
    }
    else {
        //not PAL, cannot enable PAL60
        pal60_try_enable = false;
    }
    
    menu->settings.pal60_enabled = pal60_try_enable;
    settings_save(&menu->settings);
}

static void set_wrap_file_list_scrolling_type (menu_t *menu, void *arg) {
    menu->settings.wrap_file_list_scrolling = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

#ifndef FEATURE_AUTOLOAD_ROM_ENABLED
static void set_use_rom_fast_reboot_enabled_type (menu_t *menu, void *arg) {
    menu->settings.rom_fast_reboot_enabled = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}
#endif

#ifdef BETA_SETTINGS
static void set_show_browser_file_extensions_type(menu_t *menu, void *arg) {
    menu->settings.show_browser_file_extensions = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
    menu->browser.reload = true;
}

static void set_show_browser_rom_tags_type (menu_t *menu, void *arg) {
    menu->settings.show_browser_rom_tags = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_rumble_enabled_type (menu_t *menu, void *arg) {
    menu->settings.rumble_enabled = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

// static void set_use_default_settings (menu_t *menu, void *arg) {
//     // FIXME: add implementation
//     menu->browser.reload = true;
// }
#endif

static void set_launcher_enabled_type (menu_t *menu, void *arg) {
    menu->settings.launcher_enabled = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_launcher_categories_enabled_type (menu_t *menu, void *arg) {
    menu->settings.launcher_categories_enabled = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_launcher_titles_highlight_only_type (menu_t *menu, void *arg) {
    menu->settings.launcher_titles_highlight_only = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_launcher_home_resume_type (menu_t *menu, void *arg) {
    menu->settings.launcher_home_resume = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_launcher_home_recent_type (menu_t *menu, void *arg) {
    menu->settings.launcher_home_recent = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_launcher_home_favorites_type (menu_t *menu, void *arg) {
    menu->settings.launcher_home_favorites = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_launcher_layout_type (menu_t *menu, void *arg) {
    menu->settings.launcher_layout = (int)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_launcher_tile_size_type (menu_t *menu, void *arg) {
    menu->settings.launcher_tile_size = (int)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_theme_type (menu_t *menu, void *arg) {
    launcher_apply_theme(menu, (int)(uintptr_t)(arg));
}

static void set_launcher_attract_minutes_type (menu_t *menu, void *arg) {
    menu->settings.launcher_attract_minutes = (int)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_launcher_one_per_game_type (menu_t *menu, void *arg) {
    menu->settings.launcher_one_per_game = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_launcher_intro_type (menu_t *menu, void *arg) {
    menu->settings.launcher_intro = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_launcher_start_in_type (menu_t *menu, void *arg) {
    menu->settings.launcher_start_in_library = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_launcher_quick_launch_type (menu_t *menu, void *arg) {
    menu->settings.launcher_quick_launch = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_launcher_widescreen_type (menu_t *menu, void *arg) {
    menu->settings.launcher_widescreen = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void remove_background_image (menu_t *menu, void *arg) {
    (void)arg;
    (void)menu;
    ui_components_background_clear();
}

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
static int get_loading_progress_bar_enabled_current_selection (menu_t *menu) {
    return menu->settings.loading_progress_bar_enabled ? 0 : 1;
}

static component_context_menu_t set_loading_progress_bar_enabled_context_menu = {
    .get_default_selection = get_loading_progress_bar_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_loading_progress_bar_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_loading_progress_bar_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

static int get_protected_entries_current_selection (menu_t *menu) {
    return menu->settings.show_protected_entries ? 0 : 1;
}

static component_context_menu_t set_protected_entries_type_context_menu = {
    .get_default_selection = get_protected_entries_current_selection,
    .list = {
        {.text = "On", .action = set_protected_entries_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_protected_entries_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_soundfx_enabled_current_selection (menu_t *menu) {
    return menu->settings.soundfx_enabled ? 0 : 1;
}

static component_context_menu_t set_soundfx_enabled_type_context_menu = {
    .get_default_selection = get_soundfx_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_soundfx_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_soundfx_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_bgm_enabled_current_selection (menu_t *menu) {
    return menu->settings.bgm_enabled ? 0 : 1;
}

static component_context_menu_t set_bgm_enabled_type_context_menu = {
    .get_default_selection = get_bgm_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_bgm_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_bgm_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_bgm_order_current_selection (menu_t *menu) {
    return menu->settings.bgm_order;
}

static component_context_menu_t set_bgm_order_context_menu = {
    .get_default_selection = get_bgm_order_current_selection,
    .list = {
        {.text = "Shuffle", .action = set_bgm_order_type, .arg = (void *)(uintptr_t)(SOUND_BGM_ORDER_SHUFFLE) },
        {.text = "In Order", .action = set_bgm_order_type, .arg = (void *)(uintptr_t)(SOUND_BGM_ORDER_IN_ORDER) },
        {.text = "Repeat One", .action = set_bgm_order_type, .arg = (void *)(uintptr_t)(SOUND_BGM_ORDER_REPEAT_ONE) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_bgm_volume_current_selection (menu_t *menu) {
    return menu->settings.bgm_volume;
}

static component_context_menu_t set_bgm_volume_context_menu = {
    .get_default_selection = get_bgm_volume_current_selection,
    .list = {
        {.text = "Low", .action = set_bgm_volume_type, .arg = (void *)(uintptr_t)(SOUND_BGM_VOLUME_LOW) },
        {.text = "Medium", .action = set_bgm_volume_type, .arg = (void *)(uintptr_t)(SOUND_BGM_VOLUME_MEDIUM) },
        {.text = "High", .action = set_bgm_volume_type, .arg = (void *)(uintptr_t)(SOUND_BGM_VOLUME_HIGH) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_use_saves_folder_current_selection (menu_t *menu) {
    return menu->settings.use_saves_folder ? 0 : 1;
}

static component_context_menu_t set_use_saves_folder_type_context_menu = {
    .get_default_selection = get_use_saves_folder_current_selection,
    .list = {
        {.text = "On", .action = set_use_saves_folder_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_use_saves_folder_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_show_saves_folder_current_selection (menu_t *menu) {
    return menu->settings.show_saves_folder ? 0 : 1;
}

static component_context_menu_t set_show_saves_folder_type_context_menu = {
    .get_default_selection = get_show_saves_folder_current_selection,
    .list = {
        {.text = "On", .action = set_show_saves_folder_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_show_saves_folder_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_show_save_files_current_selection (menu_t *menu) {
    return menu->settings.show_save_files ? 0 : 1;
}

static component_context_menu_t set_show_save_files_type_context_menu = {
    .get_default_selection = get_show_save_files_current_selection,
    .list = {
        {.text = "On", .action = set_show_save_files_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_show_save_files_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_show_cheat_files_current_selection (menu_t *menu) {
    return menu->settings.show_cheat_files ? 0 : 1;
}

static component_context_menu_t set_show_cheat_files_type_context_menu = {
    .get_default_selection = get_show_cheat_files_current_selection,
    .list = {
        {.text = "On", .action = set_show_cheat_files_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_show_cheat_files_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_pal60_current_selection (menu_t *menu) {
    return menu->settings.pal60_enabled ? 0 : 1;
}

static component_context_menu_t set_pal60_type_context_menu = {
    .get_default_selection = get_pal60_current_selection,
    .list = {
        {.text = "On", .action = set_pal60_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_pal60_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_wrap_file_list_scrolling_current_selection (menu_t *menu) {
    return menu->settings.wrap_file_list_scrolling ? 0 : 1;
}

static component_context_menu_t set_wrap_file_list_scrolling_context_menu = {
    .get_default_selection = get_wrap_file_list_scrolling_current_selection,
    .list = {
        {.text = "On", .action = set_wrap_file_list_scrolling_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_wrap_file_list_scrolling_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

#ifndef FEATURE_AUTOLOAD_ROM_ENABLED
static int get_use_rom_fast_reboot_current_selection (menu_t *menu) {
    return menu->settings.rom_fast_reboot_enabled ? 0 : 1;
}

static component_context_menu_t set_use_rom_fast_reboot_context_menu = {
    .get_default_selection = get_use_rom_fast_reboot_current_selection,
    .list = {
        {.text = "On", .action = set_use_rom_fast_reboot_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_use_rom_fast_reboot_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

#ifdef BETA_SETTINGS
static int get_show_browser_file_extensions_current_selection (menu_t *menu) {
    return menu->settings.show_browser_file_extensions ? 0 : 1;
}

static component_context_menu_t set_show_browser_file_extensions_context_menu = {
    .get_default_selection = get_show_browser_file_extensions_current_selection,
    .list = {
        { .text = "On", .action = set_show_browser_file_extensions_type, .arg = (void *)(uintptr_t)(true) },
        { .text = "Off", .action = set_show_browser_file_extensions_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_show_browser_rom_tags_current_selection (menu_t *menu) {
    return menu->settings.show_browser_rom_tags ? 0 : 1;
}

static component_context_menu_t set_show_browser_rom_tags_context_menu = {
    .get_default_selection = get_show_browser_rom_tags_current_selection,
    .list = {
        {.text = "On", .action = set_show_browser_rom_tags_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_show_browser_rom_tags_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_rumble_enabled_current_selection (menu_t *menu) {
    return menu->settings.rumble_enabled ? 0 : 1;
}

static component_context_menu_t set_rumble_enabled_type_context_menu = {
    .get_default_selection = get_rumble_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_rumble_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_rumble_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

static int get_launcher_enabled_current_selection (menu_t *menu) {
    return menu->settings.launcher_enabled ? 0 : 1;
}

static component_context_menu_t set_launcher_enabled_context_menu = {
    .get_default_selection = get_launcher_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_launcher_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_launcher_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_categories_enabled_current_selection (menu_t *menu) {
    return menu->settings.launcher_categories_enabled ? 0 : 1;
}

static component_context_menu_t set_launcher_categories_enabled_context_menu = {
    .get_default_selection = get_launcher_categories_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_launcher_categories_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_launcher_categories_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_titles_highlight_only_current_selection (menu_t *menu) {
    return menu->settings.launcher_titles_highlight_only ? 0 : 1;
}

static component_context_menu_t set_launcher_titles_highlight_only_context_menu = {
    .get_default_selection = get_launcher_titles_highlight_only_current_selection,
    .list = {
        {.text = "Highlighted Only", .action = set_launcher_titles_highlight_only_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Always", .action = set_launcher_titles_highlight_only_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_home_resume_current_selection (menu_t *menu) {
    return menu->settings.launcher_home_resume ? 0 : 1;
}

static component_context_menu_t set_launcher_home_resume_context_menu = {
    .get_default_selection = get_launcher_home_resume_current_selection,
    .list = {
        {.text = "On", .action = set_launcher_home_resume_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_launcher_home_resume_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_home_recent_current_selection (menu_t *menu) {
    return menu->settings.launcher_home_recent ? 0 : 1;
}

static component_context_menu_t set_launcher_home_recent_context_menu = {
    .get_default_selection = get_launcher_home_recent_current_selection,
    .list = {
        {.text = "On", .action = set_launcher_home_recent_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_launcher_home_recent_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_home_favorites_current_selection (menu_t *menu) {
    return menu->settings.launcher_home_favorites ? 0 : 1;
}

static component_context_menu_t set_launcher_home_favorites_context_menu = {
    .get_default_selection = get_launcher_home_favorites_current_selection,
    .list = {
        {.text = "On", .action = set_launcher_home_favorites_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_launcher_home_favorites_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_layout_current_selection (menu_t *menu) {
    return menu->settings.launcher_layout;
}

static component_context_menu_t set_launcher_layout_context_menu = {
    .get_default_selection = get_launcher_layout_current_selection,
    .list = {
        {.text = "Grid", .action = set_launcher_layout_type, .arg = (void *)(uintptr_t)(LAUNCHER_LAYOUT_GRID) },
        {.text = "Carousel", .action = set_launcher_layout_type, .arg = (void *)(uintptr_t)(LAUNCHER_LAYOUT_CAROUSEL) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_tile_size_current_selection (menu_t *menu) {
    return menu->settings.launcher_tile_size;
}

static component_context_menu_t set_launcher_tile_size_context_menu = {
    .get_default_selection = get_launcher_tile_size_current_selection,
    .list = {
        {.text = "Small", .action = set_launcher_tile_size_type, .arg = (void *)(uintptr_t)(LAUNCHER_TILE_SMALL) },
        {.text = "Medium", .action = set_launcher_tile_size_type, .arg = (void *)(uintptr_t)(LAUNCHER_TILE_MEDIUM) },
        {.text = "Large", .action = set_launcher_tile_size_type, .arg = (void *)(uintptr_t)(LAUNCHER_TILE_LARGE) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_attract_minutes_current_selection (menu_t *menu) {
    switch (menu->settings.launcher_attract_minutes) {
        case 0: return 0;
        case 2: return 1;
        case 10: return 3;
        default: return 2;
    }
}

static component_context_menu_t set_launcher_attract_minutes_context_menu = {
    .get_default_selection = get_launcher_attract_minutes_current_selection,
    .list = {
        {.text = "Off", .action = set_launcher_attract_minutes_type, .arg = (void *)(uintptr_t)(0) },
        {.text = "After 2 minutes", .action = set_launcher_attract_minutes_type, .arg = (void *)(uintptr_t)(2) },
        {.text = "After 5 minutes", .action = set_launcher_attract_minutes_type, .arg = (void *)(uintptr_t)(5) },
        {.text = "After 10 minutes", .action = set_launcher_attract_minutes_type, .arg = (void *)(uintptr_t)(10) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_one_per_game_current_selection (menu_t *menu) {
    return menu->settings.launcher_one_per_game ? 0 : 1;
}

static component_context_menu_t set_launcher_one_per_game_context_menu = {
    .get_default_selection = get_launcher_one_per_game_current_selection,
    .list = {
        {.text = "One per game", .action = set_launcher_one_per_game_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Show all", .action = set_launcher_one_per_game_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_quick_launch_current_selection (menu_t *menu) {
    return menu->settings.launcher_quick_launch ? 0 : 1;
}

static component_context_menu_t set_launcher_quick_launch_context_menu = {
    .get_default_selection = get_launcher_quick_launch_current_selection,
    .list = {
        {.text = "On", .action = set_launcher_quick_launch_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_launcher_quick_launch_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_widescreen_current_selection (menu_t *menu) {
    return menu->settings.launcher_widescreen ? 1 : 0;
}

static component_context_menu_t set_launcher_widescreen_context_menu = {
    .get_default_selection = get_launcher_widescreen_current_selection,
    .list = {
        {.text = "4:3", .action = set_launcher_widescreen_type, .arg = (void *)(uintptr_t)(false) },
        {.text = "16:9", .action = set_launcher_widescreen_type, .arg = (void *)(uintptr_t)(true) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_intro_current_selection (menu_t *menu) {
    return menu->settings.launcher_intro ? 0 : 1;
}

static component_context_menu_t set_launcher_intro_context_menu = {
    .get_default_selection = get_launcher_intro_current_selection,
    .list = {
        {.text = "On", .action = set_launcher_intro_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_launcher_intro_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_launcher_start_in_current_selection (menu_t *menu) {
    return menu->settings.launcher_start_in_library ? 1 : 0;
}

static component_context_menu_t set_launcher_start_in_context_menu = {
    .get_default_selection = get_launcher_start_in_current_selection,
    .list = {
        {.text = "Home", .action = set_launcher_start_in_type, .arg = (void *)(uintptr_t)(false) },
        {.text = "Library", .action = set_launcher_start_in_type, .arg = (void *)(uintptr_t)(true) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t launcher_context_menu = { .list = {
    { .text = "Launcher", .submenu = &set_launcher_enabled_context_menu },
    { .text = "Start In", .submenu = &set_launcher_start_in_context_menu },
    { .text = "Startup Intro", .submenu = &set_launcher_intro_context_menu },
    { .text = "Folder Categories", .submenu = &set_launcher_categories_enabled_context_menu },
    { .text = "Duplicates", .submenu = &set_launcher_one_per_game_context_menu },
    { .text = "Library Layout", .submenu = &set_launcher_layout_context_menu },
    { .text = "Tile Size", .submenu = &set_launcher_tile_size_context_menu },
    { .text = "Game Titles", .submenu = &set_launcher_titles_highlight_only_context_menu },
    { .text = "Home: Resume", .submenu = &set_launcher_home_resume_context_menu },
    { .text = "Home: Recent", .submenu = &set_launcher_home_recent_context_menu },
    { .text = "Home: Favorites", .submenu = &set_launcher_home_favorites_context_menu },
    { .text = "Quick Launch", .submenu = &set_launcher_quick_launch_context_menu },
    { .text = "Attract Mode", .submenu = &set_launcher_attract_minutes_context_menu },
    { .text = "Screen", .submenu = &set_launcher_widescreen_context_menu },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_theme_current_selection (menu_t *menu) {
    return theme_current_index();
}

// Labels are filled from theme.c in view_settings_init.
static component_context_menu_t set_theme_context_menu = {
    .get_default_selection = get_theme_current_selection,
    .list = {
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(0) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(1) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(2) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(3) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(4) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(5) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(6) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(7) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(8) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(9) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(10) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(11) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(12) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(13) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(14) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(15) },
        { .text = "", .action = set_theme_type, .arg = (void *)(uintptr_t)(16) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t options_context_menu = { .list = {
    { .text = "Theme", .submenu = &set_theme_context_menu },
    { .text = "Show Hidden Files", .submenu = &set_protected_entries_type_context_menu },
    { .text = "Sound Effects", .submenu = &set_soundfx_enabled_type_context_menu },
    { .text = "Background Music", .submenu = &set_bgm_enabled_type_context_menu },
    { .text = "Music Order", .submenu = &set_bgm_order_context_menu },
    { .text = "Music Volume", .submenu = &set_bgm_volume_context_menu },
    { .text = "Next Track", .action = next_bgm_track },
    { .text = "Use Saves Folder", .submenu = &set_use_saves_folder_type_context_menu },
    { .text = "Show Saves Folder", .submenu = &set_show_saves_folder_type_context_menu },
    { .text = "Show Save Files", .submenu = &set_show_save_files_type_context_menu },
    { .text = "Show Cheat Files", .submenu = &set_show_cheat_files_type_context_menu },
    { .text = "PAL60 Mode", .submenu = &set_pal60_type_context_menu },
    { .text = "Wrap File List", .submenu = &set_wrap_file_list_scrolling_context_menu },
    #ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    { .text = "ROM Loading Bar", .submenu = &set_loading_progress_bar_enabled_context_menu },
#else
    { .text = "Fast Reboot ROM", .submenu = &set_use_rom_fast_reboot_context_menu },
#endif
#ifdef BETA_SETTINGS
    { .text = "Hide ROM Extensions", .submenu = &set_show_browser_file_extensions_context_menu },
    { .text = "Hide ROM Tags", .submenu = &set_show_browser_rom_tags_context_menu },
    { .text = "Rumble Feedback", .submenu = &set_rumble_enabled_type_context_menu },
    // { .text = "Restore Defaults", .action = set_use_default_settings },
#endif
    { .text = "Launcher Settings", .submenu = &launcher_context_menu },
    { .text = "Remove Background", .action = remove_background_image },

    COMPONENT_CONTEXT_MENU_LIST_END,
}};


static void process (menu_t *menu) {
    if (ui_components_context_menu_process(menu, &options_context_menu)) {
        return;
    }

    if (menu->actions.enter) {
        if (show_message_reset_settings) {
            settings_reset_to_defaults();
            menu_show_error(menu, "Reboot N64 to take effect!");
            show_message_reset_settings = false;
        } else {
            ui_components_context_menu_show(&options_context_menu);
        }
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.back) {
        if (show_message_reset_settings) {
            show_message_reset_settings = false;
        } else {
            menu->next_mode = MENU_MODE_BROWSER;
        }
        sound_play_effect(SFX_EXIT);
    } else if (menu->actions.options){
        show_message_reset_settings = true;
    }
}

static const launcher_hint_t settings_hints[] = {
    { LAUNCHER_BUTTON_A, "A", "Change" },
    { LAUNCHER_BUTTON_B, "B", "Back" },
    { LAUNCHER_BUTTON_OTHER, "R", "Reset" },
};

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    // This page's body is the stock layout, so its launcher-style title and hints are drawn in 4:3 too.
    launcher_set_widescreen(false);

    ui_components_background_draw();

    if (menu->settings.launcher_enabled) {
        launcher_draw_page_title("Settings");
    } else {
        ui_components_layout_draw();

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "MENU SETTINGS EDITOR\n"
            "\n"
        );
    }

    ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "\n\n"
        "  Default Directory : %s\n\n"
        "To change the following menu settings, press 'A':\n"
        "     Show Hidden Files : %s\n"
        "     Sound Effects     : %s\n"
        "     Background Music  : %s\n"
        "     Use Saves folder  : %s\n"
        "     Show Saves folder : %s\n"
        "     Show Save files   : %s\n"
        "     Show Cheat files  : %s\n"
        "*    PAL60 Mode        : %s\n"
        "     Wrap File List    : %s\n"
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
        "     Autoload ROM      : %s\n\n"
        "     ROM Loading Bar   : %s\n"
#else
        "     Fast Reboot ROM   : %s\n"
#endif
#ifdef BETA_SETTINGS
        "     Hide ROM Extension: %s\n"
        "     Hide ROM Tags     : %s\n"
        "     Rumble Feedback   : %s\n"
#endif
        "\n\n"
        "* NOTE: This setting may cause the display to go dark. If you get it wrong, you must manually edit the menu/config.ini on the SD card to re-disable it.\n"
        ,
        menu->settings.default_directory,
        format_switch(menu->settings.show_protected_entries),
        format_switch(menu->settings.soundfx_enabled),
        format_switch(menu->settings.bgm_enabled),
        format_switch(menu->settings.use_saves_folder),
        format_switch(menu->settings.show_saves_folder),
        format_switch(menu->settings.show_save_files),
        format_switch(menu->settings.show_cheat_files),
        format_switch(menu->settings.pal60_enabled),
        format_switch(menu->settings.wrap_file_list_scrolling),
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
        format_switch(menu->settings.rom_autoload_enabled),
        format_switch(menu->settings.loading_progress_bar_enabled)
#else
        format_switch(menu->settings.rom_fast_reboot_enabled)
#endif
#ifdef BETA_SETTINGS
        ,
        format_switch(menu->settings.show_browser_file_extensions),
        format_switch(menu->settings.show_browser_rom_tags),
        format_switch(menu->settings.rumble_enabled)
#endif
    );

    if (menu->settings.launcher_enabled) {
        launcher_draw_footer(settings_hints, (int)(sizeof(settings_hints) / sizeof(settings_hints[0])));
    } else {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "A: Change\n"
            "B: Back"
        );

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_RIGHT, VALIGN_TOP,
            "R: Reset settings\n"
            "\n"
        );
    }

    ui_components_context_menu_draw(&options_context_menu);

    if (show_message_reset_settings) {
        ui_components_messagebox_draw(
            "Reset settings?\n\n"
            "A: Yes, B: Back"
        );
    }

    rdpq_detach_show();
}


void view_settings_init (menu_t *menu) {
    for (int i = 0; i < THEME_BUILTIN_COUNT; i++) {
        const theme_t *theme = theme_at(i);
        set_theme_context_menu.list[i].text = theme ? theme->name : "-";
    }
    ui_components_context_menu_init(&options_context_menu);

}

void view_settings_display (menu_t *menu, surface_t *display) {
    process(menu);
    
    draw(menu, display);
}
