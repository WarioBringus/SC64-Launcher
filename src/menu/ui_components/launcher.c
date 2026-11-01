/**
 * @file launcher.c
 * @brief Drawing and navigation helpers shared by the launcher views.
 * @ingroup ui_components
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../launcher_ui.h"
#include "../sound.h"
#include "../theme.h"
#include "../ui_components.h"

#define HEADER_TAB_SPACING      (24)
#define HEADER_UNDERLINE_Y      (LAUNCHER_HEADER_Y + 21)
#define TEXT_HEIGHT             (20)


int launcher_text (int x, int y, int width, rdpq_align_t align, menu_font_style_t style, const char *fmt, ...) {
    char buffer[256];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, va);
    va_end(va);

    rdpq_textmetrics_t metrics = rdpq_text_printn(
        &(rdpq_textparms_t) {
            .style_id = style,
            .width = width,
            .height = TEXT_HEIGHT,
            .align = align,
            .valign = VALIGN_TOP,
            .wrap = (width > 0) ? WRAP_ELLIPSES : WRAP_NONE,
        },
        FNT_DEFAULT,
        x,
        y,
        buffer,
        strlen(buffer)
    );
    return (int)(metrics.advance_x);
}

void launcher_draw_header (menu_t *menu, launcher_tab_t active) {
    ui_components_background_draw();

    static const char *tabs[] = { "Home", "Library" };
    int x = LAUNCHER_X0;
    for (int i = 0; i < 2; i++) {
        bool on = (i == (int)active);
        int w = launcher_text(x, LAUNCHER_HEADER_Y, 0, ALIGN_LEFT, on ? STL_DEFAULT : STL_GRAY, "%s", tabs[i]);
        if (on) {
            ui_components_box_draw(x, HEADER_UNDERLINE_Y, x + w, HEADER_UNDERLINE_Y + 3, theme_current()->accent2);
        }
        x += w + HEADER_TAB_SPACING;
    }

    if (menu->current_time >= 0) {
        char clock[16];
        struct tm *t = localtime(&menu->current_time);
        strftime(clock, sizeof(clock), "%I:%M %p", t);
        launcher_text(LAUNCHER_X0, LAUNCHER_HEADER_Y, LAUNCHER_WIDTH, ALIGN_RIGHT, STL_GRAY, "%s", clock[0] == '0' ? clock + 1 : clock);
    }
}

void launcher_draw_footer (const char *hints) {
    ui_components_box_draw(LAUNCHER_X0, LAUNCHER_FOOTER_Y, LAUNCHER_X1, LAUNCHER_FOOTER_Y + 1, theme_current()->line);
    launcher_text(LAUNCHER_X0, LAUNCHER_FOOTER_Y + 10, LAUNCHER_WIDTH, ALIGN_RIGHT, STL_DEFAULT, "%s", hints);
}

void launcher_draw_ring (int x0, int y0, int x1, int y1) {
    color_t c = theme_current()->accent;
    int t = LAUNCHER_RING;
    ui_components_box_draw(x0 - t, y0 - t, x1 + t, y0, c);
    ui_components_box_draw(x0 - t, y1, x1 + t, y1 + t, c);
    ui_components_box_draw(x0 - t, y0, x0, y1, c);
    ui_components_box_draw(x1, y0, x1 + t, y1, c);
}

void launcher_draw_tile (int x, int y, int w, int h, surface_t *image, bool pending, const char *title, bool selected) {
    if (image) {
        if (image->width < w || image->height < h) {
            ui_components_box_draw(x, y, x + w, y + h, theme_current()->panel);
        }
        rdpq_mode_push();
            rdpq_set_mode_copy(false);
            rdpq_tex_blit(image, x + (w - image->width) / 2, y + (h - image->height) / 2, NULL);
        rdpq_mode_pop();
    } else {
        ui_components_box_draw(x, y, x + w, y + h, theme_current()->panel);
        if (!pending && title) {
            rdpq_text_printn(
                &(rdpq_textparms_t) {
                    .style_id = STL_GRAY,
                    .width = w - 8,
                    .height = h - 8,
                    .align = ALIGN_CENTER,
                    .valign = VALIGN_CENTER,
                    .wrap = WRAP_WORD,
                },
                FNT_DEFAULT,
                x + 4,
                y + 4,
                title,
                strlen(title)
            );
        }
    }
    if (selected) {
        launcher_draw_ring(x, y, x + w, y + h);
    }
}

void launcher_draw_cover_scaled (surface_t *image, int x, int y, int w, int h) {
    float scale = fminf((float)w / image->width, (float)h / image->height);
    int dw = (int)(image->width * scale);
    int dh = (int)(image->height * scale);
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_tex_blit(image, x + (w - dw) / 2, y + (h - dh) / 2, &(rdpq_blitparms_t) {
            .scale_x = scale,
            .scale_y = scale,
        });
    rdpq_mode_pop();
}

void launcher_leave (menu_t *menu, menu_mode_t next_mode) {
    menu->launcher_return_mode = menu->mode;
    menu->next_mode = next_mode;
}

static void open_screen (menu_t *menu, void *arg) {
    launcher_leave(menu, (menu_mode_t)(arg));
}

static void open_file_browser (menu_t *menu, void *arg) {
    (void)arg;
    // The file browser is a destination of its own; screens opened from it return to it.
    menu->launcher_return_mode = MENU_MODE_NONE;
    menu->next_mode = MENU_MODE_BROWSER;
}

static component_context_menu_t start_menu = {
    .list = {
        { .text = "File browser", .action = open_file_browser },
        { .text = "Controller Pak manager", .action = open_screen, .arg = (void *)(MENU_MODE_CONTROLLER_PAKFS) },
        { .text = "Menu settings", .action = open_screen, .arg = (void *)(MENU_MODE_SETTINGS_EDITOR) },
        { .text = "Time (RTC) settings", .action = open_screen, .arg = (void *)(MENU_MODE_RTC) },
        { .text = "Menu information", .action = open_screen, .arg = (void *)(MENU_MODE_CREDITS) },
        { .text = "Flashcart information", .action = open_screen, .arg = (void *)(MENU_MODE_FLASHCART) },
        { .text = "N64 information", .action = open_screen, .arg = (void *)(MENU_MODE_SYSTEM_INFO) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

void launcher_menu_init (void) {
    ui_components_context_menu_init(&start_menu);
}

void launcher_menu_show (void) {
    ui_components_context_menu_show(&start_menu);
    sound_play_effect(SFX_SETTING);
}

bool launcher_menu_process (menu_t *menu) {
    return ui_components_context_menu_process(menu, &start_menu);
}

void launcher_menu_draw (void) {
    ui_components_context_menu_draw(&start_menu);
}
