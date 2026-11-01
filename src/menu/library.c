/**
 * @file library.c
 * @brief Launcher game library: folders as categories, games and their covers.
 * @ingroup menu
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libdragon.h>

#include "library.h"
#include "utils/fs.h"

#define METADATA_DIRECTORY      "menu/metadata"
#define OLD_BOXART_DIRECTORY    "menu/boxart"
#define HOMEBREW_SUBDIRECTORY   "homebrew"
#define BY_NAME_SUBDIRECTORY    "names"
#define COVER_FILE              "boxart_front.png"
#define ALL_GAMES_NAME          "All Games"
#define ROOT_CATEGORY_NAME      "SD Card"
#define FALLBACK_REGIONS        "EPJUADFIS"
#define PORTRAIT_SAMPLE_SIZE    (8)

static const char *rom_extensions[] = { "z64", "n64", "v64", "rom", NULL };
static const char *skipped_folders[] = { "System Volume Information", "menu", NULL };

/* Folders that hold ROMs. When categories are on, these are the categories. */
static library_category_t *folders = NULL;
static int folder_count = 0;

/* Single merged category used when categories are off. */
static library_category_t all_games;
static bool categories_enabled = true;

static bool scanned = false;
static char *scanned_root = NULL;
static const char *storage_prefix = NULL;


static bool is_rom_name (const char *name) {
    return (name[0] != '.') && file_has_extensions((char *)name, rom_extensions);
}

