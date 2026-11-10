/**
 * @file library.h
 * @brief Launcher game library: folders as categories, games and their covers.
 * @ingroup menu
 */

#ifndef LIBRARY_H__
#define LIBRARY_H__

#include <stdbool.h>

#include "menu_state.h"
#include "path.h"

/** @brief Cover lookup state for a game. */
typedef enum {
    LIBRARY_COVER_UNKNOWN = 0,  /**< Not looked up yet */
    LIBRARY_COVER_FOUND,        /**< cover_path is set */
    LIBRARY_COVER_NONE,         /**< No cover on the SD card */
} library_cover_state_t;

/** @brief One game (ROM file). */
typedef struct {
    char *file_name;            /**< File name with extension */
    char *title;                /**< File name without extension, shown in the UI */
    path_t *directory;          /**< Folder holding the file (owned by the category) */
    library_cover_state_t cover_state;
    char *cover_path;           /**< Full path to the cover PNG when found */
    const char *genre;          /**< Game details from the cover index, or NULL when unknown */
    const char *developer;
    const char *year;
    int players;                /**< Maximum players, 0 when unknown */
    uint8_t spine_width;        /**< Shelf spine width once worked out (library_game_spine_width), else 0 */
    int8_t group_sort;          /**< Sort order group_key was worked out for, plus one (0 = not yet) */
    uint32_t group_key;         /**< Hash of the game's group label (library_game_group) for that order */
    uint32_t favorite_generation; /**< launcher_favorites_generation() when favorite was worked out */
    bool favorite;              /**< In the favorites (valid when favorite_generation is current) */
    uint32_t hidden_generation; /**< Hidden list version when hidden was worked out */
    bool hidden;                /**< Hidden from the library (valid when hidden_generation is current) */
    bool shown_now;             /**< Passes the filter being arranged for (worked out once before a sort) */
    uint16_t title_rank;        /**< Place in title order within its category (when the category is ranked) */
} library_game_t;

/** @brief One category (a folder of ROMs). */
typedef struct {
    char *name;                 /**< Folder name, shown in the UI */
    path_t *directory;          /**< Folder path */
    bool loaded;                /**< games[] has been listed */
    bool portrait;              /**< Covers in this folder are mostly portrait (Japanese boxes) */
    bool from_index;            /**< Games come from the cover index rather than a folder listing */
    bool folder_hidden;         /**< Left out of the library (Start > Library > Folders) */
    int index_first;            /**< First index entry of this folder (-1: root files) */
    int index_count;            /**< Number of index entries of this folder */
    int sort;                   /**< Order games[] is in (launcher_sort_t) */
    int filter_field;           /**< Filter games[] is arranged for (a launcher_sort_t field, or -1) */
    char filter_value[64];      /**< Group label the filter keeps */
    uint32_t hidden_generation; /**< Hidden list version games[] is arranged for */
    int shown;                  /**< Games to show: not hidden and matching the filter (they come first) */
    library_game_t *games;
    int count;
    bool ranked;                /**< games[].title_rank is worked out (titles don't change once listed) */
} library_category_t;

/**
 * @brief Find the categories under the library root (folders that contain ROMs).
 *
 * Safe to call repeatedly; it rescans only when the root or category setting changed.
 *
 * @param menu Menu state (settings and storage prefix).
 */
void library_init(menu_t *menu);

/** @brief Number of categories (1 when categories are turned off). */
int library_category_count(void);

/** @brief Name of a category without listing its games. */
const char *library_category_name(int index);

/** @brief Index of the category with this name (case-insensitive), or -1. */
int library_category_find(const char *name);

/** @brief Whether getting a category's games is quick (already listed, or listed from the index). */
bool library_category_is_quick(int index);

/**
 * @brief Re-list the folders on the SD card, ignoring the cover index's game lists until reboot.
 *
 * For ROMs added since the cover tool last ran.
 */
void library_rescan(menu_t *menu);

/**
 * @brief Get a category, listing its games on first use.
 *
 * @param index Category index.
 * @return The category, or NULL if out of range.
 */
library_category_t *library_category(int index);

/**
 * @brief Path of a game's ROM file. The caller frees it with path_free().
 */
path_t *library_game_path(library_game_t *game);

/**
 * @brief Look up a game's cover on the SD card (once; the result is remembered).
 *
 * @return Full path of the cover PNG, or NULL when there is none.
 */
const char *library_game_cover(menu_t *menu, library_game_t *game);

/**
 * @brief While enabled, every folder's category is the whole library (every visible folder's games),
 * for searching everywhere.
 */
void library_search_everywhere(bool enabled);

/** @brief Width of a game's spine on the shelf: as thick as its box (covers.idx), else LAUNCHER_SPINE_W. */
int library_game_spine_width(menu_t *menu, library_game_t *game);

