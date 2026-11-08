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

/** @brief A cover or spine read from the SD card. */
void perf_log_read(uint32_t bytes, uint32_t us);

/** @brief Leaving the launcher (e.g. to start a game): write what's kept. */
void perf_log_leave(void);

#endif
