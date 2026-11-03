/**
 * @file library.c
 * @brief Launcher game library: folders as categories, games and their covers.
 * @ingroup menu
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libdragon.h>

#include "library.h"
#include "sound.h"
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
#define COVER_INDEX_FILE        "menu/cache/launcher/covers.idx"
#define HIDDEN_FILE             "menu/cache/launcher/hidden.txt"
#define FOLDERS_FILE            "menu/cache/launcher/folders.txt"
#define MAX_HIDDEN              (256)
#define COVER_INDEX_MAGIC       "LCIDX1"

static const char *rom_extensions[] = { "z64", "n64", "v64", "rom", NULL };
static const char *skipped_folders[] = { "System Volume Information", "menu", NULL };

/* Folders that hold ROMs. When categories are on, these are the categories. */
static library_category_t *folders = NULL;
static int folder_count = 0;

/* Single merged category used when categories are off. */
static int *visible = NULL;             /* Category index -> folders[] index, shown folders only */
static int visible_count = 0;
static library_category_t all_games;
static bool categories_enabled = true;
static bool one_per_game = true;

static bool scanned = false;
static char *scanned_root = NULL;
static const char *storage_prefix = NULL;

/* Cover index written by pc-tools/coverpack/coverpack.py: which cover each ROM uses. */
typedef struct {
    const char *rom;            /**< ROM path relative to the SD card root */
    int cover;                  /**< Index into index_covers, or -1 */
} index_rom_t;

static char *index_text = NULL;
static const char **index_covers = NULL;
static int index_cover_count = 0;
static index_rom_t *index_roms = NULL;
static int index_rom_count = 0;

/* Game details lines (M) from the index: genre, developer, year, players, publisher. */
typedef struct {
    const char *rom;
    const char *genre;
    const char *developer;
    const char *year;
    int players;
    const char *publisher;
} index_meta_t;

static index_meta_t *index_meta = NULL;
static int index_meta_count = 0;
static bool index_tried = false;
static bool index_lists_disabled = false;

/* Background check for ROMs added since the cover index was built. */
static int new_check_folder = 0;        /* Next folder to walk (index into folders[]) */
static bool new_check_listing = false;  /* A folder listing is in progress */
static dir_t new_check_entry;
static bool new_check_done = false;
static int new_game_count = 0;


static int index_rom_cover (path_t *rom_path);


