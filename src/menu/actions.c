#include <libdragon.h>

#include "actions.h"


/* Held directions repeat after a pause, then steadily; timed in milliseconds so the frame rate doesn't matter. */
#define ACTIONS_REPEAT_DELAY_MS     (250)
#define ACTIONS_REPEAT_INTERVAL_MS  (40)


static uint64_t repeat_at_ms;
static joypad_8way_t last_dir = JOYPAD_8WAY_NONE;


static void actions_clear (menu_t *menu) {
    menu->actions.go_up = false;
    menu->actions.go_down = false;
    menu->actions.go_left = false;
    menu->actions.go_right = false;
    menu->actions.go_fast = false;

    menu->actions.enter = false;
    menu->actions.back = false;
    menu->actions.options = false;
    menu->actions.settings = false;
    menu->actions.lz_context = false;

    menu->actions.shoulder_l = false;
    menu->actions.shoulder_r = false;
    menu->actions.trigger_z = false;
}

static void actions_update_direction (menu_t *menu) {
    joypad_8way_t held_dir = JOYPAD_8WAY_NONE;
    joypad_8way_t fast_dir = JOYPAD_8WAY_NONE;

    JOYPAD_PORT_FOREACH (i) {
        held_dir = joypad_get_direction(i, JOYPAD_2D_DPAD | JOYPAD_2D_STICK);
        fast_dir = joypad_get_direction(i, JOYPAD_2D_C);
        if (held_dir != JOYPAD_8WAY_NONE || fast_dir != JOYPAD_8WAY_NONE) {
            break;
        }
    }

    if (fast_dir != JOYPAD_8WAY_NONE) {
        held_dir = fast_dir;
        menu->actions.go_fast = true;
    }

    joypad_8way_t final_dir = held_dir;
    uint64_t now = get_ticks_ms();

    if (held_dir == JOYPAD_8WAY_NONE) {
        // Nothing held.
    } else if (held_dir != last_dir) {
        // New press (or a change of direction): act now, then wait before repeating.
        repeat_at_ms = now + ACTIONS_REPEAT_DELAY_MS;
    } else if (now >= repeat_at_ms) {
        repeat_at_ms = now + ACTIONS_REPEAT_INTERVAL_MS;
    } else {
        final_dir = JOYPAD_8WAY_NONE;
    }

    switch (final_dir) {
        case JOYPAD_8WAY_NONE:
            break;
        case JOYPAD_8WAY_RIGHT:
            menu->actions.go_right = true;
            break;
        case JOYPAD_8WAY_UP_RIGHT:
            menu->actions.go_up = true;
            menu->actions.go_right = true;
            break;
        case JOYPAD_8WAY_UP:
            menu->actions.go_up = true;
            break;
        case JOYPAD_8WAY_UP_LEFT:
            menu->actions.go_up = true;
            menu->actions.go_left = true;
            break;
        case JOYPAD_8WAY_LEFT:
            menu->actions.go_left = true;
            break;
        case JOYPAD_8WAY_DOWN_LEFT:
            menu->actions.go_down = true;
            menu->actions.go_left = true;
            break;
        case JOYPAD_8WAY_DOWN:
            menu->actions.go_down = true;
            break;
        case JOYPAD_8WAY_DOWN_RIGHT:
            menu->actions.go_down = true;
            menu->actions.go_right = true;
            break;
    }

    last_dir = held_dir;
}

static void actions_update_buttons (menu_t *menu) {    
    joypad_buttons_t pressed = {0};

    JOYPAD_PORT_FOREACH (i) {
        pressed = joypad_get_buttons_pressed(i);
        if (pressed.raw) {
            break;
        }
    }

    if (pressed.a) {
        menu->actions.enter = true;
    } else if (pressed.b) {
        menu->actions.back = true;
    } else if (pressed.r) {
        menu->actions.options = true;
        menu->actions.shoulder_r = true;
    } else if (pressed.start) {
        menu->actions.settings = true;
    } else if (pressed.l || pressed.z) {
        menu->actions.lz_context = true;
        menu->actions.shoulder_l = pressed.l;
        menu->actions.trigger_z = pressed.z;
    }
}


void actions_init (void) {
    JOYPAD_PORT_FOREACH (port) {
        joypad_set_rumble_active(port, false);
    }
}

void actions_update (menu_t *menu) {
    joypad_poll();

    actions_clear(menu);
    actions_update_direction(menu);
    actions_update_buttons(menu);
}
