/**
 * @file about.c
 * @brief Launcher "About this game" page: covers, details and the description from the metadata pack.
 * @ingroup view
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "views.h"
#include "../cart_load.h"
#include "../launcher_ui.h"
#include "../library.h"
#include "../rom_info.h"
#include "../sound.h"
#include "../theme.h"
#include "utils/fs.h"

#define TEXT_X              (LAUNCHER_X0 + 184)
#define TITLE_Y             (64)
#define DETAILS_Y           (100)
#define DETAILS_LINE_H      (22)
#define DESCRIPTION_GAP     (8)
#define DESCRIPTION_Y1      (408)
#define COVER_Y             (66)
#define BACK_COVER_GAP      (16)
#define SCROLL_STEP         (20)
/* Long enough for the pack's descriptions, short enough to stay under rdpq's 2048-pixel paragraph limit. */
#define MAX_DESCRIPTION     (3200)

static const launcher_hint_t hints_about[] = {
    { LAUNCHER_BUTTON_A, "A", "Play" },
    { LAUNCHER_BUTTON_OTHER, "R", "Options" },
    { LAUNCHER_BUTTON_C_UP_DOWN, "C", "Scroll" },
    { LAUNCHER_BUTTON_B, "B", "Back" },
};

static path_t *rom = NULL;
static char *title = NULL;
static char *cover = NULL;
static char *back_cover = NULL;
static char details[160];
static char origin[96];         /* "USA · Published by Acclaim" */
static char accessories[160];
static bool accessories_warning = false;   /* Something the game needs that this console lacks */
static char save_info[48];      /* "Last saved Nov 1, 2026" */
static char *description = NULL;
static launcher_paragraph_t *paragraph = NULL;
static int scroll = 0;
static int description_y0 = 0;      /* Below however many detail lines there are */
static menu_mode_t back_mode = MENU_MODE_HOME;


static void release (void) {
    path_free(rom);
    rom = NULL;
    free(title);
    title = NULL;
    free(cover);
    cover = NULL;
    free(back_cover);
    back_cover = NULL;
    free(description);
    description = NULL;
    launcher_paragraph_free(paragraph);
    paragraph = NULL;
}

/** @brief Read a small text file (NULL if missing). */
static char *read_text (const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    char *text = malloc(MAX_DESCRIPTION + 1);
    size_t n = text ? fread(text, 1, MAX_DESCRIPTION, f) : 0;
    fclose(f);
    if (!text || n == 0) {
        free(text);
        return NULL;
    }
    // A cut-off description must not end in the middle of a UTF-8 character.
    // Dropping trailing non-ASCII bytes is enough (at worst it also drops one whole character).
    if (n == MAX_DESCRIPTION) {
        while (n > 0 && (((unsigned char)text[n - 1]) & 0x80)) {
            n--;
        }
    }
    text[n] = '\0';
    // Normalize line endings; keep paragraph breaks.
    char *o = text;
    for (char *p = text; *p; p++) {
        if (*p != '\r') {
            *o++ = *p;
        }
    }
    *o = '\0';
    return text;
}

/**
 * @brief Look for metadata next to the cover: the cover's folder, then its parent
 * (descriptions live in metadata/C/I/D/, covers often in metadata/C/I/D/R/).
 */
static char *find_beside_cover (const char *file_name) {
    if (!cover || !strstr(cover, "/metadata/") || strstr(cover, "/metadata/names/")) {
        return NULL;
    }
    char *dir = strdup(cover);
    for (int level = 0; level < 2; level++) {
        char *slash = strrchr(dir, '/');
        if (!slash) {
            break;
        }
        *slash = '\0';
        size_t len = strlen(dir) + 1 + strlen(file_name) + 1;
        char *candidate = malloc(len);
        snprintf(candidate, len, "%s/%s", dir, file_name);
        if (file_exists(candidate)) {
            free(dir);
            return candidate;
        }
        free(candidate);
    }
    free(dir);
    return NULL;
}

/** @brief Where a game is from, by the last letter of its game code. */
static const char *region_name (rom_destination_type_t destination) {
    switch (destination) {
        case MARKET_JAPANESE_MULTI: return "Japan & USA";
        case MARKET_BRAZILIAN: return "Brazil";
        case MARKET_CHINESE: return "China";
        case MARKET_GERMAN: return "Germany";
        case MARKET_NORTH_AMERICA: return "USA";
        case MARKET_FRENCH: return "France";
        case MARKET_DUTCH: return "Netherlands";
        case MARKET_ITALIAN: return "Italy";
        case MARKET_JAPANESE: return "Japan";
        case MARKET_KOREAN: return "Korea";
        case MARKET_CANADIAN: return "Canada";
        case MARKET_SPANISH: return "Spain";
        case MARKET_AUSTRALIAN: return "Australia";
        case MARKET_SCANDINAVIAN: return "Scandinavia";
        case MARKET_GATEWAY64_NTSC:
        case MARKET_GATEWAY64_PAL: return "Gateway 64";
        case MARKET_EUROPEAN_BASIC:
        case MARKET_OTHER_X:
        case MARKET_OTHER_Y: return "Europe";
        default: return NULL;
    }
}