static bool is_rom_name (const char *name) {
    // Flashcart menu files (sc64menu.n64 and its backups, EverDrive OS64*.v64) are not games.
    if (name[0] == '.' || strncasecmp(name, "sc64menu", 8) == 0 || strncasecmp(name, "OS64", 4) == 0) {
        return false;
    }
    return file_has_extensions((char *)name, rom_extensions);
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

/** @brief A title as it sorts: "The Legend of Zelda" files under L. */
static const char *sort_title (const char *title) {
    return (strncasecmp(title, "The ", 4) == 0 && title[4] != '\0') ? title + 4 : title;
}

static int compare_titles (const char *a, const char *b) {
    int r = strcasecmp(sort_title(a), sort_title(b));
    return (r != 0) ? r : strcasecmp(a, b);
}

static int compare_games (const void *a, const void *b) {
    return compare_titles(((const library_game_t *)a)->title, ((const library_game_t *)b)->title);
}

static int compare_categories (const void *a, const void *b) {
    return strcasecmp(((const library_category_t *)a)->name, ((const library_category_t *)b)->name);
}

static char *file_stem (const char *file_name) {
    char *stem = strdup(file_name);
    char *dot = strrchr(stem, '.');
    if (dot && dot != stem) {
        *dot = '\0';
    }
    return stem;
}

static char *title_from_file_name (const char *file_name) {
    char *title = file_stem(file_name);
    // File names can't hold a colon, so "Castlevania - Legacy of Darkness" stands for one.
    char *dash;
    while ((dash = strstr(title, " - ")) != NULL) {
        dash[0] = ':';
        memmove(dash + 1, dash + 2, strlen(dash + 2) + 1);
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
 * @brief Cover named after the game's file: menu/metadata/names/<file name without extension>.png.
 * Takes priority over the game code, and covers ROMs whose header has no game code.
 */
static char *find_cover_by_name (path_t *rom_path) {
    path_t *path = path_init(storage_prefix, METADATA_DIRECTORY);
    path_push(path, BY_NAME_SUBDIRECTORY);
    char *stem = file_stem(path_last_get(rom_path));
    size_t len = strlen(stem);
    char *file = malloc(len + 5);
    memcpy(file, stem, len);
    memcpy(file + len, ".png", 5);
    free(stem);
    path_push(path, file);
    free(file);
    char *found = file_if_exists(path);
    path_free(path);
    return found;
}

static char *find_cover (path_t *rom_path) {
    char code[4];
    char raw_title[21];
    char *found = NULL;

    // The PC-built index answers without touching the ROM; games it has no cover for
    // still get a live lookup, so art added since the index was built is found.
    int indexed = index_rom_cover(rom_path);
    if (indexed >= 0) {
        path_t *path = path_init(storage_prefix, "menu");
        path_push(path, (char *)index_covers[indexed]);
        found = strdup(path_get(path));
        path_free(path);
        return found;
    }

    // A cover named after the game wins: it fixes prototypes and hacks that reuse another game's code.
    found = find_cover_by_name(rom_path);
    if (!found && read_rom_header(path_get(rom_path), code, raw_title) && code_is_valid(code)) {
        found = find_cover_by_code(code, raw_title);
    }
    return found;
}

static void index_free (void) {
    free(index_text);
    free(index_covers);
    free(index_roms);
    free(index_meta);
    index_text = NULL;
    index_covers = NULL;
    index_roms = NULL;
    index_meta = NULL;
    index_cover_count = 0;
    index_rom_count = 0;
    index_meta_count = 0;
    index_tried = false;
}

/**
 * @brief Load the cover index once. Without it, covers are looked up on the SD card as before.
 */
static void index_load (void) {
    if (index_tried || !storage_prefix) {
        return;
    }
    index_tried = true;

    path_t *path = path_init(storage_prefix, COVER_INDEX_FILE);
    FILE *f = fopen(path_get(path), "rb");
    path_free(path);
    if (!f) {
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return;
    }
    index_text = malloc(size + 1);
    if (!index_text || fread(index_text, 1, size, f) != (size_t)size) {
        fclose(f);
        index_free();
        index_tried = true;
        return;
    }
    fclose(f);
    index_text[size] = '\0';
    if (strncmp(index_text, COVER_INDEX_MAGIC, strlen(COVER_INDEX_MAGIC)) != 0) {
        index_free();
        index_tried = true;
        return;
    }

    int lines = 1;
    for (char *p = index_text; *p; p++) {
        if (*p == '\n') {
            lines++;
        }
    }
    index_covers = malloc(lines * sizeof(char *));
    index_roms = malloc(lines * sizeof(index_rom_t));
    index_meta = malloc(lines * sizeof(index_meta_t));

    // Lines are already sorted byte-wise by the PC tool, so both lists can be binary searched.
    char *line = index_text;
    while (line && *line) {
        char *end = strchr(line, '\n');
        if (end) {
            *end = '\0';
        }
        if (line[0] == 'C' && line[1] == '\t') {
            index_covers[index_cover_count++] = line + 2;
        } else if (line[0] == 'M' && line[1] == '\t') {
            // M<TAB>rom<TAB>genre<TAB>developer<TAB>year<TAB>players[<TAB>publisher]
            char *fields[6];
            char *p = line + 2;
            int n = 0;
            fields[n++] = p;
            while (n < 6 && (p = strchr(p, '\t')) != NULL) {
                *p++ = '\0';
                fields[n++] = p;
            }
            if (n >= 5) {
                index_meta_t *m = &index_meta[index_meta_count++];
                m->rom = fields[0];
                m->genre = fields[1][0] ? fields[1] : NULL;
                m->developer = fields[2][0] ? fields[2] : NULL;
                m->year = fields[3][0] ? fields[3] : NULL;
                m->players = atoi(fields[4]);
                m->publisher = NULL;
                if (n == 6) {
                    char *end = strchr(fields[5], '\t');
                    if (end) {
                        *end = '\0';
                    }
                    m->publisher = fields[5][0] ? fields[5] : NULL;
                }
            }
        } else if (line[0] == 'R' && line[1] == '\t') {
            char *tab = strrchr(line + 2, '\t');
            if (tab) {
                *tab = '\0';
                index_roms[index_rom_count].rom = line + 2;
                index_roms[index_rom_count].cover = atoi(tab + 1);
                index_rom_count++;
            }
        }
        line = end ? end + 1 : NULL;
    }
}

static int compare_index_cover (const void *key, const void *element) {
    return strcmp((const char *)key, *(const char * const *)element);
}

static int compare_index_rom (const void *key, const void *element) {
    return strcmp((const char *)key, ((const index_rom_t *)element)->rom);
}

static int compare_index_meta (const void *key, const void *element) {
    return strcmp((const char *)key, ((const index_meta_t *)element)->rom);
}

/**
 * @brief Cover id for a ROM from the index: >= 0 for a cover, -1 when the index has none or no entry.
 */
static int index_rom_cover (path_t *rom_path) {
    index_load();
    if (!index_roms) {
        return -1;
    }
    const char *rel = path_get(rom_path);
    size_t prefix_len = strlen(storage_prefix);
    if (strncmp(rel, storage_prefix, prefix_len) == 0) {
        rel += prefix_len;
    }
    while (*rel == '/') {
        rel++;
    }
    index_rom_t *hit = bsearch(rel, index_roms, index_rom_count, sizeof(index_rom_t), compare_index_rom);
    if (!hit || hit->cover < 0 || hit->cover >= index_cover_count) {
        return -1;
    }
    return hit->cover;
}

int library_cover_id (const char *cover_rel) {
    index_load();
    if (!index_covers) {
        return -1;
    }
    const char **hit = bsearch(cover_rel, index_covers, index_cover_count, sizeof(char *), compare_index_cover);
    return hit ? (int)(hit - index_covers) : -1;
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

/**
 * @brief Make the full path of an indexed cover.
 */
static char *index_cover_path (int id) {
    path_t *path = path_init(storage_prefix, "menu");
    path_push(path, (char *)index_covers[id]);
    char *full = strdup(path_get(path));
    path_free(path);
    return full;
}

/**
 * @brief List a folder's games from the cover index: no SD card access, covers already known.
 */
static void load_index_games (library_category_t *c) {
    int first = (c->index_first >= 0) ? c->index_first : 0;
    int last = (c->index_first >= 0) ? c->index_first + c->index_count : index_rom_count;
    c->games = malloc(((last - first) > 0 ? (last - first) : 1) * sizeof(library_game_t));
    c->count = 0;
    int japan = 0;
    int with_cover = 0;

    for (int k = first; k < last; k++) {
        const char *rel = index_roms[k].rom;
        const char *slash = strchr(rel, '/');
        if (c->index_first < 0 && slash) {
            continue;   // The root category takes only files directly in the root.
        }
        const char *file = slash ? slash + 1 : rel;
        library_game_t *g = &c->games[c->count++];
        memset(g, 0, sizeof(*g));
        g->file_name = strdup(file);
        g->title = title_from_file_name(file);
        g->directory = c->directory;

        if (index_meta_count > 0) {
            index_meta_t *m = bsearch(rel, index_meta, index_meta_count, sizeof(index_meta_t), compare_index_meta);
            if (m) {
                g->genre = m->genre;
                g->developer = m->developer;
                g->year = m->year;
                g->players = m->players;
            }
        }

        int id = index_roms[k].cover;
        if (id >= 0 && id < index_cover_count) {
            g->cover_path = index_cover_path(id);
            g->cover_state = LIBRARY_COVER_FOUND;
            // metadata/C/I/D/J/... is a Japanese box, which is portrait.
            const char *cover = index_covers[id];
            with_cover++;
            if (strncmp(cover, "metadata/", 9) == 0 && strlen(cover) > 16 && cover[15] == 'J' && cover[16] == '/') {
                japan++;
            }
        }
    }

    qsort(c->games, c->count, sizeof(library_game_t), compare_games);
    c->portrait = with_cover > 0 && (japan * 2 > with_cover);
    c->filter_field = -1;
    c->shown = c->count;
    c->loaded = true;
}

static void load_folder_games (library_category_t *c) {
    if (c->from_index) {
        load_index_games(c);
        return;
    }

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
            if ((c->count & 15) == 0) {
                sound_bgm_keep_alive();     // A big folder takes a while: keep the music going.
            }
        }
        result = dir_findnext(path_get(c->directory), &info);
    }

    qsort(c->games, c->count, sizeof(library_game_t), compare_games);
    c->portrait = detect_portrait(c);
    c->filter_field = -1;
    c->shown = c->count;
    c->loaded = true;
}

/**
 * @brief A game's identity for duplicate detection: the 3-character game code in its cover's folder
 * (metadata/N/D/O/... -> "NDO"). False for covers matched by name or without a cover.
 */
static bool game_identity (const library_game_t *g, char key[4]) {
    if (!g->cover_path) {
        return false;
    }
    const char *m = strstr(g->cover_path, "/metadata/");
    if (!m || strncmp(m, "/metadata/names/", 16) == 0) {
        return false;
    }
    m += strlen("/metadata/");
    if (strlen(m) < 6 || m[1] != '/' || m[3] != '/' || m[5] != '/') {
        return false;
    }
    key[0] = m[0];
    key[1] = m[2];
    key[2] = m[4];
    key[3] = '\0';
    return true;
}

/** @brief Whether a folder holds ROM hacks: its name contains "hack", in any case ("N64 Hacks", "ROM hacks"). */
static bool is_hack_folder (path_t *directory) {
    const char *name = path_last_get(directory);
    for (const char *p = name; p && *p; p++) {
        if (strncasecmp(p, "hack", 4) == 0) {
            return true;
        }
    }
    return false;
}

/** @brief Preference among regional copies: lower is better (USA, Europe, Japan, then the rest). */
static int region_rank (path_t *directory) {
    const char *dir = path_get(directory);
    if (strstr(dir, "USA")) {
        return 0;
    }
    if (strstr(dir, "Europe")) {
        return 1;
    }
    if (strstr(dir, "Japan")) {
        return 2;
    }
    return 3;
}

/**
 * @brief Whether another copy of game i in a different folder is preferred over it.
 * Copies in the same folder (an original and its hack, say) are all kept. keys/ranks are
 * precomputed so the all-pairs check only compares numbers.
 */
static bool has_better_copy (library_game_t *games, const uint32_t *keys, const int *ranks, int count, int i) {
    if (keys[i] == 0) {
        return false;
    }
    for (int j = 0; j < count; j++) {
        if (j == i || keys[j] != keys[i] || games[j].directory == games[i].directory) {
            continue;
        }
        if (ranks[j] < ranks[i] || (ranks[j] == ranks[i] && j < i)) {
            return true;
        }
    }
    return false;
}

static void load_all_games (void) {
    int total = 0;
    for (int i = 0; i < folder_count; i++) {
        if (folders[i].folder_hidden) {
            continue;
        }
        if (!folders[i].loaded) {
            load_folder_games(&folders[i]);
        }
        total += folders[i].count;
    }

    // Every game, for spotting regional duplicates.
    library_game_t *everything = malloc((total > 0 ? total : 1) * sizeof(library_game_t));
    int n = 0;
    for (int i = 0; i < folder_count; i++) {
        for (int j = 0; !folders[i].folder_hidden && j < folders[i].count; j++) {
            everything[n++] = folders[i].games[j];
        }
    }

    uint32_t *keys = malloc((n > 0 ? n : 1) * sizeof(uint32_t));
    int *ranks = malloc((n > 0 ? n : 1) * sizeof(int));
    for (int k = 0; k < n; k++) {
        char key[4];
        // Hacks reuse their original's game code but are different games: never treat them as copies.
        bool hack = is_hack_folder(everything[k].directory);
        keys[k] = (!hack && game_identity(&everything[k], key)) ? ((uint32_t)key[0] << 16) | ((uint32_t)key[1] << 8) | (uint32_t)key[2] : 0;
        ranks[k] = region_rank(everything[k].directory);
    }

    // Shallow copies: names and directories stay owned by the folder entries.
    all_games.games = malloc((total > 0 ? total : 1) * sizeof(library_game_t));
    all_games.count = 0;
    for (int k = 0; k < n; k++) {
        if (one_per_game && has_better_copy(everything, keys, ranks, n, k)) {
            continue;
        }
        {
            library_game_t *g = &all_games.games[all_games.count++];
            *g = everything[k];
            // Each list owns its cover paths.
            g->cover_path = g->cover_path ? strdup(g->cover_path) : NULL;
            if (g->cover_state == LIBRARY_COVER_FOUND && !g->cover_path) {
                g->cover_state = LIBRARY_COVER_UNKNOWN;
            }
        }
    }
    free(keys);
    free(ranks);
    free(everything);
    qsort(all_games.games, all_games.count, sizeof(library_game_t), compare_games);
    all_games.portrait = false;
    all_games.filter_field = -1;
    all_games.shown = all_games.count;
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

/**
 * @brief Build the categories from the cover index (folders are the first part of each ROM path).
 *
 * @return false when there is no usable index.
 */
static bool scan_from_index (void) {
    index_load();
    if (!index_roms || index_rom_count == 0 || index_lists_disabled) {
        return false;
    }

    int capacity = 8;
    folders = malloc(capacity * sizeof(library_category_t));
    folder_count = 0;
    bool root_files = false;
    path_t *root = path_init(storage_prefix, "/");

    // Entries are sorted byte-wise, so each folder's ROMs are one contiguous run.
    for (int i = 0; i < index_rom_count; ) {
        const char *rel = index_roms[i].rom;
        const char *slash = strchr(rel, '/');
        if (!slash) {
            root_files = true;
            i++;
            continue;
        }
        size_t len = slash - rel;
        int j = i + 1;
        while (j < index_rom_count && strncmp(index_roms[j].rom, rel, len + 1) == 0) {
            j++;
        }
        if (folder_count == capacity) {
            capacity *= 2;
            folders = realloc(folders, capacity * sizeof(library_category_t));
        }
        library_category_t *c = &folders[folder_count++];
        memset(c, 0, sizeof(*c));
        c->name = malloc(len + 1);
        memcpy(c->name, rel, len);
        c->name[len] = '\0';
        c->directory = path_clone_push(root, c->name);
        c->from_index = true;
        c->index_first = i;
        c->index_count = j - i;
        i = j;
    }

    qsort(folders, folder_count, sizeof(library_category_t), compare_categories);
    strip_common_prefix();

    if (root_files) {
        folders = realloc(folders, (folder_count + 1) * sizeof(library_category_t));
        library_category_t *c = &folders[folder_count++];
        memset(c, 0, sizeof(*c));
        c->name = strdup(ROOT_CATEGORY_NAME);
        c->directory = path_clone(root);
        c->from_index = true;
        c->index_first = -1;
    }
    path_free(root);
    return true;
}

/* Folder order and hidden folders: "+Name" (shown) or "-Name" (hidden) per line, in order. */
static void folders_update_visible (void) {
    free(visible);
    visible = malloc((folder_count > 0 ? folder_count : 1) * sizeof(int));
    visible_count = 0;
    for (int i = 0; i < folder_count; i++) {
        if (!folders[i].folder_hidden) {
            visible[visible_count++] = i;
        }
    }
}

static void folders_load_order (void) {
    path_t *file = path_init(storage_prefix, FOLDERS_FILE);
    FILE *f = fopen(path_get(file), "r");
    path_free(file);
    if (f) {
        // Listed folders go first, in the saved order; new ones keep their place after them.
        int placed = 0;
        char line[256];
        while (fgets(line, sizeof(line), f) && placed < folder_count) {
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0] != '+' && line[0] != '-') {
                continue;
            }
            for (int i = placed; i < folder_count; i++) {
                if (strcmp(folders[i].name, line + 1) == 0) {
                    library_category_t c = folders[i];
                    memmove(&folders[placed + 1], &folders[placed], (i - placed) * sizeof(library_category_t));
                    folders[placed] = c;
                    folders[placed].folder_hidden = (line[0] == '-');
                    placed++;
                    break;
                }
            }
        }
        fclose(f);
    }
    folders_update_visible();
    if (visible_count == 0 && folder_count > 0) {
        // Never hide everything.
        for (int i = 0; i < folder_count; i++) {
            folders[i].folder_hidden = false;
        }
        folders_update_visible();
    }
}

void library_folders_save (void) {
    path_t *file = path_init(storage_prefix, FOLDERS_FILE);
    FILE *f = fopen(path_get(file), "w");
    path_free(file);
    if (f) {
        for (int i = 0; i < folder_count; i++) {
            fprintf(f, "%c%s\n", folders[i].folder_hidden ? '-' : '+', folders[i].name);
        }
        fclose(f);
    }
}

static void folders_changed (void) {
    folders_update_visible();

    // All Games is built from the shown folders.
    if (all_games.games) {
        for (int i = 0; i < all_games.count; i++) {
            free(all_games.games[i].cover_path);
        }
        free(all_games.games);
    }
    memset(&all_games, 0, sizeof(all_games));
    all_games.name = ALL_GAMES_NAME;
}

int library_folder_count (void) {
    return folder_count;
}

const char *library_folder_name (int index) {
    return (index >= 0 && index < folder_count) ? folders[index].name : NULL;
}

bool library_folder_hidden (int index) {
    return index >= 0 && index < folder_count && folders[index].folder_hidden;
}

bool library_folder_set_hidden (int index, bool hidden) {
    if (index < 0 || index >= folder_count || folders[index].folder_hidden == hidden) {
        return true;
    }
    if (hidden && visible_count <= 1) {
        return false;
    }
    folders[index].folder_hidden = hidden;
    folders_changed();
    return true;
}

int library_folder_move (int index, int delta) {
    int to = index + delta;
    if (index < 0 || index >= folder_count || to < 0 || to >= folder_count) {
        return index;
    }
    library_category_t c = folders[index];
    folders[index] = folders[to];
    folders[to] = c;
    folders_changed();
    return to;
}

void library_init (menu_t *menu) {
    const char *root_setting = menu->settings.launcher_library_root;
    if (!root_setting || root_setting[0] == '\0') {
        root_setting = "/";
    }
    if (scanned && scanned_root && strcmp(scanned_root, root_setting) == 0 &&
        categories_enabled == menu->settings.launcher_categories_enabled &&
        one_per_game == menu->settings.launcher_one_per_game) {
        return;
    }

    library_free();

    storage_prefix = menu->storage_prefix;
    categories_enabled = menu->settings.launcher_categories_enabled;
    one_per_game = menu->settings.launcher_one_per_game;
    scanned_root = strdup(root_setting);

    // The cover index lists ROMs relative to the card root, so it only stands in for a root library.
    bool indexed = (strcmp(scanned_root, "/") == 0) && scan_from_index();
    if (!indexed) {
        path_t *root = path_init(storage_prefix, scanned_root);
        scan_folders(root);
        path_free(root);
    }
    folders_load_order();

    memset(&all_games, 0, sizeof(all_games));
    all_games.name = ALL_GAMES_NAME;
    scanned = true;
}

int library_category_count (void) {
    if (!categories_enabled) {
        return visible_count > 0 ? 1 : 0;
    }
    return visible_count;
}

const char *library_category_name (int index) {
    if (!categories_enabled) {
        return (index == 0 && visible_count > 0) ? ALL_GAMES_NAME : NULL;
    }
    return (index >= 0 && index < visible_count) ? folders[visible[index]].name : NULL;
}

int library_category_find (const char *name) {
    if (!name) {
        return -1;
    }
    if (!categories_enabled) {
        return visible_count > 0 ? 0 : -1;
    }
    for (int i = 0; i < visible_count; i++) {
        if (strcasecmp(folders[visible[i]].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

bool library_category_is_quick (int index) {
    if (!categories_enabled) {
        return all_games.loaded;
    }
    if (index < 0 || index >= visible_count) {
        return false;
    }
    library_category_t *c = &folders[visible[index]];
    return c->loaded || c->from_index;
}

/* Hidden ROMs: full paths, one per line in HIDDEN_FILE. */
static char *hidden_paths[MAX_HIDDEN];
static int hidden_total = 0;
static bool hidden_loaded = false;
static uint32_t hidden_generation = 1;

static void hidden_load (void) {
    if (hidden_loaded || !storage_prefix) {
        return;
    }
    hidden_loaded = true;
    path_t *file = path_init(storage_prefix, HIDDEN_FILE);
    FILE *f = fopen(path_get(file), "r");
    path_free(file);
    if (!f) {
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), f) && hidden_total < MAX_HIDDEN) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0]) {
            hidden_paths[hidden_total++] = strdup(line);
        }
    }
    fclose(f);
}

