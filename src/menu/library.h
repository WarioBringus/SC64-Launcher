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
} library_game_t;

/** @brief One category (a folder of ROMs). */
typedef struct {
    char *name;                 /**< Folder name, shown in the UI */
    path_t *directory;          /**< Folder path */
    bool loaded;                /**< games[] has been listed */
    bool portrait;              /**< Covers in this folder are mostly portrait (Japanese boxes) */
    library_game_t *games;
    int count;
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
 * @brief Look up the cover for any ROM file (used by the home screen).
 *
 * @param menu Menu state.
 * @param rom_path Full path of the ROM file.
 * @param title Display title, used for the by-name fallback.
 * @return Newly allocated path of the cover PNG (caller frees), or NULL.
 */
char *library_find_cover(menu_t *menu, path_t *rom_path, const char *title);

/**
 * @brief Display title for a ROM path (file name without extension). Caller frees.
 */
char *library_title_from_path(path_t *rom_path);

/** @brief Free everything. */
void library_free(void);

#endif /* LIBRARY_H__ */
