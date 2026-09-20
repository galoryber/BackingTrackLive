/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * PortAudio backend.
 *
 * PortAudio is asked for paNonInterleaved buffers, so it hands the callback a
 * float** laid out exactly the way bt_engine_render() writes: one buffer per
 * channel. There is no interleaving step anywhere in the audio path, which is
 * why the engine's output format was chosen this way in the first place.
 */
#include "backtrack/bt_device.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "portaudio.h"

#if defined(__STDC_NO_ATOMICS__)
  #define _bt_atomic     volatile
  #define bt_load(p)     (*(p))
  #define bt_store(p, v) (*(p) = (v))
#else
  #include <stdatomic.h>
  #define _bt_atomic     _Atomic
  #define bt_load(p)     atomic_load_explicit((p), memory_order_relaxed)
  #define bt_store(p, v) atomic_store_explicit((p), (v), memory_order_relaxed)
#endif

struct bt_device {
    PaStream    *stream;
    bt_device_cb cb;
    void        *user;
    int32_t      out_channels;
    int32_t      sample_rate;
    int32_t      buffer_frames;
    double       latency;
    char         err[256];
    _bt_atomic unsigned long long xruns;
};

static int g_init_count = 0;

/* ---------------------------------------------------------------- init */

bt_err bt_device_init(void) {
    if (g_init_count > 0) { g_init_count++; return BT_OK; }
    PaError e = Pa_Initialize();
    if (e != paNoError) return BT_ERR_STATE;
    g_init_count = 1;
    return BT_OK;
}

void bt_device_term(void) {
    if (g_init_count <= 0) return;
    if (--g_init_count == 0) Pa_Terminate();
}

const char *bt_device_backend(void) { return "PortAudio"; }

/* --------------------------------------------------------- enumeration */

int32_t bt_device_count(void) {
    if (g_init_count <= 0) return 0;
    PaDeviceIndex n = Pa_GetDeviceCount();
    return (n < 0) ? 0 : (int32_t)n;
}

bt_err bt_device_get(int32_t i, bt_device_info *out) {
    if (!out) return BT_ERR_RANGE;
    if (g_init_count <= 0) return BT_ERR_STATE;

    const PaDeviceInfo *d = Pa_GetDeviceInfo((PaDeviceIndex)i);
    if (!d) return BT_ERR_NOT_FOUND;

    memset(out, 0, sizeof(*out));
    out->index = i;
    snprintf(out->name, sizeof(out->name), "%s", d->name ? d->name : "");

    const PaHostApiInfo *h = Pa_GetHostApiInfo(d->hostApi);
    snprintf(out->api, sizeof(out->api), "%s", (h && h->name) ? h->name : "");

    out->max_out_channels    = (int32_t)d->maxOutputChannels;
    out->default_sample_rate = d->defaultSampleRate;
    out->default_low_latency = d->defaultLowOutputLatency;
    out->is_default_output   = (Pa_GetDefaultOutputDevice() == (PaDeviceIndex)i);
    return BT_OK;
}

static bool ci_contains(const char *hay, const char *needle) {
    if (!needle || !*needle) return true;
    if (!hay) return false;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t k = 0;
        while (k < nl) {
            char a = p[k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            k++;
        }
        if (k == nl) return true;
    }
    return false;
}

int32_t bt_device_find(const char *name_substr, const char *api_substr) {
    int32_t n = bt_device_count();
    for (int32_t i = 0; i < n; i++) {
        bt_device_info info;
        if (bt_device_get(i, &info) != BT_OK) continue;
        if (info.max_out_channels <= 0) continue;     /* inputs only */
        if (!ci_contains(info.name, name_substr)) continue;
        if (!ci_contains(info.api, api_substr)) continue;
        return i;
    }
    return BT_DEVICE_DEFAULT;
}

/* ------------------------------------------------------------ callback */

static int pa_callback(const void *in, void *out, unsigned long frames,
                       const PaStreamCallbackTimeInfo *time,
                       PaStreamCallbackFlags flags, void *user) {
    (void)in;
    (void)time;
    bt_device *d = (bt_device *)user;

    /* An underrun means the audience heard a click. Counting it is the only
     * thing worth doing about it from in here. */
    if (flags & (paOutputUnderflow | paPrimingOutput))
        bt_store(&d->xruns, bt_load(&d->xruns) + 1u);

    d->cb((float *const *)out, (int32_t)frames, d->user);
    return paContinue;
}

/* ---------------------------------------------------------------- open */

static void set_err(bt_device *d, PaError e, const char *what) {
    snprintf(d->err, sizeof(d->err), "%s: %s", what, Pa_GetErrorText(e));
}