static void hidden_save (void) {
    path_t *file = path_init(storage_prefix, HIDDEN_FILE);
    FILE *f = fopen(path_get(file), "w");
    path_free(file);
    if (f) {
        for (int i = 0; i < hidden_total; i++) {
            fprintf(f, "%s\n", hidden_paths[i]);
        }
        fclose(f);
    }
}

static int hidden_index (const char *full) {
    for (int i = 0; i < hidden_total; i++) {
        if (strcmp(hidden_paths[i], full) == 0) {
            return i;
        }
    }
    return -1;
}

bool library_game_hidden (library_game_t *game) {
    if (game->hidden_generation != hidden_generation) {
        hidden_load();
        game->hidden = false;
        if (hidden_total > 0) {
            path_t *path = library_game_path(game);
            game->hidden = hidden_index(path_get(path)) >= 0;
            path_free(path);
        }
        game->hidden_generation = hidden_generation;
    }
    return game->hidden;
}

void library_set_hidden (menu_t *menu, path_t *rom_path, bool hidden) {
    if (!storage_prefix) {
        storage_prefix = menu->storage_prefix;
    }
    hidden_load();
    int index = hidden_index(path_get(rom_path));
    if (hidden && index < 0 && hidden_total < MAX_HIDDEN) {
        hidden_paths[hidden_total++] = strdup(path_get(rom_path));
    } else if (!hidden && index >= 0) {
        free(hidden_paths[index]);
        hidden_paths[index] = hidden_paths[--hidden_total];
    } else {
        return;
    }
    hidden_save();
    hidden_generation++;
}

