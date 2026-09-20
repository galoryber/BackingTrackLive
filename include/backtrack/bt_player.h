/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The set list player: the layer between a set list on disk and the engine.
 *
 * bt_engine knows about exactly one song and never loads anything. This owns
 * the rest of what a performance actually needs - which song is current, when
 * one ends, what `on_end` means, and keeping the next song in RAM before
 * anybody asks for it.
 *
 * Threading, stated plainly because it matters:
 *
 *   bt_player_render()  is the audio callback. It forwards to the engine and
 *                       does nothing else. RT-safe.
 *   bt_player_tick()    is called from the UI thread, regularly. Everything
 *                       that allocates, reads a file or decides anything lives
 *                       here.
 *
 * A load inside tick() blocks the caller, but not the audio: the device drives
 * the callback, and the currently playing song is already resident. So a slow
 * load stalls the UI, never the music. Phase 2 moves loading to its own thread
 * behind this same API.
 */
#ifndef BT_PLAYER_H
#define BT_PLAYER_H

#include "bt_types.h"
#include "bt_error.h"
#include "bt_model.h"
#include "bt_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bt_player bt_player;

typedef struct {
    int32_t sample_rate;
    int32_t out_channels;
    int32_t max_block_frames;
    /* Songs to keep resident beyond the current one. 1 - the next song - is
     * what makes `on_end: next` seamless and song switching instant. Raise it
     * only if RAM is free; a full 40-song set will not fit. */
    int32_t preload_ahead;
} bt_player_cfg;

/* The set list is borrowed, not owned: the caller keeps it alive for the
 * player's lifetime and frees it afterwards. */
bt_err bt_player_create(const bt_player_cfg *cfg, bt_setlist *setlist,
                        const bt_device_cfg *dev, bt_player **out);
void   bt_player_destroy(bt_player *p);

/* ---- Selection. Loads the song if needed, binds it, leaves it stopped. --- */
bt_err  bt_player_select(bt_player *p, int32_t song_index);
bt_err  bt_player_next(bt_player *p);
bt_err  bt_player_prev(bt_player *p);

int32_t bt_player_current(const bt_player *p);
int32_t bt_player_count(const bt_player *p);
const bt_song *bt_player_song(const bt_player *p);

/* ---- Transport. Forwarded to the engine; all RT-safe. ------------------- */
void     bt_player_start(bt_player *p);   /* from the count-in               */
void     bt_player_play(bt_player *p);    /* from the current playhead       */
void     bt_player_stop(bt_player *p);
void     bt_player_panic(bt_player *p);
bool     bt_player_playing(const bt_player *p);
bt_frame bt_player_playhead(const bt_player *p);

/* ---- The audio callback. Forwards only. -------------------------------- */
void bt_player_render(bt_player *p, float *const *out, int32_t nframes);

/* ---- The UI-thread pump. ----------------------------------------------- */
typedef enum {
    BT_TICK_IDLE = 0,
    BT_TICK_PRELOADED,    /* a song entered the preload window              */
    BT_TICK_SONG_ENDED,   /* finished, on_end: stop - waiting for a human   */
    BT_TICK_ADVANCED      /* finished, on_end: next - already playing it    */
} bt_tick_result;

/* Acts on end-of-song and keeps the preload window populated. Safe and cheap
 * to call every UI frame.
 *
 * `on_end: next` starts the following song at frame 0 with NO count-in - a
 * segue should not have clicks counted into it.
 *
 * Advancing happens on the next tick, not on the sample, so the seam is one
 * render block wide: measured at 5.4 ms with a 512-frame block. That is
 * gapless to an audience and not sample-accurate. A medley that has to be
 * musically locked belongs in one song file with the segue rendered in.
 *
 * Closing that gap properly means pre-binding the next song and swapping it
 * inside the audio callback - worth doing only if a real musical need for a
 * sample-accurate transition turns up, which "one song file" already answers
 * more simply. */
bt_err bt_player_tick(bt_player *p, bt_tick_result *result);

/* ---- Residency, for the UI to show and for tests to assert. ------------- */
size_t bt_player_resident_bytes(const bt_player *p);
bool   bt_player_song_resident(const bt_player *p, int32_t song_index);

#ifdef __cplusplus
}
#endif
#endif /* BT_PLAYER_H */
