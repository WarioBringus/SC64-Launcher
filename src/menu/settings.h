/**
 * @file settings.h
 * @brief Menu Settings
 * @ingroup menu 
 */

#ifndef SETTINGS_H__
#define SETTINGS_H__


/** @brief Launcher library layout */
typedef enum {
    LAUNCHER_LAYOUT_GRID = 0, /**< Grid of cover tiles */
    LAUNCHER_LAYOUT_CAROUSEL = 1, /**< Horizontal carousel of covers */
    LAUNCHER_LAYOUT_SHELF = 2, /**< Box spines on a shelf, the selected game opened to its front */
} launcher_layout_t;

/** @brief Launcher library sort order */
typedef enum {
    LAUNCHER_SORT_TITLE = 0,
    LAUNCHER_SORT_GENRE = 1,
    LAUNCHER_SORT_DEVELOPER = 2,
    LAUNCHER_SORT_YEAR = 3,
    LAUNCHER_SORT_PLAYERS = 4,
} launcher_sort_t;

/** @brief Launcher Home screen style (Look > Home style) */
typedef enum {
    LAUNCHER_HOME_BINDER = 0,      /**< Binder Mosaic: the lists as tabs, each a mosaic page (was Classic) */
    LAUNCHER_HOME_SPOTLIGHT = 1,    /**< The selected game large on a panel, the rest in a column beside it */
    LAUNCHER_HOME_CARTRIDGE = 2,    /**< Recent games as cartridges on a band, favorites below */
    LAUNCHER_HOME_MINIMAL = 3,      /**< Text lists and one large cover */
} launcher_home_style_t;

/** @brief Launcher grid tile size */
typedef enum {
    LAUNCHER_TILE_SMALL = 0, /**< Most tiles on screen */
    LAUNCHER_TILE_MEDIUM = 1, /**< Default */
    LAUNCHER_TILE_LARGE = 2, /**< Fewest tiles, covers at full size */
} launcher_tile_size_t;

/** @brief Settings Structure */
typedef struct {
    /** @brief Settings version */
    int schema_revision;

    /** @brief First run of the menu */
    bool first_run;

    /** @brief Use 60 Hz refresh rate on a PAL console */
    bool pal60_enabled;

    /** @brief Direct the VI to force progressive scan output at 240p. Meant for TVs and other devices which struggle to display interlaced video. */
    bool force_progressive_scan;

    /** @brief Show files/directories that are filtered in the browser */
    bool show_protected_entries;

    /** @brief Default directory to navigate to when menu loads */
    char *default_directory;

    /** @brief Put saves into separate directory */
    bool use_saves_folder;

    /** @brief Show saves folder in file browser */ 
    bool show_saves_folder;

    /** @brief Show save files in file browser */
    bool show_save_files;

    /** @brief Show cheat files in file browser */
    bool show_cheat_files;

    /** @brief Show rom configuration files in file browser */
    bool show_rom_configuration_files;

    /** @brief Show rom file extensions in browser */    
    bool show_browser_file_extensions;

    /** @brief Show rom tags in browser */  
    bool show_browser_rom_tags;

    /** @brief Wrap file list scrolling (infinite/circular navigation) */
    bool wrap_file_list_scrolling;

    /** @brief Enable Background music */
    bool bgm_enabled;

    /** @brief Enable Sound effects within the menu */
    bool soundfx_enabled;

    /** @brief Enable rumble feedback within the menu */
    bool rumble_enabled;

    /** @brief Name of the color theme (see theme.c) */
    char *theme;

    /** @brief Background music playlist order (sound_bgm_order_t) */
    int bgm_order;

    /** @brief Background music volume (sound_bgm_volume_t) */
    int bgm_volume;

    /** @brief Use the console-style launcher instead of the file browser */
    bool launcher_enabled;

    /** @brief Folder whose subfolders become launcher categories */
    char *launcher_library_root;

    /** @brief Show folders as switchable categories rather than one list */
    bool launcher_categories_enabled;

    /** @brief Category the library opens on (the last one used) */
    char *launcher_category;

    /** @brief Library layout (launcher_layout_t) */
    int launcher_layout;

    /** @brief Grid tile size (launcher_tile_size_t) */
    int launcher_tile_size;

    /** @brief Home screen style (launcher_home_style_t) */
    int launcher_home_style;

    /** @brief Library sort order (launcher_sort_t) */
    int launcher_sort;

    /** @brief Draw the launcher for a 16:9 display (the TV or Analogue 3D stretches it) */
    bool launcher_widescreen;

    /** @brief A starts the game straight away, skipping the ROM details screen */
    bool launcher_quick_launch;

    /** @brief The clock in 24-hour time (13:45) instead of 12-hour (1:45 PM) */
    bool launcher_clock_24h;

    /** @brief Open the launcher on the Library rather than Home */
    bool launcher_start_in_library;

    /** @brief Minutes idle before the attract mode slideshow starts (0 = never) */
    int launcher_attract_minutes;

    /** @brief Show cartridge label art instead of boxes where the metadata pack has it */
    bool launcher_cartridge_art;

    /** @brief With folder categories off, list each game once (USA, then Europe, then Japan) */
    bool launcher_one_per_game;

    /** @brief Show a game's title only while it is highlighted */
    bool launcher_titles_highlight_only;

    /** @brief Larger game titles and details (Look > Text size) */
    bool launcher_large_text;

    /** @brief Write the launcher's performance log (menu/cache/launcher/perf-log.txt); set by hand in config.ini */
    bool launcher_perf_log;

    /** @brief Show the "resume last game" row on the home screen */
    bool launcher_home_resume;

    /** @brief Show the recently played row on the home screen */
    bool launcher_home_recent;

    /** @brief Show the favorites row on the home screen */
    bool launcher_home_favorites;

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    /** @brief Enable the ability to bypass the menu and instantly load a ROM on power and reset button */
    bool rom_autoload_enabled;

    /** @brief A path to the autoloaded ROM */
    char *rom_autoload_path;

    /** @brief A filename of the autoloaded ROM */
    char *rom_autoload_filename;
    
    /** @brief Show progress bar when loading a ROM */
    bool loading_progress_bar_enabled;
#else
    /** @brief Enable the ability to bypass the menu and instantly load a ROM on reset button */
    bool rom_fast_reboot_enabled;
#endif

} settings_t;


/** @brief Init settings path */
void settings_init (char *path);
/** @brief The settings to load */
void settings_load (settings_t *settings);
/** @brief The settings to save */
void settings_save (settings_t *settings);
/**
 * @brief While deferred, settings_save only notes that a write is waiting (an SD card write is a visible stall, so
 * the Settings page writes once rather than on every press). Turning it off writes what's waiting.
 */
void settings_defer_saves (bool defer);
/** @brief Stop deferring, leaving a waiting write for settings_flush (or the next save) to make. */
void settings_resume_saves (void);
/** @brief Note that the settings changed: written once the controller rests, not now (an SD write is a stall). */
void settings_save_later (settings_t *settings);
/** @brief Whether a deferred write is waiting. */
bool settings_save_waiting (void);
/** @brief Write a waiting deferred save now. */
void settings_flush (void);
/** @brief Reset settings to defaults */
void settings_reset_to_defaults();

#endif