int library_hidden_count (menu_t *menu) {
    if (!storage_prefix) {
        storage_prefix = menu->storage_prefix;
    }
    hidden_load();
    return hidden_total;
}

const char *library_hidden_at (int index) {
    return (index >= 0 && index < hidden_total) ? hidden_paths[index] : NULL;
}

/* Sort order and filter used by compare_sorted (qsort has no context argument). */
static int sort_order = 0;
static int filter_field_now = -1;
static const char *filter_value_now = NULL;

/** @brief Letters and digits only, lower case: "Castlevania: Legacy" -> "castlevanialegacy". */
static void squash_text (const char *text, char *out, size_t size) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p && o + 1 < size; p++) {
        if (isalnum(*p)) {
            out[o++] = (char)tolower(*p);
        }
    }
    out[o] = '\0';
}

static char search_now[64];     /* The searched text, squashed, while a category is arranged */

static bool game_matches_filter (const library_game_t *g) {
    if (library_game_hidden((library_game_t *)g)) {
        return false;
    }
    if (filter_field_now < 0) {
        return true;
    }
    if (filter_field_now == LAUNCHER_FILTER_SEARCH) {
        char title[128];
        squash_text(g->title, title, sizeof(title));
        return strstr(title, search_now) != NULL;
    }
    char label[64];
    library_game_group((library_game_t *)g, filter_field_now, label, sizeof(label));
    return strcmp(label, filter_value_now) == 0;
}