/**
 * @brief "Last saved Nov 1, 2026" from the save file's date (the menu writes it back after each play),
 * "Save file found" when the date can't be trusted, or nothing without a save.
 */
static void load_save_info (menu_t *menu, path_t *rom_path) {
    static const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    save_info[0] = '\0';
    // Saves live in a "saves" folder beside the ROM, or beside it directly; try the configured place first.
    for (int attempt = 0; attempt < 2; attempt++) {
        bool in_folder = (attempt == 0) == menu->settings.use_saves_folder;
        path_t *save = path_clone(rom_path);
        path_ext_replace(save, "sav");
        if (in_folder) {
            path_push_subdir(save, SAVE_DIRECTORY_NAME);
        }
        struct stat st;
        bool found = (stat(path_get(save), &st) == 0);
        path_free(save);
        if (!found) {
            continue;
        }
        struct tm *t = localtime(&st.st_mtime);
        if (t && t->tm_year + 1900 >= 2000 && t->tm_mon >= 0 && t->tm_mon < 12) {
            snprintf(save_info, sizeof(save_info), "Last saved %s %d, %d", months[t->tm_mon], t->tm_mday, t->tm_year + 1900);
        } else {
            snprintf(save_info, sizeof(save_info), "Save file found");
        }
        return;
    }
}

/**
 * @brief Region and publisher ("USA · Published by Acclaim"), then accessories ("Rumble Pak · Controller Pak")
 * from the ROM header and the menu's game database.
 * Expansion Pak notes only appear when they matter on this console.
 */
static void load_accessories (menu_t *menu, path_t *path) {
    origin[0] = '\0';
    accessories[0] = '\0';
    accessories_warning = false;
    const char *publisher = library_publisher_for_path(menu, path);
    rom_info_t info;
    memset(&info, 0, sizeof(info));
    const char *region = NULL;
    if (rom_info_load_header(path, &info) == ROM_OK) {
        region = region_name(info.destination_code);
    }
    snprintf(origin, sizeof(origin), "%s%s%s%s", region ? region : "", region && publisher ? "   \xC2\xB7   " : "",
        publisher ? "Published by " : "", publisher ? publisher : "");
    if (!region) {
        return;     // No header read: no accessories either.
    }
    const char *parts[8];
    int n = 0;
    if (info.features.expansion_pak == EXPANSION_PAK_REQUIRED && !is_memory_expanded()) {
        parts[n++] = "Needs Expansion Pak";
        accessories_warning = true;
    } else if (info.features.expansion_pak == EXPANSION_PAK_RECOMMENDED && !is_memory_expanded()) {
        parts[n++] = "Better with Expansion Pak";
    } else if (info.features.expansion_pak == EXPANSION_PAK_FAULTY && is_memory_expanded()) {
        parts[n++] = "Has issues with Expansion Pak";
        accessories_warning = true;
    }
    if (info.features.rumble_pak) {
        parts[n++] = "Rumble Pak";
    }
    if (info.features.controller_pak) {
        parts[n++] = "Controller Pak";
    }
    if (info.features.transfer_pak) {
        parts[n++] = "Transfer Pak";
    }
    if (info.features.voice_recognition_unit) {
        parts[n++] = "Voice Recognition Unit";
    }
    if (info.features.real_time_clock) {
        parts[n++] = "Clock";
    }
    size_t used = 0;
    for (int i = 0; i < n && used < sizeof(accessories); i++) {
        used += snprintf(accessories + used, sizeof(accessories) - used, "%s%s", i ? "   \xC2\xB7   " : "", parts[i]);
    }
}

static void play (menu_t *menu, bool quick) {
    path_free(menu->load.launcher_rom_path);
    menu->load.launcher_rom_path = path_clone(rom);
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    menu->load.quick_start = quick;
    sound_play_effect(SFX_ENTER);
    launcher_leave(menu, MENU_MODE_LOAD_ROM);
}

