/**
 * @file perf_log.h
 * @brief Optional performance log for the launcher, written to menu/cache/launcher/perf-log.txt.
 * @ingroup menu
 */

#ifndef PERF_LOG_H__
#define PERF_LOG_H__

#include <stdbool.h>
#include <stdint.h>

/** @brief Turn the log on or off; prefix is the SD card's ("sd:/"). */
void perf_log_init(bool enable, const char *prefix);

/** @brief A frame starts: the screen shown, whether any button or direction is held, whether the selection moved. */
void perf_log_frame_begin(const char *screen_name, bool input, bool step);

/** @brief The frame's work is done (may write the log while the controller rests). */
void perf_log_frame_end(void);

/** @brief The screen's input handling is done and its drawing starts (splits the frame's work). */
void perf_log_draw_begin(void);

/** @brief Parts of a frame timed separately (perf_log_add). */
typedef enum {
    PERF_TEXT,      /**< Laying out and drawing text */
    PERF_COVERS,    /**< Drawing covers, spines and tiles */
    PERF_POLLS,     /**< Between frames: music, the PNG decoder, USB */
    PERF_SHAPES,    /**< Rounded fills, rings and cover corner masks (partly inside PERF_COVERS) */
    PERF_CHROME,    /**< The header and the hints bar (its text and shapes included) */
    PERF_PARTS,
} perf_part_t;

/** @brief Add time to one part of this second's frames (cheap enough to call often). */
void perf_log_add(perf_part_t part, uint32_t us);

/** @brief A cover or spine read from the SD card. */
void perf_log_read(uint32_t bytes, uint32_t us);

/** @brief Leaving the launcher (e.g. to start a game): write what's kept. */
void perf_log_leave(void);

#endif
