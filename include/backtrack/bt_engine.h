/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The real-time engine.
 *
 * bt_engine_render() is a pure function of engine state: no device, no clock,
 * no threads, no allocation. That is what lets the entire musical behaviour of
 * the product be verified offline, on any platform, without audio hardware.
 */
#ifndef BT_ENGINE_H
#define BT_ENGINE_H

#include "bt_types.h"
#include "bt_error.h"
#include "bt_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bt_engine bt_engine;

typedef struct {
    int32_t sample_rate;
    int32_t out_channels;      /* physical channels on the device          */
    int32_t max_block_frames;  /* largest nframes render() will ever see   */
} bt_engine_cfg;

bt_err bt_engine_create(const bt_engine_cfg *cfg, bt_engine **out);
void   bt_engine_destroy(bt_engine *e);

/* Bind a song (already audio-loaded) and a bus map. Resolves every track's
 * logical bus to physical channels *now*, so render() never does a lookup.
 *
 * Safe to call while the audio thread is rendering. The engine keeps two
 * resolved slots and publishes the live one atomically, so a render either
 * sees the whole previous song or the whole new one, never a mixture. Not
 * RT-safe itself - call it from the UI/loader thread. */
bt_err bt_engine_set_song(bt_engine *e, const bt_song *song, const bt_device_cfg *dev);

/* Blocks until no render is executing, so the audio buffers a previously
 * bound song owned can be freed. Call it before freeing a song's PCM.
 *
 * Needs no configuration and costs nothing when render() is driven from the
 * calling thread, as it is offline: the in-flight count is provably zero at
 * that moment. Bounded, so a stalled or vanished stream cannot hang the
 * caller. */
void bt_engine_sync(bt_engine *e);

/* ---- Transport. All RT-safe and lock-free. ---------------------------- */
void     bt_engine_play(bt_engine *e);     /* from the current playhead     */
void     bt_engine_stop(bt_engine *e);     /* stop, hold position           */
void     bt_engine_panic(bt_engine *e);    /* stop + silence immediately    */
void     bt_engine_seek(bt_engine *e, bt_frame f);
/* Arm the count-in: seek to -(count_in_bars) and play. */
void     bt_engine_start_with_count_in(bt_engine *e);

bool     bt_engine_playing(const bt_engine *e);
bt_frame bt_engine_playhead(const bt_engine *e);
/* True once the playhead has passed the end of every stem. */
bool     bt_engine_finished(const bt_engine *e);

/* ---- The RT callback. -------------------------------------------------- */
/*
 * Renders `nframes` into `out`, a planar array of `out_channels` buffers.
 * Overwrites (does not accumulate). nframes must be <= max_block_frames.
 *
 * MUST NOT allocate, lock, log, or make a syscall. Enforced by
 * tests/test_rtsafe.c.
 */
void bt_engine_render(bt_engine *e, float *const *out, int32_t nframes);

/* ---- Click voicing ----------------------------------------------------- */
typedef struct {
    double accent_hz;    /* downbeat pitch                                  */
    double beat_hz;      /* other beats                                     */
    double decay_ms;     /* exponential decay of the burst                  */
    double gain;         /* linear, 0..1                                    */
} bt_click_voice;

void bt_click_voice_defaults(bt_click_voice *v);
/* Re-synthesises the click samples. Allocates; not RT-safe. */
bt_err bt_engine_set_click_voice(bt_engine *e, const bt_click_voice *v);

#ifdef __cplusplus
}
#endif
#endif /* BT_ENGINE_H */
