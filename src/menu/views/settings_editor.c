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

static void set_launcher_large_text_type (menu_t *menu, void *arg) {
    menu->settings.launcher_large_text = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static int get_launcher_large_text_current_selection (menu_t *menu) {
    return menu->settings.launcher_large_text ? 1 : 0;
}

static component_context_menu_t set_launcher_large_text_context_menu = {
    .get_default_selection = get_launcher_large_text_current_selection,
    .list = {
        {.text = "Normal", .action = set_launcher_large_text_type, .arg = (void *)(uintptr_t)(false) },
        {.text = "Large", .action = set_launcher_large_text_type, .arg = (void *)(uintptr_t)(true) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

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

static const int launcher_home_style_values[] = { LAUNCHER_HOME_CLASSIC, LAUNCHER_HOME_SPOTLIGHT, LAUNCHER_HOME_MINIMAL, LAUNCHER_HOME_TILES, -1 };

static void set_launcher_home_style_type (menu_t *menu, void *arg) {
    menu->settings.launcher_home_style = (int)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static int get_launcher_home_style_current_selection (menu_t *menu) {
    for (int i = 0; launcher_home_style_values[i] >= 0; i++) {
        if (launcher_home_style_values[i] == menu->settings.launcher_home_style) {
            return i;
        }
    }
    return 0;
}

static component_context_menu_t set_launcher_home_style_context_menu = {
    .get_default_selection = get_launcher_home_style_current_selection,
    .list = {
        {.text = "Classic", .action = set_launcher_home_style_type, .arg = (void *)(uintptr_t)(LAUNCHER_HOME_CLASSIC) },
        {.text = "Spotlight", .action = set_launcher_home_style_type, .arg = (void *)(uintptr_t)(LAUNCHER_HOME_SPOTLIGHT) },
        {.text = "Minimal", .action = set_launcher_home_style_type, .arg = (void *)(uintptr_t)(LAUNCHER_HOME_MINIMAL) },
        {.text = "Tiles", .action = set_launcher_home_style_type, .arg = (void *)(uintptr_t)(LAUNCHER_HOME_TILES) },
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
        {.text = "Shelf", .action = set_launcher_layout_type, .arg = (void *)(uintptr_t)(LAUNCHER_LAYOUT_SHELF) },
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


/* ---- The launcher's Settings page (the stock page above is kept for when the launcher is off) ---- */

typedef struct {
    const char *section;                    /**< Starts a group ("Look"); the row's other fields are unused */
    const char *label;
    component_context_menu_t *choices;      /**< Opened with A; its current choice is shown as the value */
    void (*action)(menu_t *menu, void *arg); /**< Or run straight away */
    const char *note;                       /**< Shown under the list while the row is selected */
} settings_row_t;

static const settings_row_t settings_rows[] = {
    { .section = "Look" },
    { .label = "Theme", .choices = &set_theme_context_menu },
    { .label = "Screen", .choices = &set_launcher_widescreen_context_menu,
      .note = "16:9 suits a TV set to stretch the picture; games are stretched too" },
    { .label = "Home style", .choices = &set_launcher_home_style_context_menu },
    { .label = "Library layout", .choices = &set_launcher_layout_context_menu },
    { .label = "Tile size", .choices = &set_launcher_tile_size_context_menu },
    { .label = "Game titles", .choices = &set_launcher_titles_highlight_only_context_menu },
    { .label = "Text size", .choices = &set_launcher_large_text_context_menu,
      .note = "Large makes game titles and details easier to read from the couch" },
    { .label = "Remove background image", .action = remove_background_image },
    { .section = "Launcher" },
    { .label = "Launcher", .choices = &set_launcher_enabled_context_menu,
      .note = "Off shows the original menu and file browser" },
    { .label = "Start in", .choices = &set_launcher_start_in_context_menu },
    { .label = "Folder categories", .choices = &set_launcher_categories_enabled_context_menu },
    { .label = "Duplicates", .choices = &set_launcher_one_per_game_context_menu },
    { .label = "Quick launch", .choices = &set_launcher_quick_launch_context_menu },
    { .label = "Attract mode", .choices = &set_launcher_attract_minutes_context_menu },
    { .label = "Home: Resume", .choices = &set_launcher_home_resume_context_menu },
    { .label = "Home: Recent", .choices = &set_launcher_home_recent_context_menu },
    { .label = "Home: Favorites", .choices = &set_launcher_home_favorites_context_menu },
    { .section = "Sound" },
    { .label = "Sound effects", .choices = &set_soundfx_enabled_type_context_menu },
    { .label = "Background music", .choices = &set_bgm_enabled_type_context_menu },
    { .label = "Music order", .choices = &set_bgm_order_context_menu },
    { .label = "Music volume", .choices = &set_bgm_volume_context_menu },
    { .label = "Next track", .action = next_bgm_track },
    { .section = "Files and saves" },
    { .label = "Show hidden files", .choices = &set_protected_entries_type_context_menu },
    { .label = "Use saves folder", .choices = &set_use_saves_folder_type_context_menu },
    { .label = "Show saves folder", .choices = &set_show_saves_folder_type_context_menu },
    { .label = "Show save files", .choices = &set_show_save_files_type_context_menu },
    { .label = "Show cheat files", .choices = &set_show_cheat_files_type_context_menu },
    { .label = "Wrap file list", .choices = &set_wrap_file_list_scrolling_context_menu },
    { .section = "System" },
    { .label = "PAL60 mode", .choices = &set_pal60_type_context_menu,
      .note = "May turn the screen dark; if so, turn it off in menu/config.ini on the SD card" },
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    { .label = "ROM loading bar", .choices = &set_loading_progress_bar_enabled_context_menu },
#else
    { .label = "Fast reboot ROM", .choices = &set_use_rom_fast_reboot_context_menu,
      .note = "The console's Reset button starts the game again instead of returning here" },
#endif
#ifdef BETA_SETTINGS
    { .label = "Hide ROM extensions", .choices = &set_show_browser_file_extensions_context_menu },
    { .label = "Hide ROM tags", .choices = &set_show_browser_rom_tags_context_menu },
    { .label = "Rumble feedback", .choices = &set_rumble_enabled_type_context_menu },
#endif
};

#define SETTINGS_ROW_COUNT      ((int)(sizeof(settings_rows) / sizeof(settings_rows[0])))
#define SETTINGS_TOP            (LAUNCHER_CONTENT_Y - 18)
#define SETTINGS_BOTTOM         (LAUNCHER_CONTENT_Y1 - 22)  /* Room below for a row's note */
#define SETTINGS_ROW_H          (26)
#define SETTINGS_SECTION_H      (34)
#define SETTINGS_NOTE_Y         (LAUNCHER_CONTENT_Y1 - 12)

static int settings_selected = 1;               /* Row index (never a section) */
static float settings_scroll_pos = 0.0f;        /* Drawn scroll, gliding toward settings_scroll */
static int settings_scroll = 0;
static float settings_highlight_y = -1.0f;      /* Drawn highlight, gliding to the selected row */
static component_context_menu_t *settings_open = NULL;   /* A row's choices, while open */

/** @brief Top of a row within the list (before scrolling). */
static int settings_row_y (int index) {
    int y = 0;
    for (int i = 0; i < index; i++) {
        y += settings_rows[i].section ? SETTINGS_SECTION_H : SETTINGS_ROW_H;
    }
    return y;
}

/** @brief A row's current value: the text of its choices' current selection. */
static const char *settings_value (menu_t *menu, const settings_row_t *row) {
    if (!row->choices || !row->choices->get_default_selection) {
        return NULL;
    }
    int index = row->choices->get_default_selection(menu);
    for (int i = 0; row->choices->list[i].text; i++) {
        if (i == index) {
            return row->choices->list[i].text;
        }
    }
    return NULL;
}

static void settings_move (int step) {
    int next = settings_selected;
    do {
        next += step;
    } while (next >= 0 && next < SETTINGS_ROW_COUNT && settings_rows[next].section);
    if (next >= 0 && next < SETTINGS_ROW_COUNT) {
        settings_selected = next;
        sound_play_effect(SFX_TICK);
    }
}

static void launcher_settings_process (menu_t *menu) {
    if (settings_open) {
        ui_components_context_menu_process(menu, settings_open);
        if (settings_open->row_selected < 0) {
            settings_open = NULL;
        }
        return;
    }
    if (show_message_reset_settings) {
        if (menu->actions.enter) {
            settings_reset_to_defaults();
            show_message_reset_settings = false;
            sound_play_effect(SFX_SETTING);
            menu_show_error(menu, "Reboot N64 to take effect!");
        } else if (menu->actions.back) {
            show_message_reset_settings = false;
            sound_play_effect(SFX_EXIT);
        }
        return;
    }

    if (menu->actions.go_up) {
        settings_move(-1);
    } else if (menu->actions.go_down) {
        settings_move(1);
    } else if (menu->actions.enter) {
        const settings_row_t *row = &settings_rows[settings_selected];
        if (row->choices) {
            ui_components_context_menu_init(row->choices);
            ui_components_context_menu_show(row->choices);
            if (row->choices->get_default_selection) {
                row->choices->row_selected = row->choices->get_default_selection(menu);
            }
            settings_open = row->choices;
            sound_play_effect(SFX_POP);
        } else if (row->action) {
            row->action(menu, NULL);
            sound_play_effect(SFX_SETTING);
        }
    } else if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        launcher_transition(MENU_MODE_SETTINGS_EDITOR, menu->launcher_return_mode);
        menu->next_mode = MENU_MODE_BROWSER;    // Back to the launcher screen it was opened from.
    } else if (menu->actions.options) {
        show_message_reset_settings = true;
        sound_play_effect(SFX_POP);
    }
}

static void launcher_settings_draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);
    launcher_set_widescreen(menu->settings.launcher_widescreen);

    launcher_draw_background();
    launcher_draw_page_title("Settings");
    launcher_transition_content_begin(SETTINGS_TOP - 8);

    // Keep the selected row in view; the list glides there.
    int view_h = SETTINGS_BOTTOM - SETTINGS_TOP;
    int sel_y = settings_row_y(settings_selected);
    int sel_top = sel_y;
    if (settings_selected > 0 && settings_rows[settings_selected - 1].section) {
        sel_top -= SETTINGS_SECTION_H;      // Show a group's heading with its first row.
    }
    if (sel_top < settings_scroll) {
        settings_scroll = sel_top;
    }
    if (sel_y + SETTINGS_ROW_H > settings_scroll + view_h) {
        settings_scroll = sel_y + SETTINGS_ROW_H - view_h;
    }
    settings_scroll_pos = launcher_ease(settings_scroll_pos, (float)settings_scroll);
    int scroll = (int)(settings_scroll_pos + 0.5f);

    // Left edge at 0, as everywhere in the launcher: COPY mode hangs the RDP with any other.
    rdpq_set_scissor(0, SETTINGS_TOP - 4, display_get_width(), SETTINGS_BOTTOM + 4);

    // The highlight glides from row to row.
    float target = (float)(SETTINGS_TOP + sel_y - scroll);
    settings_highlight_y = (settings_highlight_y < 0.0f) ? target : launcher_ease(settings_highlight_y, target);
    int hy = (int)(settings_highlight_y + 0.5f);
    launcher_fill_rounded(LAUNCHER_X0 + 8, hy - 3, LAUNCHER_X1 - 8, hy + SETTINGS_ROW_H - 3, 6, theme_current()->highlight);

    int y = SETTINGS_TOP - scroll;
    for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
        const settings_row_t *row = &settings_rows[i];
        int h = row->section ? SETTINGS_SECTION_H : SETTINGS_ROW_H;
        if (y + h >= SETTINGS_TOP - 4 && y <= SETTINGS_BOTTOM) {
            if (row->section) {
                launcher_name(LAUNCHER_X0 + 8, y + 8, LAUNCHER_WIDTH - 16, ALIGN_LEFT, STL_ACCENT, row->section);
            } else {
                bool selected = (i == settings_selected);
                launcher_name(LAUNCHER_X0 + 20, y, LAUNCHER_WIDTH / 2, ALIGN_LEFT, selected ? STL_DEFAULT : STL_GRAY, row->label);
                const char *value = settings_value(menu, row);
                if (value) {
                    launcher_name(LAUNCHER_X0 + LAUNCHER_WIDTH / 2, y, LAUNCHER_WIDTH / 2 - 20, ALIGN_RIGHT,
                        selected ? STL_ACCENT : STL_GRAY, value);
                }
            }
        }
        y += h;
    }
    rdpq_set_scissor(0, 0, display_get_width(), display_get_height());

    const char *note = settings_rows[settings_selected].note;
    if (note) {
        launcher_small(LAUNCHER_X0 + 20, SETTINGS_NOTE_Y, LAUNCHER_WIDTH - 40, ALIGN_LEFT, STL_GRAY, note);
    }

    static const launcher_hint_t hints[] = {
        { LAUNCHER_BUTTON_A, "A", "Change" },
        { LAUNCHER_BUTTON_B, "B", "Back" },
        { LAUNCHER_BUTTON_OTHER, "R", "Reset" },
    };
    launcher_draw_footer(hints, (int)(sizeof(hints) / sizeof(hints[0])));

    if (settings_open) {
        ui_components_context_menu_draw(settings_open);
    }
    if (show_message_reset_settings) {
        ui_components_messagebox_draw(
            "Reset all settings?\n\n"
            "A: Yes, B: Back"
        );
    }

    rdpq_detach_show();
}


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
    settings_open = NULL;
    settings_highlight_y = -1.0f;
    settings_scroll_pos = (float)settings_scroll;
    show_message_reset_settings = false;
}

void view_settings_display (menu_t *menu, surface_t *display) {
    if (menu->settings.launcher_enabled) {
        launcher_settings_process(menu);
        launcher_settings_draw(menu, display);
        return;
    }
    process(menu);
    
    draw(menu, display);
}
