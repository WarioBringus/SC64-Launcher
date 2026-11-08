/*
 * coverimg - PC-side image helper for the launcher's cover thumbnails.
 *
 *   coverimg raw <in.png> <out-prefix> WxH [WxH ...]
 *       Fit the image inside each box (keeping its aspect ratio, never enlarging) and write
 *       <out-prefix>_<W>x<H>.raw in the launcher thumbnail format:
 *       "CVR1", width (u16 BE), height (u16 BE), then RGBA5551 pixels (BE), row by row.
 *
 *   A box written WxHr turns the image a quarter turn clockwise first (a box's top-edge art becomes
 *   an upright spine); WxHs takes a slice from the middle of the image with the box's shape first
 *   (a stand-in spine from the front). Either can be combined with w.
 *
 *   A box written WxHu enlarges a smaller image to fit the box (Catmull-Rom, sharper than the area
 *   average); otherwise images are only ever made smaller.
 *
 *   A box written WxHw is for the launcher's 16:9 screen: the cover is fitted as for WxH, trimmed
 *   to even sizes as the menu does, then squeezed to 3/4 width so the menu can copy it 1:1.
 *   <out-prefix>_<W>x<H>w.raw holds "CVW1", stored width, height, the 4:3 width it stands
 *   for (u16 BE each), two zero bytes, then the RGBA5551 pixels.
 *
 *   coverimg box <in.png> <out-prefix> WxH [WxH ...]
 *       As raw, for box art drawn edge to edge in its tile: see-through lines along the image's
 *       edges (many scans have a few) are trimmed first, and a cover within 5% of the box's size
 *       is stretched to fill it, so no gap shows between the cover and the selection ring.
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

#include <math.h>
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

/* Enlarging fit: the largest size with the image's shape that fits the box. */
static void fit_up (int src_w, int src_h, int max_w, int max_h, int *dst_w, int *dst_h) {
    if (src_w * max_h > src_h * max_w) {
        *dst_w = max_w;
        *dst_h = (src_h * max_w) / src_w;
    } else {
        *dst_h = max_h;
        *dst_w = (src_w * max_h) / src_h;
    }
    if (*dst_w < 1) *dst_w = 1;
    if (*dst_h < 1) *dst_h = 1;
}

/* Box art: a cover this close to the box's size fills it (a stretch too small to see). */
static void fill_near_fit (int bw, int bh, int *dw, int *dh) {
    if (*dw * 100 >= bw * 95 && *dh * 100 >= bh * 95) {
        *dw = bw;
        *dh = bh;
    }
}

/* A row or column counts as empty when nearly all of it is see-through. */
static int line_is_clear (const image_t *img, int x, int y, int dx, int dy, int n) {
    int clear = 0;
    for (int i = 0; i < n; i++, x += dx, y += dy) {
        clear += img->rgba[((size_t)y * img->w + x) * 4 + 3] < 128;
    }
    return clear * 10 >= n * 9;
}

/* Drop empty rows and columns along the edges (no more than 8% of the image from any side). */
static image_t trim_clear_edges (const image_t *src) {
    int x0 = 0, y0 = 0, x1 = src->w, y1 = src->h;
    int max_x = src->w * 8 / 100, max_y = src->h * 8 / 100;
    while (x0 < max_x && line_is_clear(src, x0, 0, 0, 1, src->h)) x0++;
    while (src->w - x1 < max_x && line_is_clear(src, x1 - 1, 0, 0, 1, src->h)) x1--;
    while (y0 < max_y && line_is_clear(src, x0, y0, 1, 0, x1 - x0)) y0++;
    while (src->h - y1 < max_y && line_is_clear(src, x0, y1 - 1, 1, 0, x1 - x0)) y1--;
    image_t out = { x1 - x0, y1 - y0, malloc((size_t)(x1 - x0) * (y1 - y0) * 4) };
    for (int y = 0; y < out.h; y++) {
        memcpy(&out.rgba[(size_t)y * out.w * 4], &src->rgba[((size_t)(y0 + y) * src->w + x0) * 4], (size_t)out.w * 4);
    }
    return out;
}

static double catmull_rom (double x) {
    x = fabs(x);
    if (x < 1.0) {
        return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    }
    if (x < 2.0) {
        return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    }
    return 0.0;
}

