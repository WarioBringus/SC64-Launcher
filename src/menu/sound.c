/**
 * @file sound.c
 * @brief Sound component implementation
 * @ingroup ui_components
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <libdragon.h>
#include "audio_player.h"
#include "perf_log.h"
#include "sound.h"

#define DEFAULT_FREQUENCY   (44100)
#define AUDIO_LATENCY       AUDIO_INIT_LATENCY_MS(460)  /* Room for the queue a game launch fills */
#define MENU_AHEAD_MS       (140)                       /* Queue in the menu: effects play this soon after a press */
#define NUM_CHANNELS        (16)

#define BGM_BUILTIN_PATH    "rom:/menu/sounds/bgm.wav64"
#define BGM_MAX_TRACKS      (64)

static wav64_t sfx_cursor, sfx_error, sfx_enter, sfx_exit, sfx_setting, bgm;
static wav64_t sfx_tick, sfx_swish, sfx_pop;

static char *bgm_tracks[BGM_MAX_TRACKS];
static int bgm_track_count = 0;
static int bgm_track_index = 0;
static int bgm_order = SOUND_BGM_ORDER_SHUFFLE;
static float bgm_volume = 0.1f;

static bool sound_initialized = false;
static bool sfx_enabled = false;
static bool bgm_enabled = false;
static bool sfx_opened = false;
static bool bgm_opened = false;
static bool bgm_paused = false;
static bool bgm_faded = false;             /* Faded out for a game launch; stays quiet until turned on again */
static uint32_t bgm_track_generation = 0;  /* Counts playlist tracks started, for "now playing" */
static int menu_ahead_buffers = 6;         /* Audio buffers kept queued in the menu */
static bool load_ahead = false;            /* A game is loading: keep every buffer queued */

/**
 * @brief Mix audio into free buffers, up to the menu's short queue or, while a game loads, all of them
 * (the load blocks for longer than the short queue lasts).
 */
#define LOAD_GAP_NOTE_US    (150000)    /* While a game loads, a longer gap between feeds is noted in the performance log */

/** @brief While a game loads: note in the performance log how low the music's queue got and any long gap. */
static void note_load_audio (void) {
    static uint64_t last_us = 0;
    static uint64_t last_low_us = 0;
    uint64_t now = get_ticks_us();
    if (!load_ahead) {
        last_us = 0;
        return;
    }
    int queued = audio_get_queued_buffers();
    int ms = queued * audio_get_buffer_length() * 1000 / audio_get_frequency();
    if (last_us && now - last_us > LOAD_GAP_NOTE_US) {
        perf_log_event("load: music not fed for %lu ms (%d ms of it left queued)", (unsigned long)((now - last_us) / 1000), ms);
    }
    if (queued <= 2 && now - last_low_us > 250000) {
        perf_log_event("load: music ran low (%d ms queued)", ms);
        last_low_us = now;
    }
    last_us = now;
}

static void fill_audio (void) {
    if (!sound_initialized) {
        return;
    }
    note_load_audio();
    int target = load_ahead ? audio_get_num_buffers() - 1 : menu_ahead_buffers;
    while (audio_get_queued_buffers() < target && audio_can_write()) {
        short *buffer = audio_write_begin();
        mixer_poll(buffer, audio_get_buffer_length());
        audio_write_end();
    }
}

/**
 * @brief Reconfigure the sound system with the specified frequency.
 * 
 * @param frequency The audio frequency.
 */
static void sound_reconfigure (int frequency) {
    if ((frequency > 0) && (audio_get_frequency() != frequency)) {
        
        sound_deinit();

        audio_init(frequency, AUDIO_LATENCY);
        mixer_init(NUM_CHANNELS);
        menu_ahead_buffers = MENU_AHEAD_MS * frequency / 1000 / audio_get_buffer_length();
        if (menu_ahead_buffers < 2) {
            menu_ahead_buffers = 2;
        }

        // Attempt to initialize wav64 compression level 1
        wav64_init_compression(1);

        // Ensure SFX channel can play standard 44.1 kHz effects even if the
        // global mixer/sample rate was reconfigured to a lower value for MP3.
        mixer_ch_set_limits(SOUND_SFX_CHANNEL, 16, DEFAULT_FREQUENCY, 0);

        // Initialize MP3/audioplayer mixer
        audioplayer_mixer_init();
        sound_initialized = true;

        if (sfx_enabled) {
            sound_init_sfx();
        }
        if (bgm_enabled) {
            sound_init_bgm();
            wav64_play(&bgm, SOUND_BGM_CHANNEL);
        }
    }
}

/**
 * @brief Initialize the default sound system.
 */
void sound_init_default (void) {
    sound_reconfigure(DEFAULT_FREQUENCY);
}