/** @brief The key a game sorts by; NULL means unknown, which sorts last. */
static const char *sort_key (const library_game_t *g) {
    switch (sort_order) {
        case LAUNCHER_SORT_GENRE: return g->genre;
        case LAUNCHER_SORT_DEVELOPER: return g->developer;
        case LAUNCHER_SORT_YEAR: return g->year;
        default: return NULL;
    }
}

static int compare_sorted (const void *a, const void *b) {
    const library_game_t *ga = a;
    const library_game_t *gb = b;
    {
        // Games shown (not hidden, and kept by any filter) come first; only they are shown.
        bool ma = game_matches_filter(ga);
        bool mb = game_matches_filter(gb);
        if (ma != mb) {
            return ma ? -1 : 1;
        }
    }
    if (sort_order == LAUNCHER_SORT_PLAYERS) {
        // Most players first (party games together); unknown last.
        int pa = ga->players > 0 ? ga->players : -1;
        int pb = gb->players > 0 ? gb->players : -1;
        if (pa != pb) {
            return pb - pa;
        }
    } else if (sort_order != LAUNCHER_SORT_TITLE) {
        const char *ka = sort_key(ga);
        const char *kb = sort_key(gb);
        if (!ka != !kb) {
            return ka ? -1 : 1;
        }
        if (ka && kb) {
            int r = strcasecmp(ka, kb);
            if (r != 0) {
                return r;
            }
        }
    }
    return compare_titles(ga->title, gb->title);
}

