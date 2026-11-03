#include <libdragon.h>
#include "ini_parser.h"

#include "settings.h"
#include "utils/fs.h"


static char *settings_path = NULL;


static settings_t init = {
    .schema_revision = 1,
    .first_run = true,
    .pal60_enabled = false,
    .force_progressive_scan = false,
    .show_protected_entries = false,
    .default_directory = "/",
    .use_saves_folder = true,
    .show_saves_folder = false,
    .show_save_files = false,
    .show_cheat_files = false,
    .show_rom_configuration_files = false,
    .soundfx_enabled = false,
    .bgm_enabled = false,
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    .rom_autoload_enabled = false,
    .rom_autoload_path = "",
    .rom_autoload_filename = "",
    .loading_progress_bar_enabled = true,
#else
    .rom_fast_reboot_enabled = false,
#endif    
    /* Beta feature flags (should always init to default) */
    .show_browser_file_extensions = true,
    .show_browser_rom_tags = true,
    .wrap_file_list_scrolling = false,
    .rumble_enabled = false,
    .theme = "Castle Sky",
    .bgm_order = 0,
    .bgm_volume = 1,
    /* Launcher */
    .launcher_enabled = true,
    .launcher_library_root = "/",
    .launcher_categories_enabled = true,
    .launcher_category = "USA",
    .launcher_layout = LAUNCHER_LAYOUT_GRID,
    .launcher_tile_size = LAUNCHER_TILE_MEDIUM,
    .launcher_sort = LAUNCHER_SORT_TITLE,
    .launcher_widescreen = false,
    .launcher_quick_launch = false,
    .launcher_start_in_library = false,
    .launcher_attract_minutes = 5,
    .launcher_cartridge_art = false,
    .launcher_one_per_game = true,
    .launcher_titles_highlight_only = true,
    .launcher_home_resume = true,
    .launcher_home_recent = true,
    .launcher_home_favorites = true,
};


void settings_init (char *path) {
    if (settings_path) {
        free(settings_path);
    }
    settings_path = strdup(path);
}

