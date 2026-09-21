/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Audio device layer.
 *
 * Deliberately a SEPARATE library from libbacktrack. The engine has no
 * platform or device dependency at all - that is what lets every question
 * about timing, mixing and routing be answered offline, on Linux, against a
 * Windows target. Nothing in tests/ links this.
 *
 * One device, N channels. There is no aggregation and there will not be:
 * two devices means two clocks, and two clocks means the click drifts away
 * from the backing track over the length of a song.
 */
#ifndef BT_DEVICE_H
#define BT_DEVICE_H

#include "bt_types.h"
#include "bt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_DEVICE_NAME_MAX 128
#define BT_DEVICE_DEFAULT  (-1)

typedef struct {
    int32_t index;                        /* pass to bt_device_open        */
    char    name[BT_DEVICE_NAME_MAX];
    char    api[BT_MAX_NAME];             /* "ASIO", "WASAPI", "ALSA", ... */
    int32_t max_out_channels;
    double  default_sample_rate;
    double  default_low_latency;          /* seconds, as the backend claims */
    bool    is_default_output;
} bt_device_info;

/* Enumeration. init/term are reference-counted and safe to nest. */
bt_err  bt_device_init(void);
void    bt_device_term(void);

int32_t bt_device_count(void);
bt_err  bt_device_get(int32_t i, bt_device_info *out);

/* Case-insensitive substring match on name and (optionally) API. Returns a
 * device index, or BT_DEVICE_DEFAULT when nothing matches - which is a real
 * answer, not an error: a set list should still open on a laptop that does
 * not have the band's interface plugged in. */
int32_t bt_device_find(const char *name_substr, const char *api_substr);

/* Picks the best available device, which is what a caller almost always
 * wants instead of the backend's idea of "default".
 *
 * With an API named, that API is required. With none, host APIs are tried in
 * descending order of how well they serve this job - ASIO, then WASAPI,
 * WDM-KS, Core Audio, JACK, ALSA, and only then DirectSound and MME. The
 * backend's default output device on Windows is the MME one, at around
 * 100 ms of latency, with the same speakers sitting on WASAPI at under 3 ms.
 *
 * Returns BT_DEVICE_DEFAULT if nothing matches at all. */
int32_t bt_device_best(const char *name_substr, const char *api_substr);

/* The API-preference order, highest first, NULL-terminated. Exposed so a UI
 * can explain the choice rather than appearing to make it arbitrarily. */
const char *const *bt_device_api_preference(void);

/* Name of the backend this build can actually use, for diagnostics. */
const char *bt_device_backend(void);

/* ---------------------------------------------------------------------- */

/* Called from the audio thread. Everything the RT rules say applies here:
 * no allocation, no locks, no I/O, no logging. `out` is planar, one buffer
 * per channel, which is exactly what bt_engine_render() wants - the engine's
 * output format was chosen to make this a straight pass-through. */
typedef void (*bt_device_cb)(float *const *out, int32_t nframes, void *user);

typedef struct {
    int32_t index;            /* from enumeration, or BT_DEVICE_DEFAULT    */
    int32_t out_channels;
    int32_t sample_rate;
    int32_t buffer_frames;    /* 0 lets the backend choose                 */
} bt_device_open_cfg;

typedef struct bt_device bt_device;

bt_err bt_device_open(const bt_device_open_cfg *cfg, bt_device_cb cb,
                      void *user, bt_device **out);
bt_err bt_device_start(bt_device *d);
bt_err bt_device_stop(bt_device *d);
void   bt_device_close(bt_device *d);

/* What the backend actually gave us, which is not always what was asked for.
 * Any argument may be NULL. */
void bt_device_actual(const bt_device *d, int32_t *sample_rate,
                      int32_t *buffer_frames, double *latency_sec);

/* Underruns reported by the backend since the stream started. On stage this
 * is the number that matters: it is the count of audible glitches. */
uint64_t bt_device_xruns(const bt_device *d);

/* True once the backend reports the stream is no longer running - which is
 * what a kicked USB cable looks like from here. */
bool bt_device_lost(const bt_device *d);

/* Last backend error text, or "" - static storage, never NULL. */
const char *bt_device_last_error(const bt_device *d);

/* Portable sleep for the UI/pump thread. Exposed here so callers need not
 * take a dependency on the backend headers just to pace a loop, and so the
 * choice of backend stays private to this library. Never call it from the
 * audio callback. */
void bt_device_sleep_ms(int32_t ms);

#ifdef __cplusplus
}
#endif
#endif /* BT_DEVICE_H */