static void process (menu_t *menu) {
    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        launcher_transition(menu->mode, back_mode);
        menu->next_mode = back_mode;
        return;
    }
    if (!rom) {
        return;
    }
    if (menu->actions.enter) {
        play(menu, menu->settings.launcher_quick_launch);
    } else if (menu->actions.shoulder_r) {
        play(menu, false);
    } else if (menu->actions.go_up) {
        scroll -= SCROLL_STEP * (menu->actions.go_fast ? 4 : 1);
    } else if (menu->actions.go_down) {
        scroll += SCROLL_STEP * (menu->actions.go_fast ? 4 : 1);
    }
    int max_scroll = paragraph ? launcher_paragraph_height(paragraph) - (DESCRIPTION_Y1 - description_y0) : 0;
    if (scroll > max_scroll) {
        scroll = max_scroll;
    }
    if (scroll < 0) {
        scroll = 0;
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    launcher_covers_begin_frame();
    launcher_draw_background();
    launcher_transition_content_begin(0);

    if (!rom) {
        launcher_text(LAUNCHER_X0, 200, LAUNCHER_WIDTH, ALIGN_CENTER, STL_GRAY, "No game selected");
    } else {
        // Japanese boxes (metadata/C/I/D/J/) are portrait; asking for a pack size keeps the front cover fast.
        bool portrait = cover && strstr(cover, "/J/boxart_front.png");
        int cw = portrait ? LAUNCHER_COVER_H : LAUNCHER_COVER_W;
        int ch = portrait ? LAUNCHER_COVER_W : LAUNCHER_COVER_H;
        bool pending;
        surface_t *front = launcher_cover_get(cover, cw, ch, &pending);
        launcher_draw_tile(LAUNCHER_X0, COVER_Y, cw, ch, front, pending, title, false, theme_current()->bg);

        if (back_cover) {
            surface_t *back = launcher_cover_get(back_cover, cw, ch, &pending);
            launcher_draw_tile(LAUNCHER_X0, COVER_Y + ch + BACK_COVER_GAP, cw, ch, back, pending, NULL, false, theme_current()->bg);
        }

        int text_w = LAUNCHER_X1 - TEXT_X;
        launcher_title(TEXT_X, TITLE_Y, text_w, ALIGN_LEFT, STL_DEFAULT, title);
        int y = DETAILS_Y;
        if (details[0]) {
            launcher_small(TEXT_X, y, text_w, ALIGN_LEFT, STL_GRAY, details);
            y += DETAILS_LINE_H;
        }
        if (origin[0]) {
            launcher_small(TEXT_X, y, text_w, ALIGN_LEFT, STL_GRAY, origin);
            y += DETAILS_LINE_H;
        }
        if (accessories[0]) {
            launcher_small(TEXT_X, y, text_w, ALIGN_LEFT, accessories_warning ? STL_ACCENT : STL_GRAY, accessories);
            y += DETAILS_LINE_H;
        }
        if (save_info[0]) {
            launcher_small(TEXT_X, y, text_w, ALIGN_LEFT, STL_GRAY, save_info);
        }
        if (paragraph) {
            launcher_paragraph_draw(paragraph, TEXT_X, description_y0 - scroll, description_y0, DESCRIPTION_Y1);
        } else {
            launcher_text(TEXT_X, description_y0, text_w, ALIGN_LEFT, STL_GRAY, "No description on the SD card");
        }
    }

    launcher_draw_footer(hints_about, (int)(sizeof(hints_about) / sizeof(hints_about[0])));

    rdpq_detach_show();
}


void view_about_init (menu_t *menu) {
    // Coming back from the ROM screen keeps the same game; a new visit from the launcher starts over.
    if (menu->launcher_return_mode == MENU_MODE_HOME || menu->launcher_return_mode == MENU_MODE_LIBRARY) {
        back_mode = menu->launcher_return_mode;
    }
    path_t *requested = launcher_about_rom();
    if (!requested) {
        return;
    }
    release();
    rom = path_clone(requested);
    title = library_title_from_path(rom);
    cover = library_find_cover(menu, rom, title);
    library_details_for_path(menu, rom, details, sizeof(details));
    load_accessories(menu, rom);
    load_save_info(menu, rom);
    description_y0 = DETAILS_Y + DESCRIPTION_GAP + DETAILS_LINE_H *
        ((details[0] ? 1 : 0) + (origin[0] ? 1 : 0) + (accessories[0] ? 1 : 0) + (save_info[0] ? 1 : 0));
    if (cover) {
        char *front_name = strstr(cover, "boxart_front.png");
        if (front_name) {
            size_t len = strlen(cover) + 1;
            back_cover = malloc(len);
            memcpy(back_cover, cover, front_name - cover);
            strcpy(back_cover + (front_name - cover), "boxart_back.png");
            if (!file_exists(back_cover)) {
                free(back_cover);
                back_cover = NULL;
            }
        }
    }
    char *description_path = find_beside_cover("description.txt");
    if (description_path) {
        description = read_text(description_path);
        free(description_path);
    }
    if (description) {
        launcher_set_widescreen(menu->settings.launcher_widescreen);
        paragraph = launcher_paragraph_build(description, LAUNCHER_X1 - TEXT_X);
    }
    scroll = 0;
}

void view_about_display (menu_t *menu, surface_t *display) {
    launcher_set_widescreen(menu->settings.launcher_widescreen);
    launcher_set_cartridge_art(false);

    process(menu);

    draw(menu, display);
}
