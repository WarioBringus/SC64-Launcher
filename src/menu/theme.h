/**
 * @file theme.h
 * @brief Menu color theme.
 * @ingroup menu
 *
 * Every color used by the menu is defined here, so the whole look can be
 * swapped by editing this one file. All values are RGBA8888.
 */

#ifndef THEME_H__
#define THEME_H__

#include <libdragon.h>

/* Backgrounds */
#define THEME_BACKGROUND_EMPTY          RGBA32(0x00, 0x00, 0x00, 0xFF) /**< Screen color when no background image is set */
#define THEME_BACKGROUND_OVERLAY        RGBA32(0x00, 0x00, 0x00, 0xA0) /**< Tint drawn over the background image */
#define THEME_DIALOG_BG                 RGBA32(0x00, 0x00, 0x00, 0xFF) /**< Dialog and message box fill */
#define THEME_FAULT_BG                  RGBA32(0x7F, 0x00, 0x00, 0xFF) /**< Unrecoverable error screen */

/* Lines and highlights */
#define THEME_BORDER                    RGBA32(0xFF, 0xFF, 0xFF, 0xFF) /**< UI borders */
#define THEME_HIGHLIGHT                 RGBA32(0x7F, 0x7F, 0x7F, 0xFF) /**< Selected row in lists and context menus */

/* Progress, scroll and loading */
#define THEME_PROGRESSBAR_BG            RGBA32(0x00, 0x00, 0x00, 0xFF) /**< Progress bar track */
#define THEME_PROGRESSBAR_DONE          RGBA32(0x3B, 0x7C, 0xF5, 0xFF) /**< Progress bar fill */
#define THEME_SCROLLBAR_BG              RGBA32(0x3F, 0x3F, 0x3F, 0xFF) /**< Scrollbar track */
#define THEME_SCROLLBAR_INACTIVE        RGBA32(0x5F, 0x5F, 0x5F, 0xFF) /**< Scrollbar thumb when everything fits */
#define THEME_SCROLLBAR_POSITION        RGBA32(0x7F, 0x7F, 0x7F, 0xFF) /**< Scrollbar thumb */
#define THEME_BOXART_LOADING            RGBA32(0x00, 0x00, 0x00, 0xFF) /**< Placeholder while box art loads */

/* Tabs */
#define THEME_TAB_INACTIVE_BORDER       RGBA32(0x5F, 0x5F, 0x5F, 0xFF) /**< Inactive tab outline */
#define THEME_TAB_ACTIVE_BORDER         RGBA32(0xFF, 0xFF, 0xFF, 0xFF) /**< Active tab outline */
#define THEME_TAB_INACTIVE_BG           RGBA32(0x3F, 0x3F, 0x3F, 0xFF) /**< Inactive tab fill */
#define THEME_TAB_ACTIVE_BG             RGBA32(0x6F, 0x6F, 0x6F, 0xFF) /**< Active tab fill */

/* Text styles (see menu_font_style_t) */
#define THEME_TEXT_DEFAULT              RGBA32(0xFF, 0xFF, 0xFF, 0xFF) /**< STL_DEFAULT */
#define THEME_TEXT_GREEN                RGBA32(0x70, 0xFF, 0x70, 0xFF) /**< STL_GREEN */
#define THEME_TEXT_BLUE                 RGBA32(0x70, 0xBC, 0xFF, 0xFF) /**< STL_BLUE */
#define THEME_TEXT_YELLOW               RGBA32(0xFF, 0xFF, 0x70, 0xFF) /**< STL_YELLOW */
#define THEME_TEXT_ORANGE               RGBA32(0xFF, 0x99, 0x00, 0xFF) /**< STL_ORANGE */
#define THEME_TEXT_RED                  RGBA32(0xFF, 0x40, 0x40, 0xFF) /**< STL_RED */
#define THEME_TEXT_GRAY                 RGBA32(0xA0, 0xA0, 0xA0, 0xFF) /**< STL_GRAY */

#endif /* THEME_H__ */
