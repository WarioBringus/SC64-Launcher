/*
 * coverimg - PC-side image helper for the launcher's cover thumbnails.
 *
 *   coverimg raw <in.png> <out-prefix> WxH [WxH ...]
 *       Fit the image inside each box (keeping its aspect ratio, never enlarging) and write
 *       <out-prefix>_<W>x<H>.raw in the launcher thumbnail format:
 *       "CVR1", width (u16 BE), height (u16 BE), then RGBA5551 pixels (BE), row by row.
 *
 *   A box written WxHw is for the launcher's 16:9 screen: the cover is fitted as for WxH, trimmed
 *   to even sizes as the menu does, then squeezed to 3/4 width so the menu can copy it 1:1.
 *   <out-prefix>_<W>x<H>w.raw holds "CVW1", stored width, height, the 4:3 width it stands
 *   for (u16 BE each), two zero bytes, then the RGBA5551 pixels.
 *
 *   coverimg png <in.png> <out.png> WxH
 *       Fit the image inside the box and save it as an RGBA PNG (for adding box art).
 *
 * The fit uses the same integer math as the menu's png_decoder, so a thumbnail has
 * exactly the size the menu would have decoded. Scaling is an area average.
 *
 * Build (MSYS2 MINGW64, from the repository root):
 *   gcc -O2 -static -DSPNG_USE_MINIZ -DSPNG_STATIC -I src/libs -I src/libs/libspng/spng -isystem src/libs/miniz \
 *       pc-tools/coverpack/coverimg.c src/libs/libspng/spng/spng.c src/libs/miniz/miniz*.c -o coverimg.exe
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spng.h"

typedef struct {
    int w;
    int h;
    uint8_t *rgba;
} image_t;

static int load_png (const char *path, image_t *img) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    spng_ctx *ctx = spng_ctx_new(0);
    spng_set_image_limits(ctx, 8192, 8192);
    spng_set_png_file(ctx, f);

    struct spng_ihdr ihdr;
    size_t size;
    int err = spng_get_ihdr(ctx, &ihdr);
    if (!err) {
        err = spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &size);
    }
    if (!err) {
        img->w = (int)ihdr.width;
        img->h = (int)ihdr.height;
        img->rgba = malloc(size);
        err = spng_decode_image(ctx, img->rgba, size, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS);
    }
    spng_ctx_free(ctx);
    fclose(f);
    if (err) {
        fprintf(stderr, "cannot decode %s: %s\n", path, spng_strerror(err));
        return 1;
    }
    return 0;
}

/* Same as png_decoder_setup() in the menu. */
static void fit (int src_w, int src_h, int max_w, int max_h, int *dst_w, int *dst_h) {
    int w = (src_w < max_w) ? src_w : max_w;
    int h = (src_h < max_h) ? src_h : max_h;
    if (src_w * h > src_h * w) {
        h = (src_h * w) / src_w;
    } else {
        w = (src_w * h) / src_h;
    }
    *dst_w = (w < 1) ? 1 : w;
    *dst_h = (h < 1) ? 1 : h;
}

/* Area-average resample to dst_w x dst_h (alpha-weighted colour). */
static image_t resample (const image_t *src, int dst_w, int dst_h) {
    image_t out = { dst_w, dst_h, malloc((size_t)dst_w * dst_h * 4) };
    double sx = (double)src->w / dst_w;
    double sy = (double)src->h / dst_h;

    for (int y = 0; y < dst_h; y++) {
        double y0 = y * sy, y1 = (y + 1) * sy;
        for (int x = 0; x < dst_w; x++) {
            double x0 = x * sx, x1 = (x + 1) * sx;
            double acc[4] = { 0, 0, 0, 0 };
            double total = 0;
            for (int yy = (int)y0; yy < src->h && yy < y1; yy++) {
                double wy = ((yy + 1 < y1) ? yy + 1 : y1) - ((yy > y0) ? yy : y0);
                for (int xx = (int)x0; xx < src->w && xx < x1; xx++) {
                    double wx = ((xx + 1 < x1) ? xx + 1 : x1) - ((xx > x0) ? xx : x0);
                    const uint8_t *p = &src->rgba[((size_t)yy * src->w + xx) * 4];
                    double wgt = wx * wy;
                    double a = p[3] * wgt;
                    acc[0] += p[0] * a;
                    acc[1] += p[1] * a;
                    acc[2] += p[2] * a;
                    acc[3] += a;
                    total += wgt;
                }
            }
            uint8_t *o = &out.rgba[((size_t)y * dst_w + x) * 4];
            if (acc[3] > 0) {
                o[0] = (uint8_t)(acc[0] / acc[3] + 0.5);
                o[1] = (uint8_t)(acc[1] / acc[3] + 0.5);
                o[2] = (uint8_t)(acc[2] / acc[3] + 0.5);
            } else {
                o[0] = o[1] = o[2] = 0;
            }
            o[3] = (uint8_t)((total > 0) ? (acc[3] / total + 0.5) : 0);
        }
    }
    return out;
}