/*
 * One axis of a resize on premultiplied RGBA: n lines of in_len samples (step apart within a line,
 * line_step between lines) to out_len. Larger uses Catmull-Rom, smaller an area average.
 */
static void resample_axis (const double *in, double *out, int n, int in_len, int out_len, int step, int line_step,
                           int out_step, int out_line_step) {
    double scale = (double)in_len / out_len;
    for (int line = 0; line < n; line++) {
        const double *src = in + (size_t)line * line_step;
        double *dst = out + (size_t)line * out_line_step;
        for (int i = 0; i < out_len; i++) {
            double acc[4] = { 0, 0, 0, 0 };
            double total = 0;
            if (out_len >= in_len) {
                double center = (i + 0.5) * scale - 0.5;
                int first = (int)floor(center) - 1;
                for (int k = first; k < first + 4; k++) {
                    double w = catmull_rom(center - k);
                    int at = (k < 0) ? 0 : (k >= in_len ? in_len - 1 : k);
                    for (int c = 0; c < 4; c++) {
                        acc[c] += src[(size_t)at * step + c] * w;
                    }
                    total += w;
                }
            } else {
                double x0 = i * scale, x1 = (i + 1) * scale;
                for (int k = (int)x0; k < in_len && k < x1; k++) {
                    double w = ((k + 1 < x1) ? k + 1 : x1) - ((k > x0) ? k : x0);
                    for (int c = 0; c < 4; c++) {
                        acc[c] += src[(size_t)k * step + c] * w;
                    }
                    total += w;
                }
            }
            for (int c = 0; c < 4; c++) {
                dst[(size_t)i * out_step + c] = (total != 0) ? acc[c] / total : 0;
            }
        }
    }
}

/* Resize that may enlarge: separable, on premultiplied alpha so see-through pixels don't bleed. */
static image_t resample_sharp (const image_t *src, int dst_w, int dst_h) {
    size_t in_n = (size_t)src->w * src->h;
    double *pre = malloc(in_n * 4 * sizeof(double));
    for (size_t i = 0; i < in_n; i++) {
        double a = src->rgba[i * 4 + 3] / 255.0;
        for (int c = 0; c < 3; c++) {
            pre[i * 4 + c] = src->rgba[i * 4 + c] * a;
        }
        pre[i * 4 + 3] = a;
    }
    double *mid = malloc((size_t)dst_w * src->h * 4 * sizeof(double));
    resample_axis(pre, mid, src->h, src->w, dst_w, 4, src->w * 4, 4, dst_w * 4);
    double *fin = malloc((size_t)dst_w * dst_h * 4 * sizeof(double));
    resample_axis(mid, fin, dst_w, src->h, dst_h, dst_w * 4, 4, dst_w * 4, 4);

    image_t out = { dst_w, dst_h, malloc((size_t)dst_w * dst_h * 4) };
    for (size_t i = 0; i < (size_t)dst_w * dst_h; i++) {
        double a = fin[i * 4 + 3];
        a = (a < 0) ? 0 : (a > 1 ? 1 : a);
        for (int c = 0; c < 3; c++) {
            double v = (a > 0) ? fin[i * 4 + c] / a : 0;
            out.rgba[i * 4 + c] = (uint8_t)((v < 0) ? 0 : (v > 255 ? 255 : v + 0.5));
        }
        out.rgba[i * 4 + 3] = (uint8_t)(a * 255 + 0.5);
    }
    free(pre);
    free(mid);
    free(fin);
    return out;
}

static image_t resample_area (const image_t *src, int dst_w, int dst_h);

/* Resize to dst_w x dst_h: an area average when only shrinking, else resample_sharp. */
static image_t resample (const image_t *src, int dst_w, int dst_h) {
    if (dst_w <= src->w && dst_h <= src->h) {
        return resample_area(src, dst_w, dst_h);
    }
    return resample_sharp(src, dst_w, dst_h);
}