/**
 * @brief Initialize the sound system for MP3 playback.
 */
void sound_init_audioplayer_playback (void) {
    // Temporarily disable BGM so it won't be restarted during audio reconfiguration.
    // BGM will be re-enabled when sound_init_default() is called on exit.
    bool bgm_was_enabled = bgm_enabled;
    bgm_enabled = false;
    mixer_ch_stop(SOUND_BGM_CHANNEL);
    sound_reconfigure(audioplayer_get_samplerate());
    bgm_enabled = bgm_was_enabled;
}

/**
 * @brief Initialize the sound effects.
 */
void sound_init_sfx (void) {
    mixer_ch_set_vol(SOUND_SFX_CHANNEL, 0.5f, 0.5f);
    wav64_open(&sfx_cursor, "rom:/menu/sounds/cursorsound.wav64");
    wav64_open(&sfx_exit, "rom:/menu/sounds/back.wav64");
    wav64_open(&sfx_setting, "rom:/menu/sounds/settings.wav64");
    wav64_open(&sfx_enter, "rom:/menu/sounds/enter.wav64");
    wav64_open(&sfx_error, "rom:/menu/sounds/error.wav64");
    wav64_open(&sfx_tick, "rom:/menu/sounds/tick.wav64");
    wav64_open(&sfx_swish, "rom:/menu/sounds/swish.wav64");
    wav64_open(&sfx_pop, "rom:/menu/sounds/pop.wav64");
    sfx_enabled = true;
    sfx_opened = true;
}

/**
 * @brief Open the current playlist track, or the built-in track if the playlist is empty.
 */
static void bgm_open_current (void) {
    if (bgm_opened) {
        mixer_ch_stop(SOUND_BGM_CHANNEL);
        wav64_close(&bgm);
        bgm_opened = false;
    }
    if (bgm_track_count == 0) {
        wav64_open(&bgm, BGM_BUILTIN_PATH);
        wav64_set_loop(&bgm, true);
    } else {
        wav64_open(&bgm, bgm_tracks[bgm_track_index]);
        wav64_set_loop(&bgm, (bgm_track_count == 1) || (bgm_order == SOUND_BGM_ORDER_REPEAT_ONE));
        bgm_track_generation++;
    }
    mixer_ch_set_vol(SOUND_BGM_CHANNEL, bgm_volume, bgm_volume);
    bgm_opened = true;
    bgm_paused = false;
}

/**
 * @brief Pick the track after the current one according to the playlist order.
 */
static int bgm_pick_next (void) {
    if (bgm_track_count <= 1) {
        return 0;
    }
    if (bgm_order == SOUND_BGM_ORDER_SHUFFLE) {
        int next;
        do {
            next = rand() % bgm_track_count;
        } while (next == bgm_track_index);
        return next;
    }
    return (bgm_track_index + 1) % bgm_track_count;
}

/**
 * @brief Check that a file is a wav64 this menu can decode, so wav64_open won't assert.
 */
static bool bgm_file_is_playable (const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    uint8_t head[8];
    int n = read(fd, head, sizeof(head));
    close(fd);
    // "WV64", version 10, format raw (0) or VADPCM (1): the formats initialized in sound_reconfigure.
    return (n == sizeof(head)) && (memcmp(head, "WV64", 4) == 0) && (head[4] == 10) && (head[5] <= 1);
}

static int bgm_compare_paths (const void *a, const void *b) {
    return strcasecmp(*(char * const *)a, *(char * const *)b);
}

/**
 * @brief Initialize the background music.
 */
void sound_init_bgm (void) {
    bgm_open_current();
}

void sound_bgm_load_folder (char *path) {
    for (int i = 0; i < bgm_track_count; i++) {
        free(bgm_tracks[i]);
    }
    bgm_track_count = 0;
    bgm_track_index = 0;

    size_t path_len = strlen(path);
    dir_t info;
    int result = dir_findfirst(path, &info);
    while (result == 0 && bgm_track_count < BGM_MAX_TRACKS) {
        size_t name_len = strlen(info.d_name);
        bool is_wav64 = (info.d_type != DT_DIR) && (name_len > 6) &&
            (strcasecmp(info.d_name + name_len - 6, ".wav64") == 0) &&
            (info.d_name[0] != '.');
        if (is_wav64) {
            char *full = malloc(path_len + 1 + name_len + 1);
            if (full) {
                strcpy(full, path);
                strcat(full, "/");
                strcat(full, info.d_name);
                if (bgm_file_is_playable(full)) {
                    bgm_tracks[bgm_track_count++] = full;
                } else {
                    debugf("BGM: skipping %s (not a VADPCM/raw wav64)\n", full);
                    free(full);
                }
            }
        }
        result = dir_findnext(path, &info);
    }

    qsort(bgm_tracks, bgm_track_count, sizeof(char *), bgm_compare_paths);

    if (bgm_track_count > 1 && bgm_order == SOUND_BGM_ORDER_SHUFFLE) {
        srand(TICKS_READ());
        bgm_track_index = rand() % bgm_track_count;
    }

    if (sound_initialized) {
        bgm_open_current();
        if (bgm_enabled) {
            wav64_play(&bgm, SOUND_BGM_CHANNEL);
        }
    }
}

