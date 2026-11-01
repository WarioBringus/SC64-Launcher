/**
 * @file covers.c
 * @brief Cache of decoded cover images for the launcher.
 * @ingroup ui_components
 */

#include <stdlib.h>
#include <string.h>

#include "../launcher_ui.h"
#include "../png_decoder.h"

#define COVER_SLOTS (24)

typedef struct {
    char *path;
    int max_w;
    int max_h;
    surface_t *image;
    bool loading;
    bool failed;
    uint32_t last_used;
} cover_slot_t;

static cover_slot_t slots[COVER_SLOTS];
static uint32_t frame = 0;
static bool decoding = false;


static void decode_done (png_err_t err, surface_t *decoded_image, void *callback_data) {
    cover_slot_t *slot = (cover_slot_t *)(callback_data);
    slot->loading = false;
    decoding = false;
    if (err == PNG_OK && decoded_image) {
        slot->image = decoded_image;
    } else {
        slot->failed = true;
        if (decoded_image) {
            surface_free(decoded_image);
            free(decoded_image);
        }
    }
}

static void slot_free (cover_slot_t *slot) {
    if (slot->loading) {
        png_decoder_abort();
        decoding = false;
    }
    if (slot->image) {
        surface_free(slot->image);
        free(slot->image);
    }
    free(slot->path);
    memset(slot, 0, sizeof(*slot));
}

void launcher_covers_begin_frame (void) {
    frame++;
}

surface_t *launcher_cover_get (const char *path, int max_w, int max_h, bool *pending) {
    if (pending) {
        *pending = false;
    }
    if (!path) {
        return NULL;
    }

    for (int i = 0; i < COVER_SLOTS; i++) {
        cover_slot_t *s = &slots[i];
        if (s->path && s->max_w == max_w && s->max_h == max_h && strcmp(s->path, path) == 0) {
            s->last_used = frame;
            if (pending) {
                *pending = s->loading;
            }
            return s->image;
        }
    }

    if (pending) {
        *pending = true;
    }
    if (decoding) {
        return NULL;
    }

    // Reuse an empty slot, or the one unused for longest (never one drawn this frame).
    cover_slot_t *victim = NULL;
    for (int i = 0; i < COVER_SLOTS; i++) {
        cover_slot_t *s = &slots[i];
        if (!s->path) {
            victim = s;
            break;
        }
        if (s->last_used != frame && !s->loading && (!victim || s->last_used < victim->last_used)) {
            victim = s;
        }
    }
    if (!victim) {
        return NULL;
    }

    slot_free(victim);
    victim->path = strdup(path);
    victim->max_w = max_w;
    victim->max_h = max_h;
    victim->last_used = frame;
    victim->loading = true;

    png_err_t err = png_decoder_start(victim->path, max_w, max_h, decode_done, victim);
    if (err == PNG_OK) {
        decoding = true;
    } else if (err == PNG_ERR_BUSY) {
        // Another screen is using the decoder; try again next frame.
        free(victim->path);
        memset(victim, 0, sizeof(*victim));
    } else {
        victim->loading = false;
        victim->failed = true;
        if (pending) {
            *pending = false;
        }
    }
    return NULL;
}

void launcher_covers_reset (void) {
    for (int i = 0; i < COVER_SLOTS; i++) {
        slot_free(&slots[i]);
    }
    decoding = false;
}
