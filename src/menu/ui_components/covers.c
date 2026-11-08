/**
 * @file covers.c
 * @brief Cache of decoded cover images for the launcher.
 * @ingroup ui_components
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../launcher_ui.h"
#include "../library.h"
#include "../perf_log.h"
#include "../png_decoder.h"

#define COVER_SLOTS             (96)    /* With an Expansion Pak */
#define COVER_SLOTS_4MB         (24)    /* Without one */
/* Pack reads stop for the frame once this much time has gone on them (at least one read per frame),
   so a screen full of new covers fills in over a few smooth frames instead of stalling one. */
#define PACK_READ_BUDGET_US     (3500)
#define FAILED_RETRY_FRAMES     (120)   /* About 2 seconds at 60 fps */
#define MAX_PACKS               (24)    /* 10 cover sizes and the spines, each as is and squeezed for 16:9 */
/* Memory for decoded covers: what the slots held when every cover was at most 158x112, so the
   carousel's and shelf's larger covers never take more than covers did before. */
#define COVER_BYTES_PER_SLOT    (LAUNCHER_COVER_W * LAUNCHER_COVER_H * 2)
#define PACK_FILE_FORMAT        "%.*s/menu/cache/launcher/covers_%dx%d%s.pak"

/* Pre-scaled covers built on the PC by pc-tools/coverpack (one file per tile size). */
typedef struct {
    int w;
    int h;
    bool wide;                  /**< Squeezed for 16:9 (covers_WxH_w.pak) */
    bool tried;
    FILE *f;
    uint32_t count;
    uint32_t *offsets;
} cover_pack_t;

typedef struct {
    char *path;
    int max_w;
    int max_h;
    surface_t *image;           /**< Handed out; for a 16:9 cover, a header with the 4:3 size over wide's pixels */
    surface_t *wide;            /**< 16:9 cover: the squeezed image, copied 1:1 */
    bool for_wide;              /**< Loaded for the 16:9 screen */
    bool loading;
    bool failed;
    uint32_t failed_frame;      /**< When it failed; it is retried after FAILED_RETRY_FRAMES */
    uint32_t last_used;
    size_t bytes;               /**< Pixel memory held */
} cover_slot_t;

static cover_slot_t slots[COVER_SLOTS];
static int slot_count = 0;
static uint32_t frame = 0;
static bool decoding = false;
static cover_pack_t packs[MAX_PACKS];
static bool cartridge_art = false;
static uint64_t pack_time_used_us = 0;
static bool pack_read_this_frame = false;
static size_t bytes_used = 0;


/** @brief Whether another pack read fits in this frame's budget (time spent reading packs only). */
static bool pack_budget_left (void) {
    return !pack_read_this_frame || pack_time_used_us < PACK_READ_BUDGET_US;
}


