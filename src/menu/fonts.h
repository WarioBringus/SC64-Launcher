/**
 * @file fonts.h
 * @brief Menu fonts
 * @ingroup menu 
 */

#ifndef FONTS_H__
#define FONTS_H__

#include <stdbool.h>
#include <libdragon.h>

/**
 * @brief Font type enumeration.
 * 
 * This enumeration defines the different types of fonts that can be used
 * in the menu system.
 */
typedef enum {
    FNT_DEFAULT = 1, /**< Default font type */
    FNT_LAUNCHER = 2, /**< Launcher body text (Inter) */
    FNT_TITLE = 3,   /**< Launcher titles (Inter, larger) */
    FNT_SMALL = 4,   /**< Launcher button hints (Inter, smaller) */
    FNT_LAUNCHER_WIDE = 5, /**< FNT_LAUNCHER squeezed for 16:9 */
    FNT_TITLE_WIDE = 6,    /**< FNT_TITLE squeezed for 16:9 */
    FNT_SMALL_WIDE = 7,    /**< FNT_SMALL squeezed for 16:9 */
    FNT_LARGE = 8,         /**< Launcher game titles with Text size: Large (Inter, 18 px) */
    FNT_LARGE_WIDE = 9,    /**< FNT_LARGE squeezed for 16:9 */
    FNT_DISPLAY = 10,      /**< Minimal's focused title (Inter, 32 px, Latin only; loaded by fonts_load_display) */
    FNT_DISPLAY_WIDE = 11, /**< FNT_DISPLAY squeezed for 16:9 */
    FNT_DISPLAY_MID = 12,  /**< 27 px, between the title and display sizes (Minimal's names as they grow) */
    FNT_DISPLAY_MID_WIDE = 13, /**< FNT_DISPLAY_MID squeezed for 16:9 */
} menu_font_type_t;

/**
 * @brief Font style enumeration.
 * 
 * This enumeration defines the different styles of fonts that can be used
 * in the menu system.
 */
typedef enum {
    STL_DEFAULT = 0, /**< Default font style */
    STL_GREEN,       /**< Green font style */
    STL_BLUE,        /**< Blue font style */
    STL_YELLOW,      /**< Yellow font style */
    STL_ORANGE,      /**< Orange font style */
    STL_RED,         /**< Red font style */
    STL_GRAY,        /**< Gray font style */
    STL_ACCENT,      /**< Theme accent color */
    STL_ON_ACCENT,   /**< Text drawn on the accent color */
    STL_ON_BUTTON,   /**< White, for button icons */
    STL_ON_C_BUTTON, /**< Dark, for the yellow C button icons */
    STL_ON_FEATURE,  /**< Text drawn on the theme's feature color */
    STL_FADE,        /**< Any colour, see-through too: set with fonts_set_fade_color just before drawing */
} menu_font_style_t;

/**
 * @brief Initialize fonts.
 * 
 * This function initializes the fonts used in the menu system. It can load
 * custom fonts from the specified path.
 * 
 * @param custom_font_path Path to the custom font file.
 */
void fonts_init(char *custom_font_path);

/**
 * @brief Load the menu's main font (Firple, 476 KB with its CJK glyphs) if it isn't yet. Startup
 * loads only the launcher's small fonts and calls this once Home is on screen; any screen of the
 * original menu calls it first.
 */
void fonts_load_default(void);

/**
 * @brief Re-color the font styles from the active theme.
 */
void fonts_apply_theme(void);
/** @brief The colour STL_FADE text is drawn in from now on (alpha below 255 is see-through). */
void fonts_set_fade_color(color_t color);
/** @brief Load the display font (once): only Minimal uses it, so it isn't loaded at startup. */
void fonts_load_display(void);
/** @brief Whether the display font is loaded. */
bool fonts_display_loaded(void);

#endif /* FONTS_H__ */
