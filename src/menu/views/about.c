/**
 * @file about.c
 * @brief Launcher "About this game" page: covers, details and the description from the metadata pack.
 * @ingroup view
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "views.h"
#include "../launcher_ui.h"
#include "../library.h"
#include "../rom_info.h"
#include "../sound.h"
#include "../theme.h"
#include "utils/fs.h"

#define TEXT_X              (LAUNCHER_X0 + 184)
#define TITLE_Y             (64)
#define DETAILS_Y           (100)
#define ACCESSORIES_Y       (122)
#define DESCRIPTION_Y0      (152)
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
static char accessories[160];
static bool accessories_warning = false;   /* Something the game needs that this console lacks */
static char *description = NULL;
static launcher_paragraph_t *paragraph = NULL;
static int scroll = 0;
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
 * @brief "USA · Rumble Pak · Controller Pak", from the ROM header and the menu's game database.
 * Expansion Pak notes only appear when they matter on this console.
 */
static void load_accessories (path_t *path) {
    accessories[0] = '\0';
    accessories_warning = false;
    rom_info_t info;
    memset(&info, 0, sizeof(info));
    if (rom_info_load_header(path, &info) != ROM_OK) {
        return;
    }
    const char *parts[9];
    int n = 0;
    const char *region = region_name(info.destination_code);
    if (region) {
        parts[n++] = region;
    }
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
    int max_scroll = paragraph ? launcher_paragraph_height(paragraph) - (DESCRIPTION_Y1 - DESCRIPTION_Y0) : 0;
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
        if (details[0]) {
            launcher_small(TEXT_X, DETAILS_Y, text_w, ALIGN_LEFT, STL_GRAY, details);
        }
        if (accessories[0]) {
            launcher_small(TEXT_X, details[0] ? ACCESSORIES_Y : DETAILS_Y, text_w, ALIGN_LEFT,
                accessories_warning ? STL_ACCENT : STL_GRAY, accessories);
        }
        if (paragraph) {
            launcher_paragraph_draw(paragraph, TEXT_X, DESCRIPTION_Y0 - scroll, DESCRIPTION_Y0, DESCRIPTION_Y1);
        } else {
            launcher_text(TEXT_X, DESCRIPTION_Y0, text_w, ALIGN_LEFT, STL_GRAY, "No description on the SD card");
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
    load_accessories(rom);
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