/* Area-average resample to dst_w x dst_h (alpha-weighted colour). */
static image_t resample_area (const image_t *src, int dst_w, int dst_h) {
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

/* Flags after WxH: 'w' 16:9 (squeezed), 'r' turned upright, 's' a middle slice. */
static int box_has (const char *s, char flag) {
    const char *x = strchr(s, 'x');
    while (x && *x && (*x == 'x' || (*x >= '0' && *x <= '9'))) {
        x++;
    }
    return x && strchr(x, flag) != NULL;
}

static int box_is_wide (const char *s) {
    return box_has(s, 'w');
}

/* A quarter turn clockwise. */
static image_t rotate_cw (const image_t *src) {
    image_t out = { src->h, src->w, malloc((size_t)src->w * src->h * 4) };
    for (int y = 0; y < src->h; y++) {
        for (int x = 0; x < src->w; x++) {
            memcpy(&out.rgba[((size_t)x * out.w + (src->h - 1 - y)) * 4], &src->rgba[((size_t)y * src->w + x) * 4], 4);
        }
    }
    return out;
}

/* The middle of the image, cut to the shape of a bw x bh box. */
static image_t middle_slice (const image_t *src, int bw, int bh) {
    int w = src->w, h = src->h;
    if (w * bh > h * bw) {
        w = (h * bw) / bh;
    } else {
        h = (w * bh) / bw;
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    int x0 = (src->w - w) / 2, y0 = (src->h - h) / 2;
    image_t out = { w, h, malloc((size_t)w * h * 4) };
    for (int y = 0; y < h; y++) {
        memcpy(&out.rgba[(size_t)y * w * 4], &src->rgba[((size_t)(y0 + y) * src->w + x0) * 4], (size_t)w * 4);
    }
    return out;
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
    if (argc >= 5 && (strcmp(argv[1], "raw") == 0 || strcmp(argv[1], "box") == 0)) {
        int box_art = (strcmp(argv[1], "box") == 0);
        image_t src;
        if (load_png(argv[2], &src)) {
            return 1;
        }
        if (box_art) {
            image_t trimmed = trim_clear_edges(&src);
            free(src.rgba);
            src = trimmed;
        }
        int failed = 0;
        for (int i = 4; i < argc; i++) {
            int bw, bh, dw, dh;
            if (parse_box(argv[i], &bw, &bh)) {
                fprintf(stderr, "bad box %s\n", argv[i]);
                return 2;
            }
            image_t shaped = { 0, 0, NULL };
            if (box_has(argv[i], 'r')) {
                shaped = rotate_cw(&src);
            } else if (box_has(argv[i], 's')) {
                shaped = middle_slice(&src, bw, bh);
            }
            const image_t *from = shaped.rgba ? &shaped : &src;
            if (box_has(argv[i], 'u')) {
                fit_up(from->w, from->h, bw, bh, &dw, &dh);
            } else {
                fit(from->w, from->h, bw, bh, &dw, &dh);
            }
            if (shaped.rgba && box_has(argv[i], 's')) {
                dw = bw;    // A stand-in spine fills the spine's size even from a small front.
                dh = bh;
            } else if ((box_art && !shaped.rgba) || box_has(argv[i], 'r')) {
                fill_near_fit(bw, bh, &dw, &dh);    // Box art and spines fill their box when close.
            }
            char path[1024];
            if (box_is_wide(argv[i])) {
                // The menu trims odd sizes by a pixel; the squeezed width is kept even for the same reason.
                int lw = (dw > 1) ? (dw & ~1) : dw;
                int lh = (dh > 1) ? (dh & ~1) : dh;
                int pw = 2 * (int)(lw * 0.375 + 0.5);
                if (pw < 2) {
                    pw = 2;
                }
                image_t out = resample(from, pw, lh);
                snprintf(path, sizeof(path), "%s_%s.raw", argv[3], argv[i]);
                failed |= write_raw(path, &out, lw);
                free(out.rgba);
            } else {
                image_t out = resample(from, dw, dh);
                snprintf(path, sizeof(path), "%s_%s.raw", argv[3], argv[i]);
                failed |= write_raw(path, &out, 0);
                free(out.rgba);
            }
            free(shaped.rgba);
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
        "  coverimg raw|box <in.png> <out-prefix> WxH[w] [WxH[w] ...]\n"
        "  coverimg png <in.png> <out.png> WxH\n");
    return 2;
}
