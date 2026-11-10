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
static uint64_t draw_start_us = 0;
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
static uint64_t input_total_us = 0;     /* Of the work: reading the controller and acting on it */
static uint64_t draw_total_us = 0;      /* Of the work: drawing (including waits for the RDP) */
static uint64_t part_total_us[PERF_PARTS];
static uint32_t frame_part_us[PERF_PARTS];  /* This frame's parts, for a slow frame's breakdown */
static uint32_t frame_read_us = 0;
static int frame_reads = 0;

#define SLOW_FRAME_US       (60000)     /* A frame whose work takes longer is noted with its breakdown */
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
                     "card_reads read_ms read_kb steps input_avg(ms) draw_avg(ms) text_avg(ms) covers_avg(ms) polls_avg(ms) wait_avg(ms) shapes_avg(ms) header_hints_avg(ms) layouts_per_frame\n", LAUNCHER_BUILD);
            header_written = true;
        }
        // Waiting: the rest of each frame, mostly for the RDP to finish or for the next vertical blank.
        // Signed, and never below zero: the polls can run before a second's first frame is counted, and a
        // wrapped-around unsigned value is too large for the FPU to convert (an unimplemented-operation exception).
        int64_t wait_us = (int64_t)frame_total_us - (int64_t)work_total_us - (int64_t)part_total_us[PERF_POLLS];
        float wait = (wait_us > 0 ? (int32_t)(wait_us / frames) : 0) / 1000.0f;
        add_line("%7.1f %-14s %3d %5.1f %5.1f %3d %5.1f %5.1f %3d %5.1f %4lu %3d %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.2f\n",
                 (now_us - start_us) / 1e6, screen, frames, frame_total_us / 1000.0f / frames, frame_max_us / 1000.0f,
                 long_frames, work_total_us / 1000.0f / frames, work_max_us / 1000.0f, reads, read_total_us / 1000.0f,
                 (unsigned long)(read_bytes / 1024), steps, input_total_us / 1000.0f / frames,
                 draw_total_us / 1000.0f / frames, part_total_us[PERF_TEXT] / 1000.0f / frames,
                 part_total_us[PERF_COVERS] / 1000.0f / frames, part_total_us[PERF_POLLS] / 1000.0f / frames,
                 wait, part_total_us[PERF_SHAPES] / 1000.0f / frames, part_total_us[PERF_CHROME] / 1000.0f / frames,
                 (float)part_total_us[PERF_LAYOUTS] / frames);
    }
    second_start_us = now_us;
    frames = long_frames = reads = steps = 0;
    frame_total_us = work_total_us = read_total_us = input_total_us = draw_total_us = 0;
    memset(part_total_us, 0, sizeof(part_total_us));
    frame_max_us = work_max_us = 0;
    read_bytes = 0;
}

void perf_log_frame_begin (const char *screen_name, bool input, bool step) {
    if (!enabled) {
        return;
    }
    uint64_t now = get_ticks_us();
    static bool first_noted = false;
    if (!first_noted && strcmp(screen_name, "other") != 0) {
        first_noted = true;
        add_line("\n# launcher %s: first frame %lu ms after power-on (%s)\n", LAUNCHER_BUILD,
                 (unsigned long)(now / 1000), screen_name);
    }
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
    draw_start_us = 0;
    memset(frame_part_us, 0, sizeof(frame_part_us));
    frame_read_us = 0;
    frame_reads = 0;
    steps += step;
    if (input) {
        last_input_ms = now / 1000;
    }
}

void perf_log_event(const char *fmt, ...);

void perf_log_frame_end (void) {
    if (!enabled) {
        return;
    }
    uint64_t now = get_ticks_us();
    uint32_t work_us = (uint32_t)(now - work_start_us);
    if (draw_start_us) {
        input_total_us += draw_start_us - work_start_us;
        draw_total_us += now - draw_start_us;
    }
    work_total_us += work_us;
    work_max_us = (work_us > work_max_us) ? work_us : work_max_us;
    // Over budget (but not a slow frame): one frame's breakdown every 10 s, to see where the time goes.
    static uint64_t last_sample_us = 0;
    if (work_us > 16700 && work_us <= SLOW_FRAME_US && now - last_sample_us > 10000000) {
        last_sample_us = now;
        perf_log_event("frame on %s: %.1f ms (background %.1f, text %.1f in %lu draws, covers %.1f, shapes %.1f, "
            "header+hints %.1f)", screen, work_us / 1000.0f, frame_part_us[PERF_BACKGROUND] / 1000.0f,
            frame_part_us[PERF_TEXT] / 1000.0f, (unsigned long)frame_part_us[PERF_TEXT_DRAWS],
            frame_part_us[PERF_COVERS] / 1000.0f, frame_part_us[PERF_SHAPES] / 1000.0f,
            frame_part_us[PERF_CHROME] / 1000.0f);
    }
    if (work_us > SLOW_FRAME_US) {
        uint32_t input_us = draw_start_us ? (uint32_t)(draw_start_us - work_start_us) : 0;
        perf_log_event("slow frame on %s: %.1f ms (input %.1f, drawing %.1f: text %.1f, covers %.1f, shapes %.1f, "
            "header+hints %.1f, %lu layouts built, %d card reads %.1f ms)", screen, work_us / 1000.0f, input_us / 1000.0f,
            draw_start_us ? (now - draw_start_us) / 1000.0f : 0.0f, frame_part_us[PERF_TEXT] / 1000.0f,
            frame_part_us[PERF_COVERS] / 1000.0f, frame_part_us[PERF_SHAPES] / 1000.0f, frame_part_us[PERF_CHROME] / 1000.0f,
            (unsigned long)frame_part_us[PERF_LAYOUTS], frame_reads, frame_read_us / 1000.0f);
    }
    if (used > 0 && (now / 1000 - last_input_ms) >= FLUSH_IDLE_MS) {
        flush("controller idle");
        last_frame_us = 0;      // The write's own time isn't a frame.
    } else if (used > sizeof(buffer) - 512) {
        flush("buffer full");
        last_frame_us = 0;
    }
}

void perf_log_add (perf_part_t part, uint32_t us) {
    if (enabled) {
        part_total_us[part] += us;
        frame_part_us[part] += us;
    }
}

void perf_log_draw_begin (void) {
    if (enabled) {
        draw_start_us = get_ticks_us();
    }
}

void perf_log_read (uint32_t bytes, uint32_t us) {
    if (!enabled) {
        return;
    }
    reads++;
    read_bytes += bytes;
    read_total_us += us;
    frame_reads++;
    frame_read_us += us;
}

#define MAX_EVENTS          (250)

void perf_log_event (const char *fmt, ...) {
    static int events = 0;
    if (!enabled || events >= MAX_EVENTS) {
        return;
    }
    events++;
    char line[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    add_line("# %.1f s: %s%s\n", (get_ticks_us() - start_us) / 1e6, line, events == MAX_EVENTS ? " (no more events noted)" : "");
}

void perf_log_leave (void) {
    if (!enabled) {
        return;
    }
    end_second(get_ticks_us());
    flush("left the launcher");
    last_frame_us = 0;
}