static int parse_box (const char *s, int *w, int *h) {
    return (sscanf(s, "%dx%d", w, h) == 2 && *w > 0 && *h > 0) ? 0 : 1;
}

/* A box ending in 'w' is a 16:9 (squeezed) one. */
static int box_is_wide (const char *s) {
    size_t n = strlen(s);
    return n > 0 && s[n - 1] == 'w';
}

static int write_raw (const char *path, const image_t *img, int logical_w) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    if (logical_w > 0) {
        uint8_t head[12] = { 'C', 'V', 'W', '1',
            (uint8_t)(img->w >> 8), (uint8_t)img->w, (uint8_t)(img->h >> 8), (uint8_t)img->h,
            (uint8_t)(logical_w >> 8), (uint8_t)logical_w, 0, 0 };
        fwrite(head, 1, sizeof(head), f);
    } else {
        uint8_t head[8] = { 'C', 'V', 'R', '1',
            (uint8_t)(img->w >> 8), (uint8_t)img->w, (uint8_t)(img->h >> 8), (uint8_t)img->h };
        fwrite(head, 1, sizeof(head), f);
    }
    for (int i = 0; i < img->w * img->h; i++) {
        const uint8_t *p = &img->rgba[(size_t)i * 4];
        uint16_t v = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 3) << 6) | ((p[2] >> 3) << 1) | (p[3] >= 128 ? 1 : 0));
        uint8_t be[2] = { (uint8_t)(v >> 8), (uint8_t)v };
        fwrite(be, 1, 2, f);
    }
    fclose(f);
    return 0;
}

static int write_png (const char *path, const image_t *img) {
    spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, 1);
    struct spng_ihdr ihdr = { 0 };
    ihdr.width = (uint32_t)img->w;
    ihdr.height = (uint32_t)img->h;
    ihdr.bit_depth = 8;
    ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    spng_set_ihdr(ctx, &ihdr);
    int err = spng_encode_image(ctx, img->rgba, (size_t)img->w * img->h * 4, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    size_t len = 0;
    void *buf = err ? NULL : spng_get_png_buffer(ctx, &len, &err);
    spng_ctx_free(ctx);
    if (err || !buf) {
        fprintf(stderr, "cannot encode %s: %s\n", path, spng_strerror(err));
        return 1;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(buf);
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    fwrite(buf, 1, len, f);
    fclose(f);
    free(buf);
    return 0;
}

int main (int argc, char **argv) {
    if (argc >= 5 && strcmp(argv[1], "raw") == 0) {
        image_t src;
        if (load_png(argv[2], &src)) {
            return 1;
        }
        int failed = 0;
        for (int i = 4; i < argc; i++) {
            int bw, bh, dw, dh;
            if (parse_box(argv[i], &bw, &bh)) {
                fprintf(stderr, "bad box %s\n", argv[i]);
                return 2;
            }
            fit(src.w, src.h, bw, bh, &dw, &dh);
            char path[1024];
            if (box_is_wide(argv[i])) {
                // The menu trims odd sizes by a pixel; the squeezed width is kept even for the same reason.
                int lw = (dw > 1) ? (dw & ~1) : dw;
                int lh = (dh > 1) ? (dh & ~1) : dh;
                int pw = 2 * (int)(lw * 0.375 + 0.5);
                if (pw < 2) {
                    pw = 2;
                }
                image_t out = resample(&src, pw, lh);
                snprintf(path, sizeof(path), "%s_%dx%dw.raw", argv[3], bw, bh);
                failed |= write_raw(path, &out, lw);
                free(out.rgba);
            } else {
                image_t out = resample(&src, dw, dh);
                snprintf(path, sizeof(path), "%s_%dx%d.raw", argv[3], bw, bh);
                failed |= write_raw(path, &out, 0);
                free(out.rgba);
            }
        }
        free(src.rgba);
        return failed;
    }
    if (argc == 5 && strcmp(argv[1], "png") == 0) {
        image_t src;
        int bw, bh, dw, dh;
        if (parse_box(argv[4], &bw, &bh) || load_png(argv[2], &src)) {
            return 1;
        }
        fit(src.w, src.h, bw, bh, &dw, &dh);
        image_t out = resample(&src, dw, dh);
        int err = write_png(argv[3], &out);
        free(out.rgba);
        free(src.rgba);
        return err;
    }
    fprintf(stderr,
        "usage:\n"
        "  coverimg raw <in.png> <out-prefix> WxH[w] [WxH[w] ...]\n"
        "  coverimg png <in.png> <out.png> WxH\n");
    return 2;
}