void sound_bgm_set_order (int order) {
    bgm_order = order;
    if (bgm_opened && bgm_track_count > 0) {
        wav64_set_loop(&bgm, (bgm_track_count == 1) || (bgm_order == SOUND_BGM_ORDER_REPEAT_ONE));
    }
}

void sound_bgm_set_volume (int level) {
    switch (level) {
        case SOUND_BGM_VOLUME_HIGH: bgm_volume = 0.5f; break;
        case SOUND_BGM_VOLUME_MEDIUM: bgm_volume = 0.25f; break;
        default: bgm_volume = 0.1f; break;
    }
    if (!bgm_paused && !bgm_faded) {
        mixer_ch_set_vol(SOUND_BGM_CHANNEL, bgm_volume, bgm_volume);
    }
}

void sound_bgm_next (void) {
    if (!sound_initialized || bgm_track_count == 0) {
        return;
    }
    bgm_track_index = (bgm_order == SOUND_BGM_ORDER_REPEAT_ONE)
        ? (bgm_track_index + 1) % bgm_track_count
        : bgm_pick_next();
    bgm_open_current();
    if (bgm_enabled) {
        wav64_play(&bgm, SOUND_BGM_CHANNEL);
    }
}

void sound_bgm_set_paused (bool paused) {
    if (!sound_initialized || !bgm_opened || !bgm_enabled || bgm_faded || paused == bgm_paused) {
        return;
    }
    bgm_paused = paused;
    // Seeking a VADPCM track is only allowed at its start, so a pause holds the channel still
    // (no samples consumed) and silent, rather than stopping it.
    if (paused) {
        mixer_ch_set_vol(SOUND_BGM_CHANNEL, 0.0f, 0.0f);
        mixer_ch_set_freq(SOUND_BGM_CHANNEL, 0.0f);
    } else {
        mixer_ch_set_freq(SOUND_BGM_CHANNEL, bgm.wave.frequency);
        mixer_ch_set_vol(SOUND_BGM_CHANNEL, bgm_volume, bgm_volume);
    }
}

bool sound_bgm_is_paused (void) {
    return bgm_paused;
}

bool sound_bgm_is_playing (void) {
    return sound_initialized && bgm_enabled && bgm_opened && !bgm_faded && !bgm_paused;
}

const char *sound_bgm_track_name (void) {
    static char name[96];
    if (bgm_track_count == 0) {
        return NULL;
    }
    const char *path = bgm_tracks[bgm_track_index];
    const char *slash = strrchr(path, '/');
    snprintf(name, sizeof(name), "%s", slash ? slash + 1 : path);
    char *dot = strrchr(name, '.');
    if (dot) {
        *dot = '\0';
    }
    return name;
}

uint32_t sound_bgm_track_generation (void) {
    return bgm_track_generation;
}

bool sound_bgm_is_faded (void) {
    return bgm_faded;
}

static void bgm_fade (int duration_ms, bool drain) {
    if (!sound_initialized || !bgm_opened || !bgm_enabled || bgm_faded) {
        return;
    }
    bgm_faded = true;
    if (!bgm_paused) {
        // Step the volume down buffer by buffer as the audio is mixed.
        int frequency = audio_get_frequency();
        int buffer_samples = audio_get_buffer_length();
        int total = duration_ms * frequency / 1000;
        int target = load_ahead ? audio_get_num_buffers() - 1 : menu_ahead_buffers;
        uint64_t give_up = get_ticks_ms() + duration_ms + 1500;
        for (int mixed = 0; mixed < total && get_ticks_ms() < give_up; ) {
            if (audio_get_queued_buffers() >= target || !audio_can_write()) {
                continue;
            }
            float level = bgm_volume * (1.0f - (float)mixed / total);
            mixer_ch_set_vol(SOUND_BGM_CHANNEL, level, level);
            short *buffer = audio_write_begin();
            mixer_poll(buffer, buffer_samples);
            audio_write_end();
            mixed += buffer_samples;
        }
    }
    mixer_ch_stop(SOUND_BGM_CHANNEL);
    bgm_paused = false;
    if (drain) {
        // Let the queued fade play out before the game takes over the console.
        uint64_t start = get_ticks_ms();
        while (audio_get_queued_buffers() > 1 && get_ticks_ms() - start < 1000) {
        }
        load_ahead = false;
    }
}