bool library_arrange_category (library_category_t *category, int sort, int filter_field, const char *filter_value) {
    if (!category) {
        return false;
    }
    if (filter_field < 0 || !filter_value) {
        filter_field = -1;
        filter_value = "";
    }
    hidden_load();
    if (category->sort == sort && category->filter_field == filter_field &&
        (filter_field < 0 || strcmp(category->filter_value, filter_value) == 0) &&
        category->hidden_generation == hidden_generation) {
        return false;
    }
    sort_order = sort;
    filter_field_now = filter_field;
    filter_value_now = filter_value;
    squash_text(filter_value, search_now, sizeof(search_now));
    qsort(category->games, category->count, sizeof(library_game_t), compare_sorted);

    category->shown = 0;
    while (category->shown < category->count && game_matches_filter(&category->games[category->shown])) {
        category->shown++;
    }
    category->hidden_generation = hidden_generation;
    category->sort = sort;
    category->filter_field = filter_field;
    snprintf(category->filter_value, sizeof(category->filter_value), "%s", filter_value);
    filter_field_now = -1;
    return true;
}

static int compare_groups (const void *a, const void *b) {
    const library_group_t *ga = a;
    const library_group_t *gb = b;
    // "Unknown ..." goes last.
    bool ua = strncmp(ga->label, "Unknown", 7) == 0;
    bool ub = strncmp(gb->label, "Unknown", 7) == 0;
    if (ua != ub) {
        return ua ? 1 : -1;
    }
    return strcasecmp(ga->label, gb->label);
}