/**
 * @brief Look up the cover for any ROM file (used by the home screen).
 *
 * @param menu Menu state.
 * @param rom_path Full path of the ROM file.
 * @return Newly allocated path of the cover PNG (caller frees), or NULL.
 */
char *library_find_cover(menu_t *menu, path_t *rom_path);

/**
 * @brief Display title for a ROM path (file name without extension, or its fix in
 * menu/game-fixes.txt). Caller frees.
 */
char *library_title_from_path(menu_t *menu, path_t *rom_path);

/**
 * @brief Id of a cover in the PC-built cover index (menu/cache/launcher/covers.idx).
 *
 * @param cover_rel Cover path relative to the menu folder, e.g. "metadata/N/G/E/E/boxart_front.png".
 * @return Its id (its position in the thumbnail packs), or -1 when not indexed.
 */
int library_cover_id(const char *cover_rel);

/**
 * @brief Put a category's games in the given order (title within each group), with games matching
 * the filter first and `shown` set to how many match.
 *
 * @param sort Sort order (launcher_sort_t).
 * @param filter_field Field to filter on (LAUNCHER_SORT_GENRE...PLAYERS, or LAUNCHER_FILTER_SEARCH), or -1 for no filter.
 * @param filter_value Group label to keep (as library_game_group writes it), or the text searched for.
 * @return true if the arrangement changed.
 */
bool library_arrange_category(library_category_t *category, int sort, int filter_field, const char *filter_value);

/** @brief Filter field that keeps games whose title holds the filter value (case, spaces and punctuation ignored). */
#define LAUNCHER_FILTER_SEARCH  (100)

/** @brief One group in a category, for the filter picker. */
typedef struct {
    char label[64];
    int count;
} library_group_t;

/**
 * @brief The distinct groups of a field in a category, with how many games each has.
 *
 * @param groups Receives a malloc'd array (free it with free()).
 * @return Number of groups.
 */
int library_category_groups(library_category_t *category, int field, library_group_t **groups);

/** @brief Whether a game is hidden from the library. */
bool library_game_hidden(library_game_t *game);

/** @brief Hide a ROM from the library, or show it again (saved in menu/cache/launcher/hidden.txt). */
void library_set_hidden(menu_t *menu, path_t *rom_path, bool hidden);

/** @brief Number of hidden ROMs. */
int library_hidden_count(menu_t *menu);

/** @brief Full path of hidden ROM number i. */
const char *library_hidden_at(int index);

/**
 * @brief One-line game details, e.g. "Racing  ·  1997  ·  Nintendo EAD  ·  4 players". Empty when unknown.
 */
void library_game_details(library_game_t *game, char *buffer, size_t size);

/**
 * @brief Details line for any ROM path (looked up in the cover index), for the home screen.
 */
void library_details_for_path(menu_t *menu, path_t *rom_path, char *buffer, size_t size);

/**
 * @brief The group a game falls in for a sort order: its first letter, genre, developer, year or player count.
 *
 * @param buffer Receives the label, e.g. "M", "Racing", "1998", "4 players".
 */
void library_game_group(library_game_t *game, int sort, char *buffer, size_t size);

/** @brief A ROM's publisher from the cover index, or NULL when unknown (owned by the library). */
const char *library_publisher_for_path(menu_t *menu, path_t *rom_path);

/** @brief Number of game folders, shown or hidden, in the order the library uses. */
int library_folder_count(void);

/** @brief A game folder's name (index 0 .. library_folder_count() - 1). */
const char *library_folder_name(int index);

/** @brief Whether a game folder is left out of the library. */
bool library_folder_hidden(int index);

/**
 * @brief Show or hide a game folder (kept until library_folders_save).
 *
 * @return false when this would hide the last shown folder.
 */
bool library_folder_set_hidden(int index, bool hidden);

/**
 * @brief Move a game folder up (delta -1) or down (+1) in the order.
 *
 * @return The folder's new index.
 */
int library_folder_move(int index, int delta);

/** @brief Save the folder order and hidden folders (menu/cache/launcher/folders.txt). */
void library_folders_save(void);

/**
 * @brief Walk the folders a little (for about budget_us) looking for ROMs the cover index doesn't
 * list yet. Call every frame; it stops by itself once done.
 */
void library_check_new_games(int budget_us);

/** @brief Whether the check for ROMs added since the cover index was built has finished (or has nothing to do). */
bool library_new_games_checked(void);

/**
 * @brief Whether a file beside a ROM (.ini, .meta) is known not to exist: its folder was walked by the new-ROM check
 * and it wasn't there. False when unknown.
 */
bool library_file_known_absent(const char *path);

/** @brief ROMs found that aren't in the cover index (0 until the check has finished, or without an index). */
int library_new_game_count(void);

/** @brief Free everything. */
void library_free(void);

#endif /* LIBRARY_H__ */