void settings_load (settings_t *settings) {
    if (!file_exists(settings_path)) {
        settings_save(&init);
    }

    ini_t *ini = ini_try_load(settings_path);

    settings->schema_revision = ini_get_int(ini, "menu", "schema_revision", init.schema_revision);
    settings->first_run = ini_get_bool(ini, "menu", "first_run", init.first_run);
    settings->pal60_enabled = ini_get_bool(ini, "menu", "pal60", init.pal60_enabled);
    settings->force_progressive_scan = ini_get_bool(ini, "menu", "force_progressive_scan", init.force_progressive_scan);
    settings->show_protected_entries = ini_get_bool(ini, "menu", "show_protected_entries", init.show_protected_entries);
    free(settings->default_directory);
    settings->default_directory = strdup(ini_get_string(ini, "menu", "default_directory", init.default_directory));
    settings->use_saves_folder = ini_get_bool(ini, "menu", "use_saves_folder", init.use_saves_folder);
    settings->show_saves_folder = ini_get_bool(ini, "menu", "show_saves_folder", init.show_saves_folder);
    settings->show_save_files = ini_get_bool(ini, "menu", "show_save_files", init.show_save_files);
    settings->show_cheat_files = ini_get_bool(ini, "menu", "show_cheat_files", init.show_cheat_files);
    settings->show_rom_configuration_files = ini_get_bool(ini, "menu", "show_rom_configuration_files", init.show_rom_configuration_files);
    settings->soundfx_enabled = ini_get_bool(ini, "menu", "soundfx_enabled", init.soundfx_enabled);
    settings->bgm_enabled = ini_get_bool(ini, "menu", "bgm_enabled", init.bgm_enabled);
    free(settings->theme);
    settings->theme = strdup(ini_get_string(ini, "menu", "theme", init.theme));
    settings->bgm_order = ini_get_int(ini, "menu", "bgm_order", init.bgm_order);
    if (settings->bgm_order < 0 || settings->bgm_order > 2) {
        settings->bgm_order = init.bgm_order;
    }
    settings->bgm_volume = ini_get_int(ini, "menu", "bgm_volume", init.bgm_volume);
    if (settings->bgm_volume < 0 || settings->bgm_volume > 2) {
        settings->bgm_volume = init.bgm_volume;
    }

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    settings->rom_autoload_enabled = ini_get_bool(ini, "menu", "autoload_rom_enabled", init.rom_autoload_enabled);
    free(settings->rom_autoload_path);
    settings->rom_autoload_path = strdup(ini_get_string(ini, "autoload", "rom_path", init.rom_autoload_path));
    free(settings->rom_autoload_filename);
    settings->rom_autoload_filename = strdup(ini_get_string(ini, "autoload", "rom_filename", init.rom_autoload_filename));
    settings->loading_progress_bar_enabled = ini_get_bool(ini, "menu", "loading_progress_bar_enabled", init.loading_progress_bar_enabled);
#else
    settings->rom_fast_reboot_enabled = ini_get_bool(ini, "menu", "reboot_rom_enabled", init.rom_fast_reboot_enabled);
#endif
    /* Beta feature flags, they might not be in the file */
    settings->show_browser_file_extensions = ini_get_bool(ini, "menu", "show_browser_file_extensions", init.show_browser_file_extensions);
    settings->show_browser_rom_tags = ini_get_bool(ini, "menu", "show_browser_rom_tags", init.show_browser_rom_tags);
    settings->wrap_file_list_scrolling = ini_get_bool(ini, "menu", "wrap_file_list_scrolling", init.wrap_file_list_scrolling);
    settings->rumble_enabled = ini_get_bool(ini, "menu_beta_flag", "rumble_enabled", init.rumble_enabled);

    settings->launcher_enabled = ini_get_bool(ini, "launcher", "enabled", init.launcher_enabled);
    free(settings->launcher_library_root);
    settings->launcher_library_root = strdup(ini_get_string(ini, "launcher", "library_root", init.launcher_library_root));
    settings->launcher_categories_enabled = ini_get_bool(ini, "launcher", "categories_enabled", init.launcher_categories_enabled);
    free(settings->launcher_category);
    settings->launcher_category = strdup(ini_get_string(ini, "launcher", "category", init.launcher_category));
    settings->launcher_layout = ini_get_int(ini, "launcher", "layout", init.launcher_layout);
    settings->launcher_tile_size = ini_get_int(ini, "launcher", "tile_size", init.launcher_tile_size);
    settings->launcher_sort = ini_get_int(ini, "launcher", "sort", init.launcher_sort);
    settings->launcher_widescreen = ini_get_bool(ini, "launcher", "widescreen", init.launcher_widescreen);
    settings->launcher_quick_launch = ini_get_bool(ini, "launcher", "quick_launch", init.launcher_quick_launch);
    settings->launcher_start_in_library = ini_get_bool(ini, "launcher", "start_in_library", init.launcher_start_in_library);
    settings->launcher_attract_minutes = ini_get_int(ini, "launcher", "attract_minutes", init.launcher_attract_minutes);
    settings->launcher_cartridge_art = ini_get_bool(ini, "launcher", "cartridge_art", init.launcher_cartridge_art);
    settings->launcher_one_per_game = ini_get_bool(ini, "launcher", "one_per_game", init.launcher_one_per_game);
    if (settings->launcher_attract_minutes < 0 || settings->launcher_attract_minutes > 60) {
        settings->launcher_attract_minutes = init.launcher_attract_minutes;
    }
    if (settings->launcher_sort < LAUNCHER_SORT_TITLE || settings->launcher_sort > LAUNCHER_SORT_PLAYERS) {
        settings->launcher_sort = init.launcher_sort;
    }
    settings->launcher_titles_highlight_only = ini_get_bool(ini, "launcher", "titles_highlight_only", init.launcher_titles_highlight_only);
    settings->launcher_home_resume = ini_get_bool(ini, "launcher", "home_resume", init.launcher_home_resume);
    settings->launcher_home_recent = ini_get_bool(ini, "launcher", "home_recent", init.launcher_home_recent);
    settings->launcher_home_favorites = ini_get_bool(ini, "launcher", "home_favorites", init.launcher_home_favorites);
    if (settings->launcher_layout < LAUNCHER_LAYOUT_GRID || settings->launcher_layout > LAUNCHER_LAYOUT_SHELF) {
        settings->launcher_layout = init.launcher_layout;
    }
    if (settings->launcher_tile_size < LAUNCHER_TILE_SMALL || settings->launcher_tile_size > LAUNCHER_TILE_LARGE) {
        settings->launcher_tile_size = init.launcher_tile_size;
    }

    ini_free(ini);
}