int library_category_groups (library_category_t *category, int field, library_group_t **groups) {
    int capacity = 32;
    int count = 0;
    library_group_t *list = malloc(capacity * sizeof(library_group_t));
    for (int i = 0; i < category->count; i++) {
        char label[64];
        library_game_group(&category->games[i], field, label, sizeof(label));
        int k = 0;
        while (k < count && strcmp(list[k].label, label) != 0) {
            k++;
        }
        if (k == count) {
            if (count == capacity) {
                capacity *= 2;
                list = realloc(list, capacity * sizeof(library_group_t));
            }
            snprintf(list[count].label, sizeof(list[count].label), "%s", label);
            list[count].count = 0;
            count++;
        }
        list[k].count++;
    }
    qsort(list, count, sizeof(library_group_t), compare_groups);
    *groups = list;
    return count;
}

static void details_text (const char *genre, const char *year, const char *developer, int players, char *buffer, size_t size) {
    buffer[0] = '\0';
    const char *parts[4];
    char players_text[16];
    int n = 0;
    if (genre) {
        parts[n++] = genre;
    }
    if (year) {
        parts[n++] = year;
    }
    if (developer) {
        parts[n++] = developer;
    }
    if (players > 0) {
        snprintf(players_text, sizeof(players_text), players == 1 ? "1 player" : "%d players", players);
        parts[n++] = players_text;
    }
    size_t used = 0;
    for (int i = 0; i < n && used < size; i++) {
        used += snprintf(buffer + used, size - used, "%s%s", i ? "   \xC2\xB7   " : "", parts[i]);
    }
}

void library_game_details (library_game_t *game, char *buffer, size_t size) {
    details_text(game->genre, game->year, game->developer, game->players, buffer, size);
}

/** @brief The index's game details line for a ROM, or NULL. */
static index_meta_t *meta_for_path (menu_t *menu, path_t *rom_path) {
    if (!storage_prefix) {
        storage_prefix = menu->storage_prefix;
    }
    index_load();
    if (index_meta_count == 0) {
        return NULL;
    }
    const char *rel = path_get(rom_path);
    size_t prefix_len = strlen(storage_prefix);
    if (strncmp(rel, storage_prefix, prefix_len) == 0) {
        rel += prefix_len;
    }
    while (*rel == '/') {
        rel++;
    }
    return bsearch(rel, index_meta, index_meta_count, sizeof(index_meta_t), compare_index_meta);
}

