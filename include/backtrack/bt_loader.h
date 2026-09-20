/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Background stem loading.
 *
 * Decoding and resampling a song's stems takes seconds. Doing that on the
 * thread that draws the UI means the screen freezes when someone taps a song,
 * which on stage is indistinguishable from a crash.
 *
 * The loader owns one thread and owns the decision of what is in RAM. It is
 * told a window of songs to keep resident and gets on with it: loading what is
 * missing, freeing what has fallen outside, and never touching audio a render
 * could still be walking - it drains the engine before freeing anything.
 *
 * Ownership, stated once: the loader is the only thing that writes
 * bt_track::pcm. Everyone else reads it, and only after the loader has
 * published that the song is resident.
 */
#ifndef BT_LOADER_H
#define BT_LOADER_H

#include "bt_types.h"
#include "bt_error.h"
#include "bt_model.h"
#include "bt_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bt_loader bt_loader;

/* `sl` and `eng` are borrowed and must outlive the loader. `eng` may be NULL
 * when no engine is bound yet; it is used only to drain in-flight renders
 * before freeing. */
bt_err bt_loader_start(bt_setlist *sl, int32_t sample_rate, bt_engine *eng,
                       bt_loader **out);
/* Stops the thread and frees every stem it loaded. */
void   bt_loader_stop(bt_loader *l);

/* Keep songs [lo, hi] resident; anything else may be freed. Returns at once -
 * the work happens on the loader thread. */
void   bt_loader_set_window(bt_loader *l, int32_t lo, int32_t hi);

/* These lock, so they are not const-qualified: pretending otherwise would
 * mean casting the qualifier away inside, which is a lie the compiler is
 * right to reject. */
bool   bt_loader_resident(bt_loader *l, int32_t song);

/* Blocks until `song` is resident or its load has failed. Use it when the
 * user has explicitly chosen a song that is not in the window yet - jumping
 * across the set - which is the one case worth waiting for. Returns
 * BT_ERR_STATE on timeout. */
bt_err bt_loader_wait(bt_loader *l, int32_t song, int32_t timeout_ms);

/* Bytes of PCM currently resident, for the UI to show. */
size_t bt_loader_resident_bytes(bt_loader *l);

/* Number of loads that have completed, so a caller can tell that something
 * changed without polling every song. */
uint64_t bt_loader_generation(bt_loader *l);

/* The most recent load failure, or BT_OK. `song` receives the index. A stem
 * missing three songs ahead must surface, but must not stop the show. */
bt_err bt_loader_last_error(bt_loader *l, int32_t *song);

#ifdef __cplusplus
}
#endif
#endif /* BT_LOADER_H */