static uint32_t read_u32_be (const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/**
 * @brief The pack for a tile size, opened on first use. path is any cover path, for its "sd:" prefix.
 */
static cover_pack_t *pack_for (const char *path, int w, int h, bool wide) {
    cover_pack_t *free_entry = NULL;
    for (int i = 0; i < MAX_PACKS; i++) {
        if (packs[i].tried && packs[i].w == w && packs[i].h == h && packs[i].wide == wide) {
            return packs[i].f ? &packs[i] : NULL;
        }
        if (!packs[i].tried && !free_entry) {
            free_entry = &packs[i];
        }
    }
    if (!free_entry) {
        return NULL;
    }

    cover_pack_t *p = free_entry;
    memset(p, 0, sizeof(*p));
    p->tried = true;
    p->w = w;
    p->h = h;
    p->wide = wide;

    const char *colon = strchr(path, ':');
    if (!colon) {
        return NULL;
    }
    char file[96];
    snprintf(file, sizeof(file), PACK_FILE_FORMAT, (int)(colon - path + 1), path, w, h, wide ? "_w" : "");
    p->f = fopen(file, "rb");
    if (!p->f) {
        return NULL;
    }
    // Unbuffered: a cover is then one large read straight into its surface, rather than many small
    // ones through stdio's buffer (the performance log measured those at about 1 MB/s).
    setvbuf(p->f, NULL, _IONBF, 0);
    uint8_t head[8];
    if (fread(head, 1, sizeof(head), p->f) != sizeof(head) || memcmp(head, "CVP1", 4) != 0) {
        fclose(p->f);
        p->f = NULL;
        return NULL;
    }
    p->count = read_u32_be(head + 4);
    p->offsets = malloc(p->count * sizeof(uint32_t));
    uint8_t *raw = malloc(p->count * 4);
    if (!p->offsets || !raw || fread(raw, 4, p->count, p->f) != p->count) {
        free(raw);
        free(p->offsets);
        p->offsets = NULL;
        fclose(p->f);
        p->f = NULL;
        return NULL;
    }
    for (uint32_t i = 0; i < p->count; i++) {
        p->offsets[i] = read_u32_be(raw + i * 4);
    }
    free(raw);
    return p;
}

/**
 * @brief Find a cover's pack and id; NULL when this cover isn't in the pack for this size.
 */
static cover_pack_t *pack_lookup_kind (const char *path, int w, int h, bool wide, int *id) {
    const char *rel = strstr(path, ":/menu/");
    if (!rel) {
        return NULL;
    }
    *id = library_cover_id(rel + strlen(":/menu/"));
    if (*id < 0) {
        return NULL;
    }
    cover_pack_t *p = pack_for(path, w, h, wide);
    if (!p || (uint32_t)*id >= p->count || p->offsets[*id] == 0) {
        return NULL;
    }
    return p;
}

static cover_pack_t *pack_lookup (const char *path, int w, int h, int *id) {
    return pack_lookup_kind(path, w, h, false, id);
}

/**
 * @brief Read a cover from a 16:9 pack: the squeezed image, and the 4:3 width it stands for.
 */
static surface_t *pack_read_wide (cover_pack_t *p, int id, int w, int h, int *logical_w) {
    uint8_t head[12];
    if (fseek(p->f, p->offsets[id], SEEK_SET) != 0 || fread(head, 1, sizeof(head), p->f) != sizeof(head) ||
        memcmp(head, "CVW1", 4) != 0) {
        return NULL;
    }
    int iw = (head[4] << 8) | head[5];
    int ih = (head[6] << 8) | head[7];
    *logical_w = (head[8] << 8) | head[9];
    if (iw <= 0 || ih <= 0 || *logical_w > w || ih > h) {
        return NULL;
    }
    surface_t *image = calloc(1, sizeof(surface_t));
    if (!image) {
        return NULL;
    }
    *image = surface_alloc(FMT_RGBA16, iw, ih);
    if (!image->buffer) {
        free(image);
        return NULL;
    }
    size_t row = (size_t)iw * 2;
    bool ok = (fread(image->buffer, row, ih, p->f) == (size_t)ih);
    if (ok && (size_t)image->stride != row) {
        uint8_t *buffer = image->buffer;
        for (int y = ih - 1; y > 0; y--) {
            memmove(buffer + y * image->stride, buffer + y * row, row);
        }
    }
    if (!ok) {
        surface_free(image);
        free(image);
        return NULL;
    }
    return image;
}

/**
 * @brief Read a pre-scaled cover from its pack: one seek and one read, no PNG decoding.
 */
static surface_t *pack_read (cover_pack_t *p, int id, int w, int h) {

    uint8_t head[8];
    if (fseek(p->f, p->offsets[id], SEEK_SET) != 0 || fread(head, 1, sizeof(head), p->f) != sizeof(head) ||
        memcmp(head, "CVR1", 4) != 0) {
        return NULL;
    }
    int iw = (head[4] << 8) | head[5];
    int ih = (head[6] << 8) | head[7];
    if (iw <= 0 || ih <= 0 || iw > w || ih > h) {
        return NULL;
    }
    surface_t *image = calloc(1, sizeof(surface_t));
    if (!image) {
        return NULL;
    }
    *image = surface_alloc(FMT_RGBA16, iw, ih);
    if (!image->buffer) {
        free(image);
        return NULL;
    }
    // Pixels are stored big-endian RGBA5551, the N64's own format. Read them in one go, then
    // spread the rows out if the surface pads them.
    size_t row = (size_t)iw * 2;
    bool ok = (fread(image->buffer, row, ih, p->f) == (size_t)ih);
    if (ok && (size_t)image->stride != row) {
        uint8_t *buffer = image->buffer;
        for (int y = ih - 1; y > 0; y--) {
            memmove(buffer + y * image->stride, buffer + y * row, row);
        }
    }
    if (!ok) {
        surface_free(image);
        free(image);
        return NULL;
    }
    return image;
}

static void packs_close (void) {
    for (int i = 0; i < MAX_PACKS; i++) {
        if (packs[i].f) {
            fclose(packs[i].f);
        }
        free(packs[i].offsets);
        memset(&packs[i], 0, sizeof(packs[i]));
    }
}


/**
 * @brief Trim a cover to even dimensions: the RDP draws an odd last row or column as a repeated
 * line of the image along the bottom or right edge. One pixel is never missed.
 */
static void even_size (surface_t *image) {
    if (image && image->width > 1 && (image->width & 1)) {
        image->width--;
    }
    if (image && image->height > 1 && (image->height & 1)) {
        image->height--;
    }
}

surface_t *launcher_cover_larger (surface_t *shown, int max_w, int max_h) {
    for (int i = 0; i < slot_count; i++) {
        cover_slot_t *s = &slots[i];
        if (s->image != shown || !s->path) {
            continue;
        }
        // One seek and one read from the pack (no PNG decoding), so it is quick enough for a launch.
        int id = -1;
        cover_pack_t *pack = pack_lookup(s->path, max_w, max_h, &id);
        surface_t *image = pack ? pack_read(pack, id, max_w, max_h) : NULL;
        if (!pack) {
            // Packs built for 16:9 only (coverpack --wide-only): widen the squeezed cover back.
            int logical_w = 0;
            pack = pack_lookup_kind(s->path, max_w, max_h, true, &id);
            surface_t *squeezed = pack ? pack_read_wide(pack, id, max_w, max_h, &logical_w) : NULL;
            if (squeezed && logical_w > 0) {
                image = calloc(1, sizeof(surface_t));
                *image = surface_alloc(FMT_RGBA16, logical_w, squeezed->height);
                if (image->buffer) {
                    for (int y = 0; y < squeezed->height; y++) {
                        uint16_t *from = (uint16_t *)((uint8_t *)squeezed->buffer + y * squeezed->stride);
                        uint16_t *to = (uint16_t *)((uint8_t *)image->buffer + y * image->stride);
                        for (int x = 0; x < logical_w; x++) {
                            to[x] = from[(x * squeezed->width) / logical_w];
                        }
                    }
                } else {
                    free(image);
                    image = NULL;
                }
            }
            if (squeezed) {
                surface_free(squeezed);
                free(squeezed);
            }
        }
        even_size(image);
        return image;
    }
    return NULL;
}

/** @brief Count a slot's pixels once its image is in. */
static void slot_account (cover_slot_t *slot) {
    surface_t *pixels = slot->wide ? slot->wide : slot->image;
    slot->bytes = pixels ? (size_t)pixels->stride * pixels->height : 0;
    bytes_used += slot->bytes;
}

static void decode_done (png_err_t err, surface_t *decoded_image, void *callback_data) {
    cover_slot_t *slot = (cover_slot_t *)(callback_data);
    slot->loading = false;
    decoding = false;
    if (err == PNG_OK && decoded_image) {
        even_size(decoded_image);
        slot->image = decoded_image;
        slot_account(slot);
    } else {
        slot->failed = true;
        slot->failed_frame = frame;
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
    if (slot->wide) {
        surface_free(slot->wide);
        free(slot->wide);
        free(slot->image);      // Only a header over wide's pixels.
    } else if (slot->image) {
        surface_free(slot->image);
        free(slot->image);
    }
    bytes_used -= slot->bytes;
    free(slot->path);
    memset(slot, 0, sizeof(*slot));
}

static bool hold_new = false;
static uint32_t last_read_frame = 0;    /* Frame of the last pack read or PNG decode started */

surface_t *launcher_cover_physical (surface_t *image) {
    if (!image) {
        return NULL;
    }
    for (int i = 0; i < slot_count; i++) {
        if (slots[i].image == image) {
            return slots[i].wide;
        }
    }
    return NULL;
}

void launcher_covers_hold (bool hold) {
    hold_new = hold;
}

void launcher_covers_begin_frame (void) {
    frame++;
    hold_new = false;
    launcher_clear_focused_cover();
    pack_time_used_us = 0;
    pack_read_this_frame = false;
    if (slot_count == 0) {
        slot_count = is_memory_expanded() ? COVER_SLOTS : COVER_SLOTS_4MB;
    }
}

void launcher_set_cartridge_art (bool enabled) {
    cartridge_art = enabled;
}

/**
 * @brief The cartridge image beside a box front, if the cover index has it (so it's in the packs).
 */
static bool cartridge_path (const char *box, char *buffer, size_t size) {
    const char *name = strstr(box, "boxart_front.png");
    const char *rel = strstr(box, ":/menu/");
    if (!name || !rel || (size_t)(name - box) + sizeof("gamepak_front.png") > size) {
        return false;
    }
    memcpy(buffer, box, name - box);
    strcpy(buffer + (name - box), "gamepak_front.png");
    return library_cover_id(buffer + (rel - box) + strlen(":/menu/")) >= 0;
}

static surface_t *cover_get (const char *path, int max_w, int max_h, bool *pending, bool spine) {
    if (pending) {
        *pending = false;
    }
    if (!path) {
        return NULL;
    }
    char cartridge[256];
    if (!spine && cartridge_art && cartridge_path(path, cartridge, sizeof(cartridge))) {
        path = cartridge;
    }
    bool want_wide = launcher_is_widescreen();

    if (slot_count == 0) {
        slot_count = is_memory_expanded() ? COVER_SLOTS : COVER_SLOTS_4MB;
    }

    for (int i = 0; i < slot_count; i++) {
        cover_slot_t *s = &slots[i];
        if (s->path && s->max_w == max_w && s->max_h == max_h && s->for_wide == want_wide && strcmp(s->path, path) == 0) {
            if (s->failed && (frame - s->failed_frame) > FAILED_RETRY_FRAMES) {
                // Give a failed cover another try rather than leaving it blank.
                slot_free(s);
                break;
            }
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

    if (hold_new && (frame - last_read_frame) < 2) {
        return NULL;    // Scrolling fast: one read every other frame, so no two frames in a row run long.
    }

    // Covers in a pack wait for read budget; only covers missing from the packs use the PNG decoder.
    // On the 16:9 screen the squeezed pack comes first (copied 1:1); the 4:3 one is resized as it is drawn.
    int id = -1;
    cover_pack_t *pack = want_wide ? pack_lookup_kind(path, max_w, max_h, true, &id) : NULL;
    bool wide_pack = (pack != NULL);
    if (!pack) {
        pack = pack_lookup(path, max_w, max_h, &id);
    }
    if (!pack && spine) {
        if (pending) {
            *pending = false;   // Spines only come from their pack.
        }
        return NULL;
    }
    if (pack ? !pack_budget_left() : decoding) {
        return NULL;
    }

    // Reuse an empty slot, or the one unused for longest (never one drawn this frame).
    cover_slot_t *victim = NULL;
    for (int i = 0; i < slot_count; i++) {
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

    // Free the covers unused longest until this one fits in memory.
    size_t need = (size_t)max_w * max_h * 2 * (want_wide ? 3 : 4) / 4;
    while (bytes_used + need > (size_t)slot_count * COVER_BYTES_PER_SLOT) {
        cover_slot_t *oldest = NULL;
        for (int i = 0; i < slot_count; i++) {
            cover_slot_t *s = &slots[i];
            if (s->path && s->bytes && s->last_used != frame && !s->loading && (!oldest || s->last_used < oldest->last_used)) {
                oldest = s;
            }
        }
        if (!oldest) {
            return NULL;    // Everything held is on screen; try again next frame.
        }
        slot_free(oldest);
    }

    victim->path = strdup(path);
    victim->max_w = max_w;
    victim->max_h = max_h;
    victim->for_wide = want_wide;
    victim->last_used = frame;

    last_read_frame = frame;
    if (pack) {
        uint64_t started = get_ticks_us();
        if (wide_pack) {
            int logical_w = 0;
            surface_t *squeezed = pack_read_wide(pack, id, max_w, max_h, &logical_w);
            surface_t *header = squeezed ? calloc(1, sizeof(surface_t)) : NULL;
            if (header) {
                *header = *squeezed;
                header->width = logical_w;
                header->flags &= ~SURFACE_FLAGS_OWNEDBUFFER;
                victim->wide = squeezed;
                victim->image = header;
            } else if (squeezed) {
                surface_free(squeezed);
                free(squeezed);
            }
        } else {
            victim->image = pack_read(pack, id, max_w, max_h);
            even_size(victim->image);
        }
        uint32_t read_us = (uint32_t)(get_ticks_us() - started);
        pack_time_used_us += read_us;
        pack_read_this_frame = true;
        if (victim->image) {
            slot_account(victim);
            perf_log_read((uint32_t)victim->bytes, read_us);
            if (pending) {
                *pending = false;
            }
            return victim->image;
        }
        victim->failed = true;
        victim->failed_frame = frame;
        if (pending) {
            *pending = false;
        }
        return NULL;
    }

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
        victim->failed_frame = frame;
        if (pending) {
            *pending = false;
        }
    }
    return NULL;
}

surface_t *launcher_cover_get (const char *path, int max_w, int max_h, bool *pending) {
    return cover_get(path, max_w, max_h, pending, false);
}

surface_t *launcher_spine_get (const char *front_path, bool *pending) {
    return cover_get(front_path, LAUNCHER_SPINE_MAX_W, LAUNCHER_SPINE_H, pending, true);
}

void launcher_covers_reset (void) {
    for (int i = 0; i < COVER_SLOTS; i++) {
        slot_free(&slots[i]);
    }
    decoding = false;
    packs_close();
}