void library_details_for_path (menu_t *menu, path_t *rom_path, char *buffer, size_t size) {
    buffer[0] = '\0';
    index_meta_t *m = meta_for_path(menu, rom_path);
    if (m) {
        details_text(m->genre, m->year, m->developer, m->players, buffer, size);
    }
}

const char *library_publisher_for_path (menu_t *menu, path_t *rom_path) {
    index_meta_t *m = meta_for_path(menu, rom_path);
    return m ? m->publisher : NULL;
}

void library_game_group (library_game_t *game, int sort, char *buffer, size_t size) {
    switch (sort) {
        case LAUNCHER_SORT_GENRE:
            snprintf(buffer, size, "%s", game->genre ? game->genre : "Unknown genre");
            return;
        case LAUNCHER_SORT_DEVELOPER:
            snprintf(buffer, size, "%s", game->developer ? game->developer : "Unknown developer");
            return;
        case LAUNCHER_SORT_YEAR:
            snprintf(buffer, size, "%s", game->year ? game->year : "Unknown year");
            return;
        case LAUNCHER_SORT_PLAYERS:
            if (game->players > 0) {
                snprintf(buffer, size, game->players == 1 ? "1 player" : "%d players", game->players);
            } else {
                snprintf(buffer, size, "Unknown players");
            }
            return;
        default: {
            char c = sort_title(game->title)[0];
            if (c >= 'a' && c <= 'z') {
                c -= 'a' - 'A';
            }
            snprintf(buffer, size, "%c", (c >= 'A' && c <= 'Z') ? c : '#');
            return;
        }
    }
}

/** @brief Whether the cover index lists this file (rel = path relative to the card root). */
static bool index_has_rom (const char *rel) {
    return bsearch(rel, index_roms, index_rom_count, sizeof(index_rom_t), compare_index_rom) != NULL;
}

void library_check_new_games (int budget_us) {
    // Only meaningful when the game lists come from the index.
    if (new_check_done || !index_roms || index_lists_disabled || folder_count == 0 || !folders[0].from_index) {
        return;
    }
    uint64_t start = get_ticks_us();
    while (get_ticks_us() - start < (uint64_t)budget_us) {
        if (new_check_folder >= folder_count) {
            new_check_done = true;
            return;
        }
        library_category_t *c = &folders[new_check_folder];
        const char *dir = path_get(c->directory);
        int result = new_check_listing ? dir_findnext(dir, &new_check_entry) : dir_findfirst(dir, &new_check_entry);
        new_check_listing = true;
        if (result != 0) {
            new_check_listing = false;
            new_check_folder++;
            continue;
        }
        if (new_check_entry.d_type == DT_DIR || !is_rom_name(new_check_entry.d_name)) {
            continue;
        }
        // The folder part of the index path: the directory without "sd:/" (empty for the root).
        const char *folder = dir + strlen(storage_prefix);
        while (*folder == '/') {
            folder++;
        }
        char rel[512];
        snprintf(rel, sizeof(rel), "%s%s%s", folder, folder[0] ? "/" : "", new_check_entry.d_name);
        if (!index_has_rom(rel)) {
            new_game_count++;
        }
    }
}

int library_new_game_count (void) {
    return new_check_done ? new_game_count : 0;
}

void library_rescan (menu_t *menu) {
    index_lists_disabled = true;
    scanned = false;
    library_init(menu);
}

library_category_t *library_category (int index) {
    if (!categories_enabled) {
        if (index != 0 || visible_count == 0) {
            return NULL;
        }
        if (!all_games.loaded) {
            load_all_games();
        }
        return &all_games;
    }
    if (index < 0 || index >= visible_count) {
        return NULL;
    }
    library_category_t *c = &folders[visible[index]];
    if (!c->loaded) {
        load_folder_games(c);
    }
    return c;
}

path_t *library_game_path (library_game_t *game) {
    return path_clone_push(game->directory, game->file_name);
}

const char *library_game_cover (menu_t *menu, library_game_t *game) {
    if (game->cover_state == LIBRARY_COVER_UNKNOWN) {
        path_t *path = library_game_path(game);
        game->cover_path = find_cover(path);
        game->cover_state = game->cover_path ? LIBRARY_COVER_FOUND : LIBRARY_COVER_NONE;
        path_free(path);
    }
    return game->cover_path;
}

char *library_find_cover (menu_t *menu, path_t *rom_path) {
    if (!storage_prefix) {
        storage_prefix = menu->storage_prefix;
    }
    return find_cover(rom_path);
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
    free(visible);
    visible = NULL;
    visible_count = 0;

    free(scanned_root);
    scanned_root = NULL;
    scanned = false;

    new_check_folder = 0;
    new_check_listing = false;
    new_check_done = false;
    new_game_count = 0;

    index_free();
}
