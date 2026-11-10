#include <stdbool.h>
#include <string.h>
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

static void set_launcher_cover_style_type (menu_t *menu, void *arg) {
    menu->settings.launcher_cartridge_art = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static int get_launcher_cover_style_current_selection (menu_t *menu) {
    return menu->settings.launcher_cartridge_art ? 1 : 0;
}

static component_context_menu_t set_launcher_cover_style_context_menu = {
    .get_default_selection = get_launcher_cover_style_current_selection,
    .list = {
        { .text = "Box", .action = set_launcher_cover_style_type, .arg = (void *)(uintptr_t)(false) },
        { .text = "Cartridge", .action = set_launcher_cover_style_type, .arg = (void *)(uintptr_t)(true) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

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

static const int launcher_home_style_values[] = { LAUNCHER_HOME_CLASSIC, LAUNCHER_HOME_SPOTLIGHT, LAUNCHER_HOME_CARTRIDGE, LAUNCHER_HOME_MINIMAL, -1 };

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
        {.text = "Cartridge", .action = set_launcher_home_style_type, .arg = (void *)(uintptr_t)(LAUNCHER_HOME_CARTRIDGE) },
        {.text = "Minimal", .action = set_launcher_home_style_type, .arg = (void *)(uintptr_t)(LAUNCHER_HOME_MINIMAL) },
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

/*
 * Tabs across the top (L and R), one setting per row in large text with its value in a column just past the longest
 * name, changed in place with left and right. Rows that open something (an information screen, the folders) show
 * what A does instead of a value.
 */

static menu_mode_t settings_came_from = MENU_MODE_NONE;    /* The launcher screen Settings goes back to */
static bool settings_child_open = false;                     /* A screen opened from here returns here */

static void settings_open_screen (menu_t *menu, void *arg) {
    settings_defer_saves(false);    // Writes what's waiting; other screens save as they always have.
    settings_child_open = true;
    launcher_leave(menu, (menu_mode_t)(uintptr_t)(arg));
}

static void settings_library (menu_t *menu, void *arg) {
    settings_defer_saves(false);
    launcher_settings_do(menu, (launcher_do_t)(uintptr_t)(arg));
}

static void settings_ask_reset (menu_t *menu, void *arg) {
    (void)menu;
    (void)arg;
    show_message_reset_settings = true;
}

typedef struct {
    const char *label;
    component_context_menu_t *choices;      /**< Changed with left and right; its current choice is the value */
    void (*action)(menu_t *menu, void *arg); /**< Or run with A */
    void *arg;
    const char *does;                       /**< For an action: what A does, shown where a value would be */
    const char *note;                       /**< Shown under the list while the row is selected */
} settings_row_t;

typedef struct {
    const char *name;
    const settings_row_t *rows;
    int count;
} settings_tab_t;

#define ROWS(r)     (r), (int)(sizeof(r) / sizeof((r)[0]))

static const settings_row_t look_rows[] = {
    { .label = "Theme", .choices = &set_theme_context_menu },
    { .label = "Screen", .choices = &set_launcher_widescreen_context_menu,
      .note = "16:9 suits a TV set to stretch the picture; games are stretched too" },
    { .label = "Text size", .choices = &set_launcher_large_text_context_menu,
      .note = "Large makes game titles and details easier to read from the couch" },
    { .label = "Game titles", .choices = &set_launcher_titles_highlight_only_context_menu },
    { .label = "Covers", .choices = &set_launcher_cover_style_context_menu,
      .note = "Cartridge shows each game's cartridge label where there is one" },
};

static const settings_row_t home_rows[] = {
    { .label = "Home style", .choices = &set_launcher_home_style_context_menu },
    { .label = "Start in", .choices = &set_launcher_start_in_context_menu },
    { .label = "Resume", .choices = &set_launcher_home_resume_context_menu,
      .note = "The last game played, first on Home" },
    { .label = "Recent games", .choices = &set_launcher_home_recent_context_menu },
    { .label = "Favorites", .choices = &set_launcher_home_favorites_context_menu },
    { .label = "Attract mode", .choices = &set_launcher_attract_minutes_context_menu,
      .note = "After this long without a button press, covers drift across the screen" },
};

static const settings_row_t library_rows[] = {
    { .label = "Layout", .choices = &set_launcher_layout_context_menu },
    { .label = "Tile size", .choices = &set_launcher_tile_size_context_menu },
    { .label = "Folder categories", .choices = &set_launcher_categories_enabled_context_menu,
      .note = "Each folder of games is a category of its own" },
    { .label = "Duplicates", .choices = &set_launcher_one_per_game_context_menu,
      .note = "One per game shows a single copy of a game found in several versions" },
    { .label = "Folders", .action = settings_library, .arg = (void *)(uintptr_t)(LAUNCHER_DO_FOLDERS), .does = "Arrange",
      .note = "Choose which folders show and in what order" },
    { .label = "Hidden games", .action = settings_library, .arg = (void *)(uintptr_t)(LAUNCHER_DO_HIDDEN), .does = "Show" },
    { .label = "Rescan folders", .action = settings_library, .arg = (void *)(uintptr_t)(LAUNCHER_DO_RESCAN), .does = "Rescan",
      .note = "Looks through the game folders again for added or removed games" },
};

static const settings_row_t sound_rows[] = {
    { .label = "Background music", .choices = &set_bgm_enabled_type_context_menu },
    { .label = "Music volume", .choices = &set_bgm_volume_context_menu },
    { .label = "Music order", .choices = &set_bgm_order_context_menu },
    { .label = "Next track", .action = next_bgm_track, .does = "Skip" },
    { .label = "Sound effects", .choices = &set_soundfx_enabled_type_context_menu },
};

static const settings_row_t games_rows[] = {
    { .label = "Quick launch", .choices = &set_launcher_quick_launch_context_menu,
      .note = "A starts the game straight away instead of showing its details first" },
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    { .label = "ROM loading bar", .choices = &set_loading_progress_bar_enabled_context_menu },
#else
    { .label = "Fast reboot", .choices = &set_use_rom_fast_reboot_context_menu,
      .note = "The console's Reset button starts the game again instead of returning here" },
#endif
    { .label = "Use saves folder", .choices = &set_use_saves_folder_type_context_menu,
      .note = "Keeps game saves together in a saves folder beside the games" },
    { .label = "Controller Pak", .action = settings_open_screen, .arg = (void *)(uintptr_t)(MENU_MODE_CONTROLLER_PAKFS),
      .does = "Manage", .note = "Back up, restore and look through Controller Pak saves" },
#ifdef BETA_SETTINGS
    { .label = "Rumble feedback", .choices = &set_rumble_enabled_type_context_menu },
#endif
};

static const settings_row_t system_rows[] = {
    { .label = "Launcher", .choices = &set_launcher_enabled_context_menu,
      .note = "Off shows the original menu and file browser instead" },
    { .label = "PAL60 mode", .choices = &set_pal60_type_context_menu,
      .note = "May turn the screen dark; if so, turn it off in menu/config.ini on the SD card" },
    { .label = "Time and date", .action = settings_open_screen, .arg = (void *)(uintptr_t)(MENU_MODE_RTC), .does = "Set" },
    { .label = "Menu information", .action = settings_open_screen, .arg = (void *)(uintptr_t)(MENU_MODE_CREDITS), .does = "View" },
    { .label = "Flashcart", .action = settings_open_screen, .arg = (void *)(uintptr_t)(MENU_MODE_FLASHCART), .does = "View" },
    { .label = "N64 information", .action = settings_open_screen, .arg = (void *)(uintptr_t)(MENU_MODE_SYSTEM_INFO), .does = "View" },
    { .label = "Reset all settings", .action = settings_ask_reset, .does = "Reset",
      .note = "Puts every setting back as it was at first (asks first)" },
};

static const settings_row_t original_rows[] = {
    { .label = "File browser", .action = settings_library, .arg = (void *)(uintptr_t)(LAUNCHER_DO_FILE_BROWSER), .does = "Open",
      .note = "The original menu's list of every file on the SD card" },
    { .label = "Show hidden files", .choices = &set_protected_entries_type_context_menu },
    { .label = "Show saves folder", .choices = &set_show_saves_folder_type_context_menu },
    { .label = "Show save files", .choices = &set_show_save_files_type_context_menu },
    { .label = "Show cheat files", .choices = &set_show_cheat_files_type_context_menu },
    { .label = "Wrap file list", .choices = &set_wrap_file_list_scrolling_context_menu,
      .note = "Moving past the end of the list goes back to the top" },
    { .label = "Background image", .action = remove_background_image, .does = "Remove",
      .note = "Clears a picture set as the background from the file browser" },
#ifdef BETA_SETTINGS
    { .label = "Hide ROM extensions", .choices = &set_show_browser_file_extensions_context_menu },
    { .label = "Hide ROM tags", .choices = &set_show_browser_rom_tags_context_menu },
#endif
};

static const settings_tab_t settings_tabs[] = {
    { "Look", ROWS(look_rows) },
    { "Home", ROWS(home_rows) },
    { "Library", ROWS(library_rows) },
    { "Sound", ROWS(sound_rows) },
    { "Games", ROWS(games_rows) },
    { "System", ROWS(system_rows) },
    { "Original menu", ROWS(original_rows) },
};

#define SETTINGS_TAB_COUNT      ((int)(sizeof(settings_tabs) / sizeof(settings_tabs[0])))
#define SETTINGS_TABS_Y         (LAUNCHER_HEADER_Y - 6)
#define SETTINGS_TABS_H         (30)
#define SETTINGS_UNDERLINE_Y    (SETTINGS_TABS_Y + SETTINGS_TABS_H + 1)
#define SETTINGS_SLASH_GAP      (14)    /* Either side of the slash between tabs */
#define SETTINGS_DIVIDER_Y      (SETTINGS_UNDERLINE_Y + 12)
#define SETTINGS_TOP            (SETTINGS_DIVIDER_Y + 14)
#define SETTINGS_ROW_H          (40)
#define SETTINGS_ROW_BOX        (36)    /* The highlight, a little shorter than a row */
#define SETTINGS_NOTE_Y         (LAUNCHER_FOOTER_Y - 32)
#define SETTINGS_BOTTOM         (SETTINGS_NOTE_Y - 6)
#define SETTINGS_LABEL_X        (LAUNCHER_X0 + 22)
#define SETTINGS_VALUE_GAP      (44)    /* From the longest name to the values */
#define SETTINGS_ARROW          (12)
#define SETTINGS_ARROW_GAP      (22)    /* Arrow centre to the value's edge */
#define SETTINGS_SAVE_IDLE_MS   (1500)  /* A waiting save is written once the buttons have been still this long */
#define SETTINGS_TAB_SLIDE      (36)

static int settings_tab = 0;
static int settings_selected_in[SETTINGS_TAB_COUNT];   /* Each tab remembers its row */
static float settings_scroll_pos = 0.0f;
static int settings_scroll = 0;
static float settings_highlight_y = -1.0f;
static float settings_underline_x = -1.0f, settings_underline_w = 0.0f;
static float settings_tabs_shift = 0.0f;
static float settings_slide = 0.0f;                     /* Rows slide in from the side a new tab is on */
static uint64_t settings_last_press_ms = 0;

static const settings_tab_t *current_tab (void) {
    return &settings_tabs[settings_tab];
}

static const settings_row_t *current_row (void) {
    const settings_tab_t *tab = current_tab();
    int i = settings_selected_in[settings_tab];
    return (i >= 0 && i < tab->count) ? &tab->rows[i] : NULL;
}

/** @brief A choice the row can be set to (theme slots without a theme are skipped). */
static bool choice_usable (const component_context_menu_t *c, int i) {
    return c->list[i].action && c->list[i].text[0] && strcmp(c->list[i].text, "-") != 0;
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

/** @brief An On/Off row: A flips it (other values change only with left and right). */
static bool settings_is_toggle (const settings_row_t *row) {
    component_context_menu_t *c = row->choices;
    if (!c || !c->list[0].text || !c->list[1].text || c->list[2].text) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        if (strcmp(c->list[i].text, "On") != 0 && strcmp(c->list[i].text, "Off") != 0) {
            return false;
        }
    }
    return true;
}

/** @brief Step a row's value to the next (or previous) choice, wrapping round, and apply it straight away. */
static void settings_cycle (menu_t *menu, const settings_row_t *row, int step) {
    component_context_menu_t *c = row->choices;
    int n = 0;
    while (c->list[n].text) {
        n++;
    }
    if (n == 0) {
        return;
    }
    int current = c->get_default_selection ? c->get_default_selection(menu) : 0;
    for (int k = 1; k <= n; k++) {
        int i = (((current + step * k) % n) + n) % n;
        if (choice_usable(c, i)) {
            c->list[i].action(menu, c->list[i].arg);
            sound_play_effect(SFX_SETTING);
            return;
        }
    }
}

static void settings_switch_tab (int step) {
    int next = settings_tab + step;
    if (next < 0 || next >= SETTINGS_TAB_COUNT) {
        return;
    }
    settings_tab = next;
    settings_scroll = 0;
    settings_scroll_pos = 0.0f;
    settings_highlight_y = -1.0f;
    settings_slide = (float)(step * SETTINGS_TAB_SLIDE);
    sound_play_effect(SFX_TICK);
}

static void settings_move (int step) {
    const settings_tab_t *tab = current_tab();
    int next = settings_selected_in[settings_tab] + step;
    if (next >= 0 && next < tab->count) {
        settings_selected_in[settings_tab] = next;
        sound_play_effect(SFX_TICK);
    }
}

static bool settings_entry_widescreen = false;     /* Screen when the page opened */

static void settings_go_back (menu_t *menu) {
    settings_defer_saves(false);
    if (menu->settings.launcher_widescreen != settings_entry_widescreen) {
        launcher_covers_reset();    // Covers stay loaded through Settings; a new screen shape takes other packs.
    }
    sound_play_effect(SFX_EXIT);
    menu->launcher_return_mode = settings_came_from;
    launcher_transition(MENU_MODE_SETTINGS_EDITOR, menu->launcher_return_mode);
    menu->next_mode = MENU_MODE_BROWSER;    // Back to the launcher screen it was opened from.
}

static void launcher_settings_process (menu_t *menu) {
    if (show_message_reset_settings) {
        if (menu->actions.enter) {
            settings_defer_saves(false);
            settings_reset_to_defaults();
            show_message_reset_settings = false;
            sound_play_effect(SFX_SETTING);
            menu_show_error(menu, "Settings reset. Turn the N64 off and on again to finish.");
        } else if (menu->actions.back) {
            show_message_reset_settings = false;
            sound_play_effect(SFX_EXIT);
        }
        return;
    }

    bool pressed = menu->actions.go_up || menu->actions.go_down || menu->actions.go_left || menu->actions.go_right ||
        menu->actions.enter || menu->actions.back || menu->actions.shoulder_l || menu->actions.shoulder_r;
    if (pressed) {
        settings_last_press_ms = get_ticks_ms();
    } else if (settings_save_waiting() && get_ticks_ms() - settings_last_press_ms > SETTINGS_SAVE_IDLE_MS) {
        settings_flush();
    }

    const settings_row_t *row = current_row();
    if (menu->actions.shoulder_l) {
        settings_switch_tab(-1);
    } else if (menu->actions.shoulder_r) {
        settings_switch_tab(1);
    } else if (menu->actions.go_up) {
        settings_move(-1);
    } else if (menu->actions.go_down) {
        settings_move(1);
    } else if ((menu->actions.go_left || menu->actions.go_right) && row && row->choices) {
        settings_cycle(menu, row, menu->actions.go_left ? -1 : 1);
    } else if (menu->actions.enter && row) {
        if (row->choices) {
            if (settings_is_toggle(row)) {
                settings_cycle(menu, row, 1);
            }
        } else if (row->action) {
            sound_play_effect(SFX_SETTING);
            row->action(menu, row->arg);
        }
    } else if (menu->actions.back) {
        settings_go_back(menu);
    }
}

/** @brief The tabs: names with slashes between them, the current one bright with the accent line under it. */
static void settings_draw_tabs (void) {
    int widths[SETTINGS_TAB_COUNT];
    int slash_w = launcher_text_sized_width(LAUNCHER_SIZE_LARGE, "/");
    int total = 0, active_x = 0;
    for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
        widths[i] = launcher_text_sized_width(LAUNCHER_SIZE_LARGE, settings_tabs[i].name);
        if (i == settings_tab) {
            active_x = total;
        }
        total += widths[i] + ((i + 1 < SETTINGS_TAB_COUNT) ? SETTINGS_SLASH_GAP * 2 + slash_w : 0);
    }
    // Too wide for the screen: the row shifts so the current tab stays in view.
    float shift = 0.0f;
    if (total > LAUNCHER_WIDTH) {
        int want = active_x + widths[settings_tab] / 2 - LAUNCHER_WIDTH / 2;
        want = (want < 0) ? 0 : (want > total - LAUNCHER_WIDTH) ? total - LAUNCHER_WIDTH : want;
        shift = (float)want;
    }
    settings_tabs_shift = launcher_ease(settings_tabs_shift, shift);
    int x = LAUNCHER_X0 - (int)(settings_tabs_shift + 0.5f);
    for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
        bool visible = x >= LAUNCHER_X0 - 2 && x + widths[i] <= LAUNCHER_X1 + 2;
        if (visible) {
            launcher_text_sized(LAUNCHER_SIZE_LARGE, x, SETTINGS_TABS_Y, 0, SETTINGS_TABS_H, ALIGN_LEFT,
                (i == settings_tab) ? STL_DEFAULT : STL_GRAY, settings_tabs[i].name);
        }
        x += widths[i];
        if (i + 1 < SETTINGS_TAB_COUNT) {
            x += SETTINGS_SLASH_GAP;
            if (x >= LAUNCHER_X0 && x + slash_w <= LAUNCHER_X1) {
                launcher_text_sized(LAUNCHER_SIZE_LARGE, x, SETTINGS_TABS_Y, 0, SETTINGS_TABS_H, ALIGN_LEFT, STL_GRAY, "/");
            }
            x += slash_w + SETTINGS_SLASH_GAP;
        }
    }
    // The accent line glides to the new tab and takes its width.
    float target_x = (float)(LAUNCHER_X0 + active_x) - settings_tabs_shift;
    if (settings_underline_x < 0.0f) {
        settings_underline_x = target_x;
        settings_underline_w = (float)widths[settings_tab];
    }
    settings_underline_x = launcher_ease(settings_underline_x, target_x);
    settings_underline_w = launcher_ease(settings_underline_w, (float)widths[settings_tab]);
    int ux = (int)(settings_underline_x + 0.5f);
    launcher_fill_rounded(ux, SETTINGS_UNDERLINE_Y, ux + (int)(settings_underline_w + 0.5f), SETTINGS_UNDERLINE_Y + 3, 1,
        theme_current()->accent2);
    ui_components_box_draw(launcher_px(LAUNCHER_X0), SETTINGS_DIVIDER_Y, launcher_px(LAUNCHER_X1), SETTINGS_DIVIDER_Y + 1,
        theme_current()->line);
}

static void launcher_settings_draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);
    launcher_set_widescreen(menu->settings.launcher_widescreen);

    launcher_draw_background();
    settings_draw_tabs();
    launcher_transition_content_begin(SETTINGS_DIVIDER_Y + 2);

    const settings_tab_t *tab = current_tab();
    int selected = settings_selected_in[settings_tab];

    // Keep the selected row in view; the list glides there.
    int view_h = SETTINGS_BOTTOM - SETTINGS_TOP;
    int sel_y = selected * SETTINGS_ROW_H;
    if (sel_y < settings_scroll) {
        settings_scroll = sel_y;
    }
    if (sel_y + SETTINGS_ROW_H > settings_scroll + view_h) {
        settings_scroll = sel_y + SETTINGS_ROW_H - view_h;
    }
    settings_scroll_pos = launcher_ease(settings_scroll_pos, (float)settings_scroll);
    int scroll = (int)(settings_scroll_pos + 0.5f);
    settings_slide = launcher_ease(settings_slide, 0.0f);
    int dx = (int)(settings_slide + (settings_slide < 0 ? -0.5f : 0.5f));

    // Values line up just past this tab's longest name.
    int longest = 0;
    for (int i = 0; i < tab->count; i++) {
        int w = launcher_text_sized_width(LAUNCHER_SIZE_TITLE, tab->rows[i].label);
        longest = (w > longest) ? w : longest;
    }
    int value_x = SETTINGS_LABEL_X + longest + SETTINGS_VALUE_GAP + dx;

    // Left edge at 0, as everywhere in the launcher: COPY mode hangs the RDP with any other.
    rdpq_set_scissor(0, SETTINGS_TOP - 4, display_get_width(), SETTINGS_BOTTOM + 4);

    // The highlight glides from row to row, with the accent bar on its left.
    float target = (float)(SETTINGS_TOP + sel_y - scroll);
    settings_highlight_y = (settings_highlight_y < 0.0f) ? target : launcher_ease(settings_highlight_y, target);
    int hy = (int)(settings_highlight_y + 0.5f);
    int box_y = hy + (SETTINGS_ROW_H - SETTINGS_ROW_BOX) / 2;
    launcher_fill_rounded(LAUNCHER_X0 + dx, box_y, LAUNCHER_X1 + dx, box_y + SETTINGS_ROW_BOX, 6, theme_current()->highlight);
    launcher_fill_rounded(LAUNCHER_X0 + dx, box_y + 4, LAUNCHER_X0 + 4 + dx, box_y + SETTINGS_ROW_BOX - 4, 2,
        theme_current()->accent2);

    for (int i = 0; i < tab->count; i++) {
        const settings_row_t *row = &tab->rows[i];
        int y = SETTINGS_TOP + i * SETTINGS_ROW_H - scroll;
        if (y + SETTINGS_ROW_H < SETTINGS_TOP - 4 || y > SETTINGS_BOTTOM) {
            continue;
        }
        bool on = (i == selected);
        launcher_text_sized(LAUNCHER_SIZE_TITLE, SETTINGS_LABEL_X + dx, y, 0, SETTINGS_ROW_H, ALIGN_LEFT,
            on ? STL_DEFAULT : STL_GRAY, row->label);
        const char *value = row->choices ? settings_value(menu, row) : row->does;
        if (!value) {
            continue;
        }
        if (row->choices && on) {
            // Where the value is in a long list ("3 of 17"), at the row's right end.
            int n = 0;
            while (row->choices->list[n].text) {
                n++;
            }
            if (n > 4 && row->choices->get_default_selection) {
                char where[16];
                snprintf(where, sizeof(where), "%d of %d", row->choices->get_default_selection(menu) + 1, n);
                launcher_text_sized(LAUNCHER_SIZE_BODY, LAUNCHER_X1 - 120, y, 100, SETTINGS_ROW_H, ALIGN_RIGHT, STL_GRAY, where);
            }
            // Arrows either side: left and right change it.
            int vx = value_x + SETTINGS_ARROW_GAP;
            int w = launcher_text_sized(LAUNCHER_SIZE_TITLE, vx, y, LAUNCHER_X1 - (n > 4 ? 140 : 40) - vx, SETTINGS_ROW_H, ALIGN_LEFT,
                STL_ACCENT, value);
            int cy = y + SETTINGS_ROW_H / 2;
            launcher_draw_arrow(value_x + 6, cy, SETTINGS_ARROW, false, theme_current()->accent);
            launcher_draw_arrow(vx + w + SETTINGS_ARROW_GAP - 6, cy, SETTINGS_ARROW, true, theme_current()->accent);
        } else {
            launcher_text_sized(LAUNCHER_SIZE_TITLE, value_x + SETTINGS_ARROW_GAP, y, LAUNCHER_X1 - 40 - value_x, SETTINGS_ROW_H,
                ALIGN_LEFT, on ? STL_ACCENT : STL_GRAY, value);
        }
    }
    rdpq_set_scissor(0, 0, display_get_width(), display_get_height());

    const settings_row_t *row = current_row();
    if (row && row->note) {
        launcher_text_sized(LAUNCHER_SIZE_BODY, SETTINGS_LABEL_X + dx, SETTINGS_NOTE_Y, LAUNCHER_X1 - SETTINGS_LABEL_X - 8,
            24, ALIGN_LEFT, STL_GRAY, row->note);
    }
    launcher_draw_now_playing(LAUNCHER_X0, LAUNCHER_FOOTER_Y + 12, LAUNCHER_WIDTH / 2, ALIGN_LEFT);

    // Each hint keeps its button beside it: left/right for a value, A for On/Off or for what a row does.
    static const launcher_hint_t value_hints[] = {
        { LAUNCHER_BUTTON_OTHER, "L R", "Tabs" },
        { LAUNCHER_BUTTON_LEFT_RIGHT, "", "Change" },
        { LAUNCHER_BUTTON_B, "B", "Back" },
    };
    static const launcher_hint_t toggle_hints[] = {
        { LAUNCHER_BUTTON_OTHER, "L R", "Tabs" },
        { LAUNCHER_BUTTON_A, "A", "On / Off" },
        { LAUNCHER_BUTTON_B, "B", "Back" },
    };
    launcher_hint_t hints[3];
    memcpy(hints, (row && row->choices && settings_is_toggle(row)) ? toggle_hints : value_hints, sizeof(hints));
    if (row && !row->choices) {
        hints[1].kind = LAUNCHER_BUTTON_A;
        hints[1].button = "A";
        hints[1].label = row->does ? row->does : "Select";
    }
    launcher_draw_footer(hints, 3);

    if (show_message_reset_settings) {
        static const launcher_hint_t answers[] = {
            { LAUNCHER_BUTTON_A, "A", "Reset" },
            { LAUNCHER_BUTTON_B, "B", "Keep them" },
        };
        launcher_draw_dialog("Reset all settings?", "Every setting goes back to how it was at first. Your games, saves "
            "and favorites stay.", answers, 2);
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
            menu_show_error(menu, "Settings reset. Turn the N64 off and on again to finish.");
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
    settings_highlight_y = -1.0f;
    settings_underline_x = -1.0f;
    settings_slide = 0.0f;
    settings_scroll_pos = (float)settings_scroll;
    show_message_reset_settings = false;
    // Back from a screen opened here: Settings still returns to where it was opened from.
    if (settings_child_open) {
        menu->launcher_return_mode = settings_came_from;
        settings_child_open = false;
    } else {
        settings_came_from = menu->launcher_return_mode;
    }
    settings_last_press_ms = get_ticks_ms();
    settings_entry_widescreen = menu->settings.launcher_widescreen;
    settings_defer_saves(menu->settings.launcher_enabled);
}

void view_settings_display (menu_t *menu, surface_t *display) {
    if (menu->settings.launcher_enabled) {
        launcher_settings_process(menu);
        launcher_settings_draw(menu, display);
        return;
    }
    settings_defer_saves(false);    // The launcher was turned off here: the original page saves as it goes.
    process(menu);
    
    draw(menu, display);
}
