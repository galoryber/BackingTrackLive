/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#ifndef BT_MODEL_H
#define BT_MODEL_H

#include "bt_types.h"
#include "bt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Tempo
 *
 * A song's tempo is stored as a *map* from the outset even though the UI will
 * initially expose a single BPM. Adding the song with a tempo change in the
 * bridge must not require a file-format migration.
 *
 * Beat 0 is the first downbeat and sits at `downbeat_frames` into the audio.
 * Downloaded stems routinely start with silence or a pickup bar, so this
 * offset is what actually makes the click line up.
 * ------------------------------------------------------------------------ */
typedef struct {
    int64_t start_beat;   /* this tempo takes effect at this beat index */
    double  bpm;
} bt_tempo_seg;

typedef struct {
    bt_tempo_seg seg[BT_MAX_TEMPO_SEG];
    int32_t      nseg;
    int32_t      sig_num, sig_den;
    double       downbeat_ms;    /* where beat 0 lands in the audio */
} bt_tempo_map;

/* Frame at which `beat` occurs. Computed from the beat index, never
 * accumulated, so a 3-hour set does not drift. Beats may be negative
 * (count-in). Sample rate is passed in rather than stored: the model is
 * portable between machines, the device rate is not. */
bt_frame bt_tempo_beat_frame(const bt_tempo_map *tm, int64_t beat, int32_t sample_rate);

/* Inverse: the highest beat index whose frame is <= `f`. */
int64_t  bt_tempo_frame_beat(const bt_tempo_map *tm, bt_frame f, int32_t sample_rate);

/* True when `beat` is the first beat of a bar. */
bool     bt_tempo_is_downbeat(const bt_tempo_map *tm, int64_t beat);

/* ---------------------------------------------------------------------------
 * Tracks and songs
 * ------------------------------------------------------------------------ */
typedef enum {
    BT_TRACK_AUDIO = 0,   /* a decoded stem                                */
    BT_TRACK_CLICK        /* synthesised from the tempo map                */
} bt_track_type;

typedef struct {
    char          name[BT_MAX_NAME];
    bt_track_type type;
    char          bus[BT_MAX_NAME];   /* LOGICAL bus name, e.g. "inear"    */
    char          file[BT_MAX_PATH];  /* relative to the set list file     */
    double        gain_db;
    int32_t       offset_ms;          /* nudge; may be negative            */
    bool          muted;

    /* Populated by the loader, not by the file. */
    float       **pcm;                /* planar, [channels][frames]        */
    int32_t       channels;
    bt_frame      frames;
} bt_track;

typedef enum {
    BT_ON_END_STOP = 0,   /* default: wait for a human                     */
    BT_ON_END_NEXT        /* advance and play the next song                */
} bt_on_end;

typedef struct {
    char         title[BT_MAX_NAME];
    char         artist[BT_MAX_NAME];
    bt_tempo_map tempo;
    int32_t      count_in_bars;
    bt_on_end    on_end;
    bt_track     track[BT_MAX_TRACKS];
    int32_t      ntracks;
} bt_song;

typedef struct {
    char     name[BT_MAX_NAME];
    char     dir[BT_MAX_PATH];   /* directory of the set list file         */
    bt_song *song;
    int32_t  nsongs;
    int32_t  cap;
} bt_setlist;

/* ---------------------------------------------------------------------------
 * Buses  (machine-local; loaded from device.json, never from setlist.json)
 * ------------------------------------------------------------------------ */
typedef struct {
    char    name[BT_MAX_NAME];
    int32_t ch[BT_MAX_BUS_CH];   /* physical output channel indices        */
    int32_t nch;                 /* 1 = mono, 2 = stereo                   */
} bt_bus;

typedef struct {
    bt_bus  bus[BT_MAX_BUSES];
    int32_t nbuses;
    int32_t sample_rate;
    int32_t buffer_frames;
    char    device[BT_MAX_NAME];
    /* Host API to prefer: "ASIO", "WASAPI", "WDM-KS", "DirectSound", "MME",
     * "Core Audio", "ALSA", "JACK". Matched as a case-insensitive substring.
     *
     * This matters more than it looks. On Windows the same speakers appear
     * under four APIs with wildly different latency - measured on one machine:
     * WASAPI 2.7 ms, WDM-KS 10 ms, DirectSound 120 ms, MME 90 ms - and
     * PortAudio's "default device" is the MME one. Leaving this empty picks
     * the best API present rather than the default. */
    char    api[BT_MAX_NAME];
} bt_device_cfg;

/* ---------------------------------------------------------------------------
 * Loading
 * ------------------------------------------------------------------------ */
bt_err bt_setlist_load_file(const char *path, bt_setlist **out, int *err_line);
bt_err bt_setlist_load_mem (const char *text, size_t len, const char *dir,
                            bt_setlist **out, int *err_line);
void   bt_setlist_free(bt_setlist *sl);

bt_err bt_device_cfg_load_file(const char *path, bt_device_cfg *out, int *err_line);
bt_err bt_device_cfg_load_mem (const char *text, size_t len, bt_device_cfg *out, int *err_line);
void   bt_device_cfg_defaults(bt_device_cfg *cfg);

/* ---------------------------------------------------------------------------
 * Saving
 *
 * Serialisation is exact: numbers are emitted at the shortest precision that
 * parses back to the identical value, so load -> save -> load is lossless and
 * saving twice produces byte-identical output. A set list is a text file the
 * user may also edit by hand and keep in git; gratuitous churn in a diff is a
 * defect.
 *
 * The *_to_json forms allocate; the caller frees. The *_save_file forms write
 * to a temporary alongside the target and rename over it, so an interrupted
 * save cannot leave a half-written set list where a working one used to be.
 * ------------------------------------------------------------------------ */
bt_err bt_setlist_to_json  (const bt_setlist *sl, char **out, size_t *len);
bt_err bt_setlist_save_file(const bt_setlist *sl, const char *path);

bt_err bt_device_cfg_to_json  (const bt_device_cfg *cfg, char **out, size_t *len);
bt_err bt_device_cfg_save_file(const bt_device_cfg *cfg, const char *path);

const bt_bus *bt_device_find_bus(const bt_device_cfg *cfg, const char *name);

/* Decode every audio stem of `song` into RAM. Off the RT thread. */
bt_err bt_song_load_audio(bt_song *song, const char *dir, int32_t sample_rate);
void   bt_song_free_audio(bt_song *song);

/* Total frames of the song: the longest (stem length + its offset), plus the
 * count-in. Zero when the song has no audio. */
bt_frame bt_song_length(const bt_song *song, int32_t sample_rate);

/* Bytes of PCM currently resident for this song. Surfaced in the UI so nobody
 * is surprised by the preload budget at soundcheck. */
size_t   bt_song_pcm_bytes(const bt_song *song);

#ifdef __cplusplus
}
#endif
#endif /* BT_MODEL_H */
