/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The real-time engine: transport, click synthesis, mixing and routing.
 *
 * Everything bt_engine_render() touches is resolved and allocated in
 * bt_engine_set_song(). The render path does arithmetic over preallocated
 * buffers and nothing else - no allocation, no locks, no I/O, no wall clock.
 * tests/test_rtsafe.c enforces that mechanically.
 */
#include "backtrack/bt_engine.h"

#include <stdlib.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if defined(__STDC_NO_ATOMICS__)
  /* Fallback for toolchains without C11 atomics. The only shared scalars are
   * a 64-bit position and two flags, written by one thread and read by one
   * other; this is adequate on the platforms we target, and Phase 2 replaces
   * it with a proper per-platform shim if MSVC forces the issue. */
  #define _bt_atomic          volatile
  #define bt_load(p)          (*(p))
  #define bt_store(p, v)      (*(p) = (v))
#else
  #include <stdatomic.h>
  #define _bt_atomic          _Atomic
  #define bt_load(p)          atomic_load_explicit((p), memory_order_relaxed)
  #define bt_store(p, v)      atomic_store_explicit((p), (v), memory_order_relaxed)
#endif

/* A resolved track: logical names already turned into pointers and channel
 * indices, so the render loop never looks anything up. */
typedef struct {
    const float *const *pcm;
    int32_t  channels;
    bt_frame frames;
    bt_frame offset;         /* frames; may be negative */
    float    gain;           /* linear */
    int32_t  ch[BT_MAX_BUS_CH];
    int32_t  nch;
    bool     is_click;
    bool     active;
} bt_rtrack;

struct bt_engine {
    bt_engine_cfg  cfg;
    bt_tempo_map   tempo;

    bt_rtrack      trk[BT_MAX_TRACKS];
    int32_t        ntrk;

    float         *click_accent;
    float         *click_beat;
    int32_t        click_len;
    bt_click_voice voice;

    bt_frame       end_frame;
    bt_frame       count_in_frame;

    _bt_atomic bt_frame playhead;
    _bt_atomic int      playing;   /* int, not bool: atomic_bool is awkward   */
    _bt_atomic int      finished;
};

static bt_frame ms_to_frames(double ms, int32_t sr) {
    return (bt_frame)llround(ms * (double)sr / 1000.0);
}

static float db_to_lin(double db) {
    return (float)pow(10.0, db / 20.0);
}

/* ------------------------------------------------------------------ click */

void bt_click_voice_defaults(bt_click_voice *v) {
    if (!v) return;
    v->accent_hz = 1600.0;
    v->beat_hz   =  900.0;
    v->decay_ms  =   28.0;
    v->gain      =    0.7;
}

/* An exponentially decaying sine burst. Starts at zero amplitude and phase
 * zero, so there is no onset discontinuity to add its own click to the click. */
static void synth_click(float *dst, int32_t n, double hz, double decay_ms,
                        double gain, int32_t sr) {
    double tau = (decay_ms / 1000.0) * (double)sr / 4.0;   /* ~4 tau to silence */
    if (tau < 1.0) tau = 1.0;
    for (int32_t i = 0; i < n; i++) {
        double env = exp(-(double)i / tau);
        double s   = sin(2.0 * M_PI * hz * (double)i / (double)sr);
        dst[i] = (float)(s * env * gain);
    }
}

bt_err bt_engine_set_click_voice(bt_engine *e, const bt_click_voice *v) {
    if (!e || !v) return BT_ERR_RANGE;
    if (v->decay_ms <= 0.0 || v->decay_ms > 500.0) return BT_ERR_RANGE;
    if (v->accent_hz <= 0.0 || v->beat_hz <= 0.0)  return BT_ERR_RANGE;

    int32_t n = (int32_t)(v->decay_ms * (double)e->cfg.sample_rate / 1000.0);
    if (n < 1) n = 1;

    float *a = (float *)malloc((size_t)n * sizeof(float));
    float *b = (float *)malloc((size_t)n * sizeof(float));
    if (!a || !b) { free(a); free(b); return BT_ERR_ALLOC; }

    synth_click(a, n, v->accent_hz, v->decay_ms, v->gain, e->cfg.sample_rate);
    synth_click(b, n, v->beat_hz,   v->decay_ms, v->gain, e->cfg.sample_rate);

    free(e->click_accent);
    free(e->click_beat);
    e->click_accent = a;
    e->click_beat   = b;
    e->click_len    = n;
    e->voice        = *v;
    return BT_OK;
}