static bool is_skipped_folder (const char *name) {
    if (name[0] == '.') {
        return true;
    }
    for (int i = 0; skipped_folders[i] != NULL; i++) {
        if (strcasecmp(name, skipped_folders[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int compare_games (const void *a, const void *b) {
    return strcasecmp(((const library_game_t *)a)->title, ((const library_game_t *)b)->title);
}

static int compare_categories (const void *a, const void *b) {
    return strcasecmp(((const library_category_t *)a)->name, ((const library_category_t *)b)->name);
}

static char *title_from_file_name (const char *file_name) {
    char *title = strdup(file_name);
    char *dot = strrchr(title, '.');
    if (dot && dot != title) {
        *dot = '\0';
    }
    return title;
}

/**
 * @brief Read the game code and internal title from a ROM header, in any byte order.
 */
static bool read_rom_header (const char *path, char code[4], char title[21]) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    uint8_t h[0x40];
    size_t n = fread(h, 1, sizeof(h), f);
    fclose(f);
    if (n < sizeof(h)) {
        return false;
    }

    if (h[0] == 0x80 && h[1] == 0x37) {
        // z64: big-endian, as-is
    } else if (h[0] == 0x37 && h[1] == 0x80) {
        // v64: 16-bit byte-swapped
        for (int i = 0; i < (int)sizeof(h); i += 2) {
            uint8_t t = h[i]; h[i] = h[i + 1]; h[i + 1] = t;
        }
    } else if (h[0] == 0x40 && h[1] == 0x12) {
        // n64: 32-bit little-endian
        for (int i = 0; i < (int)sizeof(h); i += 4) {
            uint8_t t0 = h[i], t1 = h[i + 1];
            h[i] = h[i + 3]; h[i + 1] = h[i + 2]; h[i + 2] = t1; h[i + 3] = t0;
        }
    } else {
        return false;
    }

    memcpy(code, h + 0x3B, 4);
    memcpy(title, h + 0x20, 20);
    title[20] = '\0';
    return true;
}

static bool code_is_valid (const char code[4]) {
    for (int i = 0; i < 4; i++) {
        if (!isalnum((unsigned char)code[i])) {
            return false;
        }
    }
    return true;
}

static char *file_if_exists (path_t *path) {
    return file_exists(path_get(path)) ? strdup(path_get(path)) : NULL;
}

/**
 * @brief Find a cover by game code, following the stock menu's rules first
 * (metadata/C/I/D/R, then metadata/C/I/D, then the old boxart folder),
 * then falling back to another region's art for the same game.
 */
static char *find_cover_by_code (const char code[4], const char *raw_title) {
    char *found = NULL;

    if (code[1] == 'E' && code[2] == 'D') {
        // Homebrew ID: the stock menu names the folder after the raw 20-character header title.
        path_t *path = path_init(storage_prefix, METADATA_DIRECTORY);
        path_push(path, HOMEBREW_SUBDIRECTORY);
        path_push(path, (char *)raw_title);
        path_push(path, COVER_FILE);
        found = file_if_exists(path);
        path_free(path);
        return found;
    }

    const char *bases[] = { METADATA_DIRECTORY, OLD_BOXART_DIRECTORY };
    char game_dir[8];
    snprintf(game_dir, sizeof(game_dir), "%c/%c/%c", code[0], code[1], code[2]);

    for (int b = 0; b < 2 && !found; b++) {
        path_t *path = path_init(storage_prefix, (char *)bases[b]);
        path_push(path, game_dir);

        if (directory_exists(path_get(path))) {
            char region[2] = { code[3], '\0' };

            // metadata/C/I/D/R/boxart_front.png
            path_push(path, region);
            path_push(path, COVER_FILE);
            found = file_if_exists(path);
            path_pop(path);
            path_pop(path);

            // metadata/C/I/D/boxart_front.png
            if (!found) {
                path_push(path, COVER_FILE);
                found = file_if_exists(path);
                path_pop(path);
            }

            // Another region's art for the same game
            for (int r = 0; !found && FALLBACK_REGIONS[r] != '\0'; r++) {
                if (FALLBACK_REGIONS[r] == code[3]) {
                    continue;
                }
                region[0] = FALLBACK_REGIONS[r];
                path_push(path, region);
                path_push(path, COVER_FILE);
                found = file_if_exists(path);
                path_pop(path);
                path_pop(path);
            }
        }
        path_free(path);
    }

    return found;
}

/**
 * @brief Cover named after the game: menu/metadata/names/<title>.png.
 * Covers ROMs whose header has no game code.
 */
static char *find_cover_by_name (const char *title) {
    path_t *path = path_init(storage_prefix, METADATA_DIRECTORY);
    path_push(path, BY_NAME_SUBDIRECTORY);
    size_t len = strlen(title);
    char *file = malloc(len + 5);
    memcpy(file, title, len);
    memcpy(file + len, ".png", 5);
    path_push(path, file);
    free(file);
    char *found = file_if_exists(path);
    path_free(path);
    return found;
}

static char *find_cover (path_t *rom_path, const char *title) {
    char code[4];
    char raw_title[21];
    char *found = NULL;

    if (read_rom_header(path_get(rom_path), code, raw_title) && code_is_valid(code)) {
        found = find_cover_by_code(code, raw_title);
    }
    if (!found) {
        found = find_cover_by_name(title);
    }
    return found;
}

static bool folder_has_roms (path_t *directory) {
    bool has_roms = false;
    dir_t info;
    // Walk the whole listing so the directory handle is released at the end.
    int result = dir_findfirst(path_get(directory), &info);
    while (result == 0) {
        if (!has_roms && info.d_type != DT_DIR && is_rom_name(info.d_name)) {
            has_roms = true;
        }
        result = dir_findnext(path_get(directory), &info);
    }
    return has_roms;
}

/**
 * @brief Guess whether a folder holds portrait (Japanese) boxes from a sample of game codes.
 */
static bool detect_portrait (library_category_t *c) {
    int japan = 0;
    int sampled = 0;
    for (int i = 0; i < c->count && sampled < PORTRAIT_SAMPLE_SIZE; i++) {
        path_t *path = path_clone_push(c->games[i].directory, c->games[i].file_name);
        char code[4];
        char raw_title[21];
        if (read_rom_header(path_get(path), code, raw_title) && code_is_valid(code)) {
            sampled++;
            if (code[3] == 'J') {
                japan++;
            }
        }
        path_free(path);
    }
    return sampled > 0 && (japan * 2 > sampled);
}

static void load_folder_games (library_category_t *c) {
    int capacity = 64;
    c->games = malloc(capacity * sizeof(library_game_t));
    c->count = 0;

    dir_t info;
    int result = dir_findfirst(path_get(c->directory), &info);
    while (result == 0) {
        if (info.d_type != DT_DIR && is_rom_name(info.d_name)) {
            if (c->count == capacity) {
                capacity *= 2;
                c->games = realloc(c->games, capacity * sizeof(library_game_t));
            }
            library_game_t *g = &c->games[c->count++];
            memset(g, 0, sizeof(*g));
            g->file_name = strdup(info.d_name);
            g->title = title_from_file_name(info.d_name);
            g->directory = c->directory;
        }
        result = dir_findnext(path_get(c->directory), &info);
    }

    qsort(c->games, c->count, sizeof(library_game_t), compare_games);
    c->portrait = detect_portrait(c);
    c->loaded = true;
}

static void load_all_games (void) {
    int total = 0;
    for (int i = 0; i < folder_count; i++) {
        if (!folders[i].loaded) {
            load_folder_games(&folders[i]);
        }
        total += folders[i].count;
    }

    // Shallow copies: names and directories stay owned by the folder entries.
    all_games.games = malloc((total > 0 ? total : 1) * sizeof(library_game_t));
    all_games.count = 0;
    for (int i = 0; i < folder_count; i++) {
        for (int j = 0; j < folders[i].count; j++) {
            library_game_t *g = &all_games.games[all_games.count++];
            *g = folders[i].games[j];
            g->cover_state = LIBRARY_COVER_UNKNOWN;
            g->cover_path = NULL;
        }
    }
    qsort(all_games.games, all_games.count, sizeof(library_game_t), compare_games);
    all_games.portrait = false;
    all_games.loaded = true;
}

/**
 * @brief Drop a prefix shared by every category name ("N64 USA", "N64 Japan" -> "USA", "Japan").
 */
static void strip_common_prefix (void) {
    if (folder_count < 2) {
        return;
    }
    size_t prefix = strlen(folders[0].name);
    for (int i = 1; i < folder_count; i++) {
        size_t j = 0;
        while (j < prefix && folders[i].name[j] == folders[0].name[j]) {
            j++;
        }
        prefix = j;
    }
    // Only cut at a word boundary, and never empty a name.
    while (prefix > 0 && folders[0].name[prefix - 1] != ' ') {
        prefix--;
    }
    if (prefix == 0) {
        return;
    }
    for (int i = 0; i < folder_count; i++) {
        if (strlen(folders[i].name) <= prefix) {
            return;
        }
    }
    for (int i = 0; i < folder_count; i++) {
        memmove(folders[i].name, folders[i].name + prefix, strlen(folders[i].name + prefix) + 1);
    }
}

static void scan_folders (path_t *root) {
    int capacity = 8;
    folders = malloc(capacity * sizeof(library_category_t));
    folder_count = 0;

    // Collect candidate names first; checking each folder starts a new listing.
    int name_capacity = 16;
    int name_count = 0;
    char **names = malloc(name_capacity * sizeof(char *));
    dir_t info;
    int result = dir_findfirst(path_get(root), &info);
    while (result == 0) {
        if (info.d_type == DT_DIR && !is_skipped_folder(info.d_name)) {
            if (name_count == name_capacity) {
                name_capacity *= 2;
                names = realloc(names, name_capacity * sizeof(char *));
            }
            names[name_count++] = strdup(info.d_name);
        }
        result = dir_findnext(path_get(root), &info);
    }

    for (int i = 0; i < name_count; i++) {
        path_t *directory = path_clone_push(root, names[i]);
        if (folder_has_roms(directory)) {
            if (folder_count == capacity) {
                capacity *= 2;
                folders = realloc(folders, capacity * sizeof(library_category_t));
            }
            library_category_t *c = &folders[folder_count++];
            memset(c, 0, sizeof(*c));
            c->name = names[i];
            c->directory = directory;
            names[i] = NULL;
        } else {
            path_free(directory);
            free(names[i]);
        }
    }
    free(names);

    qsort(folders, folder_count, sizeof(library_category_t), compare_categories);
    strip_common_prefix();

    // ROMs sitting directly in the root get their own category.
    if (folder_has_roms(root)) {
        if (folder_count == capacity) {
            folders = realloc(folders, (capacity + 1) * sizeof(library_category_t));
        }
        library_category_t *c = &folders[folder_count++];
        memset(c, 0, sizeof(*c));
        c->name = strdup(ROOT_CATEGORY_NAME);
        c->directory = path_clone(root);
    }
}

void library_init (menu_t *menu) {
    const char *root_setting = menu->settings.launcher_library_root;
    if (!root_setting || root_setting[0] == '\0') {
        root_setting = "/";
    }
    if (scanned && scanned_root && strcmp(scanned_root, root_setting) == 0 &&
        categories_enabled == menu->settings.launcher_categories_enabled) {
        return;
    }

    library_free();

    storage_prefix = menu->storage_prefix;
    categories_enabled = menu->settings.launcher_categories_enabled;
    scanned_root = strdup(root_setting);

    path_t *root = path_init(storage_prefix, scanned_root);
    scan_folders(root);
    path_free(root);

    memset(&all_games, 0, sizeof(all_games));
    all_games.name = ALL_GAMES_NAME;
    scanned = true;
}

int library_category_count (void) {
    if (!categories_enabled) {
        return folder_count > 0 ? 1 : 0;
    }
    return folder_count;
}

const char *library_category_name (int index) {
    if (!categories_enabled) {
        return (index == 0 && folder_count > 0) ? ALL_GAMES_NAME : NULL;
    }
    return (index >= 0 && index < folder_count) ? folders[index].name : NULL;
}

library_category_t *library_category (int index) {
    if (!categories_enabled) {
        if (index != 0 || folder_count == 0) {
            return NULL;
        }
        if (!all_games.loaded) {
            load_all_games();
        }
        return &all_games;
    }
    if (index < 0 || index >= folder_count) {
        return NULL;
    }
    if (!folders[index].loaded) {
        load_folder_games(&folders[index]);
    }
    return &folders[index];
}

path_t *library_game_path (library_game_t *game) {
    return path_clone_push(game->directory, game->file_name);
}

const char *library_game_cover (menu_t *menu, library_game_t *game) {
    if (game->cover_state == LIBRARY_COVER_UNKNOWN) {
        path_t *path = library_game_path(game);
        game->cover_path = find_cover(path, game->title);
        game->cover_state = game->cover_path ? LIBRARY_COVER_FOUND : LIBRARY_COVER_NONE;
        path_free(path);
    }
    return game->cover_path;
}

char *library_find_cover (menu_t *menu, path_t *rom_path, const char *title) {
    if (!storage_prefix) {
        storage_prefix = menu->storage_prefix;
    }
    return find_cover(rom_path, title);
}

char *library_title_from_path (path_t *rom_path) {
    return title_from_file_name(path_last_get(rom_path));
}

void library_free (void) {
    if (all_games.games) {
        for (int i = 0; i < all_games.count; i++) {
            free(all_games.games[i].cover_path);
        }
        free(all_games.games);
    }
    memset(&all_games, 0, sizeof(all_games));

    for (int i = 0; i < folder_count; i++) {
        library_category_t *c = &folders[i];
        for (int j = 0; j < c->count; j++) {
            free(c->games[j].file_name);
            free(c->games[j].title);
            free(c->games[j].cover_path);
        }
        free(c->games);
        free(c->name);
        path_free(c->directory);
    }
    free(folders);
    folders = NULL;
    folder_count = 0;

    free(scanned_root);
    scanned_root = NULL;
    scanned = false;
}
