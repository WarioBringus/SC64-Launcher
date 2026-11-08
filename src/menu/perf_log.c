/**
 * @file perf_log.c
 * @brief Optional performance log for the launcher (config.ini [launcher] perf_log = true).
 *
 * Once a second, one line: the screen, frames shown, frame time (average, worst, how many ran
 * long), the time the menu worked in a frame, cover reads from the SD card, and steps the
 * selection took. Lines are kept in memory and written to menu/cache/launcher/perf-log.txt only
 * while the controller has been left alone for a moment, or when the launcher is left, so the
 * writing never lands in the middle of scrolling.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <libdragon.h>

#include "perf_log.h"

#ifndef LAUNCHER_BUILD
#define LAUNCHER_BUILD "Unknown"
#endif

#define PERF_LOG_FILE       "menu/cache/launcher/perf-log.txt"
#define BUFFER_SIZE         (24 * 1024)
#define FLUSH_IDLE_MS       (1500)      /* Controller left alone this long before writing */
#define LONG_FRAME_US       (20000)     /* Frames longer than this count as long (a 60 Hz frame is 16.7 ms) */

static bool enabled = false;
static const char *storage_prefix = NULL;
static char buffer[BUFFER_SIZE];
static size_t used = 0;
static bool header_written = false;

static uint64_t start_us = 0;
static uint64_t last_frame_us = 0;
static uint64_t work_start_us = 0;
static uint64_t last_input_ms = 0;
static char screen[24] = "";

/* This second's totals */
static uint64_t second_start_us = 0;
static int frames = 0;
static uint64_t frame_total_us = 0;
static uint32_t frame_max_us = 0;
static int long_frames = 0;
static uint64_t work_total_us = 0;
static uint32_t work_max_us = 0;
static int reads = 0;
static uint64_t read_total_us = 0;
static uint32_t read_bytes = 0;
static int steps = 0;

void perf_log_init (bool enable, const char *prefix) {
    enabled = enable;
    storage_prefix = prefix;
    start_us = get_ticks_us();
}

static void add_line (const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buffer + used, sizeof(buffer) - used, fmt, args);
    va_end(args);
    if (n > 0 && used + n < sizeof(buffer)) {
        used += n;
    }
}

static void flush (const char *why) {
    if (used == 0 || !storage_prefix) {
        return;
    }
    uint64_t started = get_ticks_us();
    char path[256];
    snprintf(path, sizeof(path), "%s%s", storage_prefix, PERF_LOG_FILE);
    FILE *f = fopen(path, "a");
    if (f) {
        fwrite(buffer, 1, used, f);
        fprintf(f, "# written (%s) in %.1f ms\n", why, (get_ticks_us() - started) / 1000.0f);
        fclose(f);
    }
    used = 0;
}

static void end_second (uint64_t now_us) {
    if (frames > 0) {
        if (!header_written) {
            add_line("\n# launcher %s - one line per second\n"
                     "# time(s) screen frames frame_avg(ms) frame_max(ms) long_frames(>20ms) work_avg(ms) work_max(ms) "
                     "card_reads read_ms read_kb steps\n", LAUNCHER_BUILD);
            header_written = true;
        }
        add_line("%7.1f %-14s %3d %5.1f %5.1f %3d %5.1f %5.1f %3d %5.1f %4lu %3d\n",
                 (now_us - start_us) / 1e6, screen, frames, frame_total_us / 1000.0f / frames, frame_max_us / 1000.0f,
                 long_frames, work_total_us / 1000.0f / frames, work_max_us / 1000.0f, reads, read_total_us / 1000.0f,
                 (unsigned long)(read_bytes / 1024), steps);
    }
    second_start_us = now_us;
    frames = long_frames = reads = steps = 0;
    frame_total_us = work_total_us = read_total_us = 0;
    frame_max_us = work_max_us = 0;
    read_bytes = 0;
}

void perf_log_frame_begin (const char *screen_name, bool input, bool step) {
    if (!enabled) {
        return;
    }
    uint64_t now = get_ticks_us();
    if (strcmp(screen, screen_name) != 0) {
        end_second(now);
        snprintf(screen, sizeof(screen), "%s", screen_name);
    } else if (now - second_start_us >= 1000000) {
        end_second(now);
    }
    if (last_frame_us) {
        uint32_t frame_us = (uint32_t)(now - last_frame_us);
        frames++;
        frame_total_us += frame_us;
        frame_max_us = (frame_us > frame_max_us) ? frame_us : frame_max_us;
        long_frames += (frame_us > LONG_FRAME_US);
    }
    last_frame_us = now;
    work_start_us = now;
    steps += step;
    if (input) {
        last_input_ms = now / 1000;
    }
}

void perf_log_frame_end (void) {
    if (!enabled) {
        return;
    }
    uint64_t now = get_ticks_us();
    uint32_t work_us = (uint32_t)(now - work_start_us);
    work_total_us += work_us;
    work_max_us = (work_us > work_max_us) ? work_us : work_max_us;
    if (used > 0 && (now / 1000 - last_input_ms) >= FLUSH_IDLE_MS) {
        flush("controller idle");
        last_frame_us = 0;      // The write's own time isn't a frame.
    } else if (used > sizeof(buffer) - 512) {
        flush("buffer full");
        last_frame_us = 0;
    }
}

void perf_log_read (uint32_t bytes, uint32_t us) {
    if (!enabled) {
        return;
    }
    reads++;
    read_bytes += bytes;
    read_total_us += us;
}

void perf_log_leave (void) {
    if (!enabled) {
        return;
    }
    end_second(get_ticks_us());
    flush("left the launcher");
    last_frame_us = 0;
}