/* ----------------------------------------------------------- lifecycle */

bt_err bt_engine_create(const bt_engine_cfg *cfg, bt_engine **out) {
    if (!cfg || !out) return BT_ERR_RANGE;
    if (cfg->sample_rate <= 0 || cfg->out_channels <= 0
        || cfg->out_channels > BT_MAX_OUT_CH || cfg->max_block_frames <= 0)
        return BT_ERR_RANGE;

    bt_engine *e = (bt_engine *)calloc(1, sizeof(*e));
    if (!e) return BT_ERR_ALLOC;
    e->cfg = *cfg;

    bt_click_voice v;
    bt_click_voice_defaults(&v);
    bt_err err = bt_engine_set_click_voice(e, &v);
    if (err != BT_OK) { free(e); return err; }

    bt_store(&e->playhead, (bt_frame)0);
    bt_store(&e->playing, 0);
    bt_store(&e->finished, 0);
    *out = e;
    return BT_OK;
}

void bt_engine_destroy(bt_engine *e) {
    if (!e) return;
    free(e->click_accent);
    free(e->click_beat);
    free(e);
}

bt_err bt_engine_set_song(bt_engine *e, const bt_song *song, const bt_device_cfg *dev) {
    if (!e || !song || !dev) return BT_ERR_RANGE;
    if (song->ntracks > BT_MAX_TRACKS) return BT_ERR_RANGE;

    bt_store(&e->playing, 0);
    bt_store(&e->finished, 0);
    bt_store(&e->playhead, (bt_frame)0);

    memset(e->trk, 0, sizeof(e->trk));
    e->ntrk  = 0;
    e->tempo = song->tempo;

    for (int32_t i = 0; i < song->ntracks; i++) {
        const bt_track *t = &song->track[i];
        bt_rtrack      *r = &e->trk[e->ntrk];

        /* Resolve the logical bus now. A set list that names a bus this
         * machine does not have is a configuration error worth surfacing at
         * load time, not a silent dropout at the gig. */
        const bt_bus *bus = bt_device_find_bus(dev, t->bus);
        if (!bus) return BT_ERR_NOT_FOUND;

        for (int32_t k = 0; k < bus->nch; k++) {
            if (bus->ch[k] >= e->cfg.out_channels) return BT_ERR_RANGE;
            r->ch[k] = bus->ch[k];
        }
        r->nch      = bus->nch;
        r->gain     = t->muted ? 0.0f : db_to_lin(t->gain_db);
        r->offset   = ms_to_frames((double)t->offset_ms, e->cfg.sample_rate);
        r->is_click = (t->type == BT_TRACK_CLICK);

        if (!r->is_click) {
            if (!t->pcm) return BT_ERR_STATE;     /* audio not loaded yet */
            r->pcm      = (const float *const *)t->pcm;
            r->channels = t->channels;
            r->frames   = t->frames;
        }
        r->active = true;
        e->ntrk++;
    }

    e->end_frame = bt_song_length(song, e->cfg.sample_rate);

    int64_t beats_in = (int64_t)song->count_in_bars * (int64_t)song->tempo.sig_num;
    e->count_in_frame = bt_tempo_beat_frame(&e->tempo, -beats_in, e->cfg.sample_rate);

    return BT_OK;
}

/* ---------------------------------------------------------------- transport */

void bt_engine_play(bt_engine *e) {
    if (!e) return;
    bt_store(&e->finished, 0);
    bt_store(&e->playing, 1);
}

void bt_engine_stop(bt_engine *e) {
    if (!e) return;
    bt_store(&e->playing, 0);
}

void bt_engine_panic(bt_engine *e) {
    if (!e) return;
    bt_store(&e->playing, 0);
    bt_store(&e->playhead, (bt_frame)0);
    bt_store(&e->finished, 0);
}

void bt_engine_seek(bt_engine *e, bt_frame f) {
    if (!e) return;
    bt_store(&e->playhead, f);
    bt_store(&e->finished, 0);
}

void bt_engine_start_with_count_in(bt_engine *e) {
    if (!e) return;
    bt_store(&e->playhead, e->count_in_frame);
    bt_store(&e->finished, 0);
    bt_store(&e->playing, 1);
}

bool     bt_engine_playing(const bt_engine *e)  { return e && bt_load(&e->playing) != 0; }
bt_frame bt_engine_playhead(const bt_engine *e) { return e ? bt_load(&e->playhead) : 0; }
bool     bt_engine_finished(const bt_engine *e) { return e && bt_load(&e->finished) != 0; }

