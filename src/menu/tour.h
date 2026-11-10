/**
 * @file tour.h
 * @brief Screenshot tour (Settings > System): the launcher steps through every theme, Home style, list combination,
 * Library layout and screen size by itself and saves each screen to menu/tour/ on the SD card.
 * @ingroup menu
 */

#ifndef TOUR_H__
#define TOUR_H__

#include <stdbool.h>
#include <libdragon.h>

#include "menu_state.h"

/** @brief Begin the tour now (Settings > System > Screenshot tour); it starts once the launcher's screen is up. */
void tour_begin(menu_t *menu);

/** @brief Whether the tour is running (the controller is ignored meanwhile). */
bool tour_active(void);

/**
 * @brief Before a frame: the controller cleared, the next scene set up when one is due.
 * @return true when the current screen must start over (its init run again) for the scene.
 */
bool tour_before_frame(menu_t *menu);

/** @brief After a frame is drawn: the screen saved when the scene has settled. */
void tour_after_frame(menu_t *menu, surface_t *display);

#endif
