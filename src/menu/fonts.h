/**
 * @file fonts.h
 * @brief Menu fonts
 * @ingroup menu 
 */

#ifndef FONTS_H__
#define FONTS_H__

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
 * @brief Re-color the font styles from the active theme.
 */
void fonts_apply_theme(void);

#endif /* FONTS_H__ */
