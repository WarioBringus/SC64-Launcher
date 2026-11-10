#include <stdio.h>
#include <string.h>
#include "views.h"
#include "../launcher_ui.h"
#include "../sound.h"


static void process (menu_t *menu) {
    if (menu->actions.back || (menu->settings.launcher_enabled && menu->actions.enter)) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER;
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    if (menu->settings.launcher_enabled) {
        // A short message is the card's title; a longer one goes under a plain title.
        launcher_set_widescreen(menu->settings.launcher_widescreen);
        launcher_draw_background();
        const char *message = menu->error_message ? menu->error_message : "Something went wrong";
        // Its first sentence is the card's title, the rest under it; one long sentence goes under a plain title.
        char title[64];
        const char *rest = NULL;
        const char *stop = strstr(message, ". ");
        const char *line = strchr(message, '\n');
        if (line && (!stop || line < stop)) {
            stop = line;
        }
        if (stop && stop - message < (int)sizeof(title) - 1) {
            snprintf(title, sizeof(title), "%.*s", (int)(stop - message) + (*stop == '.' ? 1 : 0), message);
            rest = stop + 1;
            while (*rest == ' ' || *rest == '\n') {
                rest++;
            }
        } else if (strlen(message) <= 40) {
            snprintf(title, sizeof(title), "%s", message);
        } else {
            snprintf(title, sizeof(title), "Something didn't work");
            rest = message;
        }
        static const launcher_hint_t ok[] = {
            { LAUNCHER_BUTTON_A, "A", "OK" },
        };
        launcher_draw_dialog(title, rest, ok, 1);
        rdpq_detach_show();
        return;
    }

    ui_components_background_draw();

    if (menu->error_message) {
        ui_components_messagebox_draw(menu->error_message);
    } else {
        ui_components_messagebox_draw("Unspecified error");
    }

    rdpq_detach_show();
}

static void deinit (menu_t *menu) {
    menu->error_message = NULL;
    menu->flashcart_err = FLASHCART_OK;
}


void view_error_init (menu_t *menu) {
    if (menu->flashcart_err != FLASHCART_OK) {
        debugf(
            "Flashcart error [%d]: %s\n",
            menu->flashcart_err,
            flashcart_convert_error_message(menu->flashcart_err)
        );
    }
}

void view_error_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);

    if (menu->next_mode != MENU_MODE_ERROR) {
        deinit(menu);
    }
}

void menu_show_error (menu_t *menu, char *error_message) {
    sound_play_effect(SFX_ERROR);
    menu->next_mode = MENU_MODE_ERROR;
    menu->error_message = error_message;
}