/* ------------------------------------------------------------------ render */

/* Mixes one source channel into one output channel. */
static void mix_into(float *dst, const float *src, int32_t n, float g) {
    for (int32_t i = 0; i < n; i++) dst[i] += src[i] * g;
}

static void render_audio(const bt_rtrack *r, float *const *out,
                         bt_frame ph, int32_t n) {
    /* Source index that lines up with out[0]. A positive track offset delays
     * the stem, so it subtracts here. */
    bt_frame s0 = ph - r->offset;

    int32_t lo = 0;
    if (s0 < 0) {
        if (-s0 >= n) return;                 /* entirely before the stem */
        lo = (int32_t)(-s0);
    }
    bt_frame avail = r->frames - (s0 + lo);
    if (avail <= 0) return;                   /* entirely past the stem */
    int32_t hi = lo + (int32_t)((avail < (bt_frame)(n - lo)) ? avail : (bt_frame)(n - lo));
    int32_t len = hi - lo;
    if (len <= 0) return;

    if (r->nch >= r->channels) {
        /* Mono stem into a stereo bus feeds both sides. */
        for (int32_t bc = 0; bc < r->nch; bc++) {
            int32_t sc = (r->channels == 1) ? 0 : bc;
            if (sc >= r->channels) sc = r->channels - 1;
            mix_into(out[r->ch[bc]] + lo, r->pcm[sc] + (s0 + lo), len, r->gain);
        }
    } else {
        /* Stereo stem into a mono bus sums, halved to keep headroom. */
        float g = r->gain / (float)r->channels;
        for (int32_t sc = 0; sc < r->channels; sc++)
            mix_into(out[r->ch[0]] + lo, r->pcm[sc] + (s0 + lo), len, g);
    }
}

static void render_click(const bt_engine *e, const bt_rtrack *r,
                         float *const *out, bt_frame ph, int32_t n) {
    const int32_t clen = e->click_len;

    /* Beats whose burst could overlap this block: one that began up to clen
     * frames ago is still sounding, so the search starts behind the playhead.
     * Handling the tail this way means the click needs no voice state, which
     * is what keeps render() free of anything that has to be reset on seek. */
    int64_t b0 = bt_tempo_frame_beat(&e->tempo, ph - clen, e->cfg.sample_rate);
    int64_t b1 = bt_tempo_frame_beat(&e->tempo, ph + n,    e->cfg.sample_rate) + 1;

    for (int64_t b = b0; b <= b1; b++) {
        bt_frame bf = bt_tempo_beat_frame(&e->tempo, b, e->cfg.sample_rate);

        bt_frame start = bf - ph;             /* offset of the burst in block */
        int32_t  lo    = (start > 0) ? (int32_t)start : 0;
        if (lo >= n) continue;

        int32_t src_off = (start < 0) ? (int32_t)(-start) : 0;
        if (src_off >= clen) continue;

        int32_t len = clen - src_off;
        if (len > n - lo) len = n - lo;
        if (len <= 0) continue;

        const float *src = bt_tempo_is_downbeat(&e->tempo, b)
                         ? e->click_accent : e->click_beat;

        if (r->nch >= 1) {
            for (int32_t bc = 0; bc < r->nch; bc++)
                mix_into(out[r->ch[bc]] + lo, src + src_off, len, r->gain);
        }
    }
}

void bt_engine_render(bt_engine *e, float *const *out, int32_t nframes) {
    if (!e || !out || nframes <= 0) return;

    for (int32_t c = 0; c < e->cfg.out_channels; c++)
        memset(out[c], 0, (size_t)nframes * sizeof(float));

    if (bt_load(&e->playing) == 0) return;

    const bt_frame ph = bt_load(&e->playhead);

    for (int32_t i = 0; i < e->ntrk; i++) {
        const bt_rtrack *r = &e->trk[i];
        if (!r->active || r->gain == 0.0f) continue;
        if (r->is_click) render_click(e, r, out, ph, nframes);
        else             render_audio(r, out, ph, nframes);
    }

    const bt_frame next = ph + nframes;
    bt_store(&e->playhead, next);

    /* End of song: stop on the sample, and let the UI thread decide what
     * on_end means. The engine never loads anything itself. */
    if (next >= e->end_frame) {
        bt_store(&e->playing, 0);
        bt_store(&e->finished, 1);
    }
}