bt_err bt_device_open(const bt_device_open_cfg *cfg, bt_device_cb cb,
                      void *user, bt_device **out) {
    if (!cfg || !cb || !out) return BT_ERR_RANGE;
    if (g_init_count <= 0) return BT_ERR_STATE;
    if (cfg->out_channels <= 0 || cfg->out_channels > BT_MAX_OUT_CH) return BT_ERR_RANGE;
    if (cfg->sample_rate <= 0) return BT_ERR_RANGE;

    PaDeviceIndex idx = (cfg->index == BT_DEVICE_DEFAULT)
                      ? Pa_GetDefaultOutputDevice()
                      : (PaDeviceIndex)cfg->index;
    if (idx == paNoDevice) return BT_ERR_NOT_FOUND;

    const PaDeviceInfo *di = Pa_GetDeviceInfo(idx);
    if (!di) return BT_ERR_NOT_FOUND;

    /* Fail here, with a number, rather than letting a set list route a click
     * to a channel this interface does not have. */
    if (cfg->out_channels > di->maxOutputChannels) return BT_ERR_RANGE;

    bt_device *d = (bt_device *)calloc(1, sizeof(*d));
    if (!d) return BT_ERR_ALLOC;
    d->cb            = cb;
    d->user          = user;
    d->out_channels  = cfg->out_channels;
    d->sample_rate   = cfg->sample_rate;
    d->buffer_frames = cfg->buffer_frames;
    bt_store(&d->xruns, 0u);

    PaStreamParameters p;
    memset(&p, 0, sizeof(p));
    p.device           = idx;
    p.channelCount     = cfg->out_channels;
    /* Non-interleaved: PortAudio hands us the planar layout the engine
     * already writes, so the audio path has no interleaving step at all. */
    p.sampleFormat     = paFloat32 | paNonInterleaved;
    p.suggestedLatency = di->defaultLowOutputLatency;

    unsigned long frames = (cfg->buffer_frames > 0)
                         ? (unsigned long)cfg->buffer_frames
                         : paFramesPerBufferUnspecified;

    PaError e = Pa_OpenStream(&d->stream, NULL, &p, (double)cfg->sample_rate,
                              frames, paNoFlag, pa_callback, d);
    if (e != paNoError) {
        set_err(d, e, "Pa_OpenStream");
        /* Keep the message: the caller will want to print it, and it is the
         * difference between "it did not work" and "the rate is unsupported". */
        snprintf(d->err, sizeof(d->err), "Pa_OpenStream: %s", Pa_GetErrorText(e));
        *out = d;
        return (e == paInvalidSampleRate) ? BT_ERR_RATE : BT_ERR_STATE;
    }

    const PaStreamInfo *si = Pa_GetStreamInfo(d->stream);
    if (si) {
        d->sample_rate = (int32_t)si->sampleRate;
        d->latency     = si->outputLatency;
    }
    *out = d;
    return BT_OK;
}

bt_err bt_device_start(bt_device *d) {
    if (!d || !d->stream) return BT_ERR_STATE;
    PaError e = Pa_StartStream(d->stream);
    if (e != paNoError) { set_err(d, e, "Pa_StartStream"); return BT_ERR_STATE; }
    return BT_OK;
}

bt_err bt_device_stop(bt_device *d) {
    if (!d || !d->stream) return BT_ERR_STATE;
    PaError e = Pa_StopStream(d->stream);
    if (e != paNoError) { set_err(d, e, "Pa_StopStream"); return BT_ERR_STATE; }
    return BT_OK;
}

void bt_device_close(bt_device *d) {
    if (!d) return;
    if (d->stream) {
        if (Pa_IsStreamActive(d->stream) == 1) Pa_AbortStream(d->stream);
        Pa_CloseStream(d->stream);
    }
    free(d);
}

void bt_device_actual(const bt_device *d, int32_t *sample_rate,
                      int32_t *buffer_frames, double *latency_sec) {
    if (!d) return;
    if (sample_rate)   *sample_rate   = d->sample_rate;
    if (buffer_frames) *buffer_frames = d->buffer_frames;
    if (latency_sec)   *latency_sec   = d->latency;
}

uint64_t bt_device_xruns(const bt_device *d) {
    return d ? (uint64_t)bt_load(&d->xruns) : 0;
}

bool bt_device_lost(const bt_device *d) {
    if (!d || !d->stream) return false;
    /* PortAudio reports a negative PaError here when the stream has gone -
     * which is what a kicked USB cable looks like from this side. */
    return Pa_IsStreamActive(d->stream) < 0;
}

const char *bt_device_last_error(const bt_device *d) {
    return (d && d->err[0]) ? d->err : "";
}

void bt_device_sleep_ms(int32_t ms) {
    if (ms > 0) Pa_Sleep((long)ms);
}