void sound_bgm_begin_load (void) {
    load_ahead = true;
    fill_audio();
}

void sound_bgm_end_load (void) {
    load_ahead = false;
}

void sound_bgm_prepare_for_load (void) {
    sound_bgm_begin_load();
    // The built-in track lives in the menu's ROM filesystem, which the game is about to overwrite:
    // fade it now (what's already mixed is safe). Tracks from the SD card keep playing while the game loads.
    if (bgm_track_count == 0) {
        bgm_fade(BGM_FADE_MS, false);
    }
    // The sound effects live there too: let the one just played finish before the ROM area changes.
    uint64_t start = get_ticks_ms();
    while (sound_initialized && mixer_ch_playing(SOUND_SFX_CHANNEL) && get_ticks_ms() - start < 400) {
        fill_audio();
    }
    if (sound_initialized) {
        mixer_ch_stop(SOUND_SFX_CHANNEL);
    }
    sfx_enabled = false;    // Nothing else plays from there until the game boots or the load fails.
}

void sound_bgm_keep_alive (void) {
    // Called between chunks of a ROM load.
    sound_poll();
}

void sound_bgm_fade_out (int duration_ms) {
    bgm_fade(duration_ms, load_ahead);
}

/**
 * @brief Enable or disable sound effects.
 * 
 * @param state True to enable, false to disable.
 */
void sound_use_sfx(bool state) {
    sfx_enabled = state;
}

/**
 * @brief Enable or disable background music.
 * 
 * @param state True to enable, false to disable.
 */
void sound_use_bgm(bool state) {
    bgm_enabled = state;
    bgm_paused = false;
    bgm_faded = false;
    if (bgm_enabled && bgm_opened) {
        wav64_play(&bgm, SOUND_BGM_CHANNEL);
        mixer_ch_set_vol(SOUND_BGM_CHANNEL, bgm_volume, bgm_volume);
    } else {
        mixer_ch_stop(SOUND_BGM_CHANNEL);
    }
}

/**
 * @brief Play a sound effect.
 * 
 * @param sfx The sound effect to play.
 */
void sound_play_effect(sound_effect_t sfx) {
    if(sfx_enabled) {
        switch (sfx) {
            case SFX_CURSOR:
                wav64_play(&sfx_cursor, SOUND_SFX_CHANNEL);
                break;
            case SFX_EXIT:
                wav64_play(&sfx_exit, SOUND_SFX_CHANNEL);
                break;
            case SFX_SETTING:
                wav64_play(&sfx_setting, SOUND_SFX_CHANNEL);
                break;
            case SFX_ENTER:
                wav64_play(&sfx_enter, SOUND_SFX_CHANNEL);
                break;
            case SFX_ERROR:
                wav64_play(&sfx_error, SOUND_SFX_CHANNEL);
                break;
            case SFX_TICK:
                wav64_play(&sfx_tick, SOUND_SFX_CHANNEL);
                break;
            case SFX_SWISH:
                wav64_play(&sfx_swish, SOUND_SFX_CHANNEL);
                break;
            case SFX_POP:
                wav64_play(&sfx_pop, SOUND_SFX_CHANNEL);
                break;
            default:
                break;
        } 
    }
}

/**
 * @brief Deinitialize the sound system.
 */
void sound_deinit (void) {
    if (sound_initialized) {
        if (sfx_opened) {
            wav64_close(&sfx_cursor);
            wav64_close(&sfx_exit);
            wav64_close(&sfx_setting);
            wav64_close(&sfx_enter);
            wav64_close(&sfx_error);
            wav64_close(&sfx_tick);
            wav64_close(&sfx_swish);
            wav64_close(&sfx_pop);
            sfx_opened = false;
        }
        if (bgm_opened) {
            wav64_close(&bgm);
            bgm_opened = false;
        }
        mixer_close();
        audio_close();
        sound_initialized = false;
    }
}

/**
 * @brief Poll the sound system to process audio playback.
 */
void sound_poll (void) {
    if (sound_initialized) {
        
        // Move on to the next playlist track when the current one has finished.
        if (bgm_enabled && bgm_opened && !bgm_faded && bgm_track_count > 1 &&
            bgm_order != SOUND_BGM_ORDER_REPEAT_ONE && !mixer_ch_playing(SOUND_BGM_CHANNEL)) {
            bgm_track_index = bgm_pick_next();
            bgm_open_current();
            wav64_play(&bgm, SOUND_BGM_CHANNEL);
        }

        fill_audio();
    }
}
