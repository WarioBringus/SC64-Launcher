/**
 * @file theme.h
 * @brief Menu color themes.
 * @ingroup menu
 *
 * Every color used by the menu comes from the active theme. The palettes
 * live in theme.c; add or tweak one there. All values are RGBA8888.
 */

#ifndef THEME_H__
#define THEME_H__

#include <libdragon.h>

/** @brief One color palette. */
typedef struct {
    const char *name;       /**< Name shown in settings and saved in config.ini */

    color_t bg;             /**< Screen background */
    color_t panel;          /**< Cards, dialogs, empty cover tiles */
    color_t line;           /**< Borders and separators */
    color_t highlight;      /**< Selected row behind text */
    color_t accent;         /**< Selection ring, progress fill */
    color_t accent2;        /**< Active tab, secondary accent */
    color_t on_accent;      /**< Text drawn on the accent color */

    color_t text;           /**< Main text */
    color_t text_muted;     /**< Secondary text, hints */
    color_t text_green;     /**< STL_GREEN */
    color_t text_blue;      /**< STL_BLUE */
    color_t text_yellow;    /**< STL_YELLOW */
    color_t text_orange;    /**< STL_ORANGE */
    color_t text_red;       /**< STL_RED */
} theme_t;

/** @brief Number of built-in themes. */
int theme_count(void);

/** @brief Theme by index, or NULL when out of range. */
const theme_t *theme_at(int index);

/** @brief Index of the theme with this name, or 0 (the default) when not found. */
int theme_find(const char *name);

/** @brief Make a theme active. Out-of-range indexes select the default. */
void theme_set(int index);

/** @brief Index of the active theme. */
int theme_current_index(void);

/** @brief The active theme. */
const theme_t *theme_current(void);

/** @brief A color with its alpha replaced. */
static inline color_t theme_with_alpha(color_t c, uint8_t a) {
    c.a = a;
    return c;
}

/* Roles used by the stock menu screens, mapped onto the active theme. */
#define THEME_BACKGROUND_EMPTY          (theme_current()->bg)
#define THEME_BACKGROUND_OVERLAY        (theme_with_alpha(theme_current()->bg, 0xA0))
#define THEME_DIALOG_BG                 (theme_current()->panel)
#define THEME_FAULT_BG                  RGBA32(0x7F, 0x00, 0x00, 0xFF)

#define THEME_BORDER                    (theme_current()->line)
#define THEME_HIGHLIGHT                 (theme_current()->highlight)

#define THEME_PROGRESSBAR_BG            (theme_current()->panel)
#define THEME_PROGRESSBAR_DONE          (theme_current()->accent)
#define THEME_SCROLLBAR_BG              (theme_current()->panel)
#define THEME_SCROLLBAR_INACTIVE        (theme_current()->line)
#define THEME_SCROLLBAR_POSITION        (theme_current()->text_muted)
#define THEME_BOXART_LOADING            (theme_current()->panel)

#define THEME_TAB_INACTIVE_BORDER       (theme_current()->line)
#define THEME_TAB_ACTIVE_BORDER         (theme_current()->accent2)
#define THEME_TAB_INACTIVE_BG           (theme_current()->panel)
#define THEME_TAB_ACTIVE_BG             (theme_current()->highlight)

#define THEME_TEXT_DEFAULT              (theme_current()->text)
#define THEME_TEXT_GREEN                (theme_current()->text_green)
#define THEME_TEXT_BLUE                 (theme_current()->text_blue)
#define THEME_TEXT_YELLOW               (theme_current()->text_yellow)
#define THEME_TEXT_ORANGE               (theme_current()->text_orange)
#define THEME_TEXT_RED                  (theme_current()->text_red)
#define THEME_TEXT_GRAY                 (theme_current()->text_muted)

#endif /* THEME_H__ */
