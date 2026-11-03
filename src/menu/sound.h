/**
 * @file sound.h
 * @brief Menu Sound
 * @ingroup menu
 */

#ifndef SOUND_H__
#define SOUND_H__

#include <stdbool.h>
#include <stdint.h>

#define SOUND_SFX_CHANNEL           (0) /**< First Channel of sound effects 0-7 (8 in total) */
#define SOUND_BGM_CHANNEL           (8) /**< First Channel for background music 8-9 (2 in total [stereo]) */
#define BGM_FADE_MS                 (500) /**< Music fade-out when a game starts */
#define SOUND_MP3_PLAYER_CHANNEL    (10) /**< First Channel for MP3 player sound 10-15 (6 in total [surround sound possible]) */


/**
 * @brief Enumeration of available sound effects for menu interactions.
 * 
 * This enumeration defines the different sound effects that can be used
 * for menu interactions.
 */
typedef enum {
    SFX_CURSOR,  /**< Sound effect for cursor movement */
    SFX_ERROR,   /**< Sound effect for error */
    SFX_ENTER,   /**< Sound effect for entering a menu */
    SFX_EXIT,    /**< Sound effect for exiting a menu */
    SFX_SETTING, /**< Sound effect for changing a setting */
} sound_effect_t;

/** @brief Order in which background music tracks from the SD card play. */
typedef enum {
    SOUND_BGM_ORDER_SHUFFLE = 0,    /**< Random track each time */
    SOUND_BGM_ORDER_IN_ORDER = 1,   /**< Alphabetical, then loop the list */
    SOUND_BGM_ORDER_REPEAT_ONE = 2, /**< Loop the current track */
} sound_bgm_order_t;

/** @brief Background music volume levels. */
typedef enum {
    SOUND_BGM_VOLUME_LOW = 0,       /**< The stock menu's level */
    SOUND_BGM_VOLUME_MEDIUM = 1,
    SOUND_BGM_VOLUME_HIGH = 2,
} sound_bgm_volume_t;

/**
 * @brief Initialize the default sound system.
 * 
 * This function initializes the default sound system, setting up
 * necessary resources and configurations.
 */
void sound_init_default(void);

/**
 * @brief Initialize the Audioplayer playback system.
 * 
 * This function initializes the audioplayer playback system, preparing it
 * for playing audio files.
 */
void sound_init_audioplayer_playback(void);

/**
 * @brief Initialize the sound effects system.
 * 
 * This function initializes the sound effects system, setting up
 * necessary resources and configurations for playing sound effects.
 */
void sound_init_sfx(void);

/**
 * @brief Enable or disable sound effects.
 * 
 * @param enable True to enable sound effects, false to disable.
 */
void sound_use_sfx(bool enable);

/**
 * @brief Initialize the background music system.
 * 
 * This function initializes the background music system, setting up
 * necessary resources and configurations for playing background music.
 */
void sound_init_bgm(void);


/**
 * @brief Enable or disable background music.
 * 
 * @param enable True to enable background music, false to disable.
 */
void sound_use_bgm(bool enable);

/**
 * @brief Use the .wav64 files in a folder as the background music playlist.
 *
 * Files that are not playable wav64 (wrong header or compression) are skipped.
 * With no playable files, the built-in track is used.
 *
 * @param path Folder to scan, e.g. "sd:/menu/music".
 */
void sound_bgm_load_folder(char *path);

/**
 * @brief Set the playlist order.
 *
 * @param order One of sound_bgm_order_t.
 */
void sound_bgm_set_order(int order);

/**
 * @brief Set the background music volume.
 *
 * @param level One of sound_bgm_volume_t.
 */
void sound_bgm_set_volume(int level);

/**
 * @brief Skip to the next track in the playlist.
 */
void sound_bgm_next(void);

/**
 * @brief Number of playable tracks found in the music folder.
 */
int sound_bgm_track_count(void);

/** @brief Pause (hold silent where it is) or resume the background music. */
void sound_bgm_set_paused(bool paused);

/** @brief Whether the background music is paused. */
bool sound_bgm_is_paused(void);

/** @brief Whether background music is audible now (on, not paused, not faded out). */
bool sound_bgm_is_playing(void);

/** @brief The current playlist track's file name without extension, or NULL for the built-in track. */
const char *sound_bgm_track_name(void);

/** @brief Goes up each time a playlist track starts. */
uint32_t sound_bgm_track_generation(void);

/** @brief Fade the music out over duration_ms (blocks) and stop it, e.g. before a game boots. */
void sound_bgm_fade_out(int duration_ms);

/** @brief Whether the music was faded out (for a game launch). */
bool sound_bgm_is_faded(void);

/**
 * @brief Get sound ready for a ROM load: SD card music keeps playing, the built-in track fades out,
 * and sound effects stop (they live in the ROM area the game is loaded into).
 */
void sound_bgm_prepare_for_load(void);

/** @brief Keep the music fed during a long blocking load (call it between chunks). */
void sound_bgm_keep_alive(void);

/** @brief A game launch has started: queue as much audio as there is room for, to cover the pauses. */
void sound_bgm_begin_load(void);

/** @brief The launch failed: back to the menu's short audio queue. */
void sound_bgm_end_load(void);

/**
 * @brief Play a specified sound effect.
 * 
 * @param sfx The sound effect to play, as defined in sound_effect_t.
 */
void sound_play_effect(sound_effect_t sfx);

/**
 * @brief Deinitialize the sound system.
 * 
 * This function deinitializes the sound system, releasing any resources
 * that were allocated.
 */
void sound_deinit(void);

/**
 * @brief Poll the sound system.
 * 
 * This function polls the sound system, updating its state as necessary.
 */
void sound_poll(void);

#endif /* SOUND_H__ */