void settings_save (settings_t *settings) {
    ini_t *ini = ini_create();

    ini_set_int(ini, "menu", "schema_revision", settings->schema_revision);
    ini_set_bool(ini, "menu", "first_run", settings->first_run);
    ini_set_bool(ini, "menu", "pal60", settings->pal60_enabled);
    ini_set_bool(ini, "menu", "force_progressive_scan", settings->force_progressive_scan);
    ini_set_bool(ini, "menu", "show_protected_entries", settings->show_protected_entries);
    ini_set_string(ini, "menu", "default_directory", settings->default_directory);
    ini_set_bool(ini, "menu", "use_saves_folder", settings->use_saves_folder);
    ini_set_bool(ini, "menu", "show_saves_folder", settings->show_saves_folder);
    ini_set_bool(ini, "menu", "show_save_files", settings->show_save_files);
    ini_set_bool(ini, "menu", "show_cheat_files", settings->show_cheat_files);
    ini_set_bool(ini, "menu", "show_rom_configuration_files", settings->show_rom_configuration_files);
    ini_set_bool(ini, "menu", "soundfx_enabled", settings->soundfx_enabled);
    ini_set_bool(ini, "menu", "bgm_enabled", settings->bgm_enabled);
    ini_set_string(ini, "menu", "theme", settings->theme);
    ini_set_int(ini, "menu", "bgm_order", settings->bgm_order);
    ini_set_int(ini, "menu", "bgm_volume", settings->bgm_volume);
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    ini_set_bool(ini, "menu", "autoload_rom_enabled", settings->rom_autoload_enabled);
    ini_set_string(ini, "autoload", "rom_path", settings->rom_autoload_path);
    ini_set_string(ini, "autoload", "rom_filename", settings->rom_autoload_filename);
    ini_set_bool(ini, "menu", "loading_progress_bar_enabled", settings->loading_progress_bar_enabled);
#else
    ini_set_bool(ini, "menu", "reboot_rom_enabled", settings->rom_fast_reboot_enabled);
#endif

    /* Beta feature flags, they should not save until production ready! */
    // ini_set_bool(ini, "menu", "show_browser_file_extensions", settings->show_browser_file_extensions);
    // ini_set_bool(ini, "menu", "show_browser_rom_tags", settings->show_browser_rom_tags);
    ini_set_bool(ini, "menu", "wrap_file_list_scrolling", settings->wrap_file_list_scrolling);
    // ini_set_bool(ini, "menu_beta_flag", "rumble_enabled", settings->rumble_enabled);

    ini_set_bool(ini, "launcher", "enabled", settings->launcher_enabled);
    ini_set_string(ini, "launcher", "library_root", settings->launcher_library_root);
    ini_set_bool(ini, "launcher", "categories_enabled", settings->launcher_categories_enabled);
    ini_set_string(ini, "launcher", "category", settings->launcher_category);
    ini_set_int(ini, "launcher", "layout", settings->launcher_layout);
    ini_set_int(ini, "launcher", "tile_size", settings->launcher_tile_size);
    ini_set_int(ini, "launcher", "sort", settings->launcher_sort);
    ini_set_bool(ini, "launcher", "widescreen", settings->launcher_widescreen);
    ini_set_bool(ini, "launcher", "quick_launch", settings->launcher_quick_launch);
    ini_set_bool(ini, "launcher", "start_in_library", settings->launcher_start_in_library);
    ini_set_int(ini, "launcher", "attract_minutes", settings->launcher_attract_minutes);
    ini_set_bool(ini, "launcher", "cartridge_art", settings->launcher_cartridge_art);
    ini_set_bool(ini, "launcher", "one_per_game", settings->launcher_one_per_game);
    ini_set_bool(ini, "launcher", "titles_highlight_only", settings->launcher_titles_highlight_only);
    ini_set_bool(ini, "launcher", "home_resume", settings->launcher_home_resume);
    ini_set_bool(ini, "launcher", "home_recent", settings->launcher_home_recent);
    ini_set_bool(ini, "launcher", "home_favorites", settings->launcher_home_favorites);

    if (!ini_save(ini, settings_path)) {
        debugf("[SETTINGS] Failed to save settings to %s\n", settings_path);
    }

    ini_free(ini);
}

void settings_reset_to_defaults() {
    remove(settings_path);
}