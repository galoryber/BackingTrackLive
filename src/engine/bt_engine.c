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

#if defined(_WIN32)
  #include <windows.h>
  static void bt_sleep_100us(void) { Sleep(1); }
#else
  #include <time.h>
  static void bt_sleep_100us(void) {
      struct timespec ts = { 0, 100000L };   /* 100 us */
      nanosleep(&ts, NULL);
  }
#endif

#if defined(__STDC_NO_ATOMICS__)
  /* Fallback for toolchains without C11 atomics. The only shared scalars are
   * a 64-bit position and two flags, written by one thread and read by one
   * other; this is adequate on the platforms we target, and Phase 2 replaces
   * it with a proper per-platform shim if MSVC forces the issue. */
  #define _bt_atomic          volatile
  #define bt_load(p)          (*(p))
  #define bt_store(p, v)      (*(p) = (v))
  #define bt_load_acq(p)      (*(p))
  #define bt_store_rel(p, v)  (*(p) = (v))
  #define bt_load_seq(p)      (*(p))
  #define bt_inc_seq(p)       (++*(p))
  #define bt_dec_rel(p)       (--*(p))
#else
  #include <stdatomic.h>
  #define _bt_atomic          _Atomic
  #define bt_load(p)          atomic_load_explicit((p), memory_order_relaxed)
  #define bt_store(p, v)      atomic_store_explicit((p), (v), memory_order_relaxed)
  /* Publishing a freshly built slot needs release/acquire: relaxed would let
   * the audio thread observe the new slot index before the slot's contents. */
  #define bt_load_acq(p)      atomic_load_explicit((p), memory_order_acquire)
  #define bt_store_rel(p, v)  atomic_store_explicit((p), (v), memory_order_release)
  /* The in-flight count needs a real read-modify-write, and the increment
   * needs to be sequentially consistent: a relaxed store would be free to
   * sink below the acquire-load of `active`, letting sync() observe zero for
   * a render that has already chosen its slot. That is not theoretical -
   * ThreadSanitizer catches it on the second song change. */
  #define bt_load_seq(p)      atomic_load_explicit((p), memory_order_seq_cst)
  #define bt_inc_seq(p)       atomic_fetch_add_explicit((p), 1, memory_order_seq_cst)
  #define bt_dec_rel(p)       atomic_fetch_sub_explicit((p), 1, memory_order_release)
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

/* One fully resolved song. The engine keeps two and publishes the index of
 * the live one atomically, so the audio thread only ever walks a slot that is
 * completely written. Rewriting the array in place - which is what this used
 * to do - is a data race against every render in flight, and the kind that
 * works in testing and crashes on stage. */
typedef struct {
    bt_rtrack    trk[BT_MAX_TRACKS];
    int32_t      ntrk;
    bt_tempo_map tempo;
    bt_frame     end_frame;
    bt_frame     count_in_frame;
} bt_slot;

struct bt_engine {
    bt_engine_cfg  cfg;

    bt_slot        slot[2];
    _bt_atomic int active;            /* slot index render() walks           */

    float         *click_accent;
    float         *click_beat;
    int32_t        click_len;
    bt_click_voice voice;

    /* Number of renders currently executing. bt_engine_sync() drains it.
     *
     * A count of in-flight renders, rather than a flag saying "a device is
     * attached", is the whole trick: when render() is driven from the calling
     * thread - btrender, every offline test - it is provably zero at the
     * moment set_song runs, so nothing waits. When a driver thread is calling
     * it, the count is what set_song actually needs to know. There is no
     * configuration to get wrong. */
    _bt_atomic int in_render;

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
    bt_store(&e->in_render, 0);
    bt_store_rel(&e->active, 0);
    *out = e;
    return BT_OK;
}

/* Waits until no render is executing, so the slot a render might have been
 * reading - and the PCM that slot points at - can be rewritten or freed.
 *
 * Once this returns, any render that starts will acquire the currently
 * published slot, so the other slot is nobody's. */
void bt_engine_sync(bt_engine *e) {
    if (!e) return;

    /* Bounded so a stalled or vanished stream cannot hang the UI thread. The
     * bound is a safety net, not the expected path: draining takes at most
     * one callback. */
    double block_ms = 1000.0 * (double)e->cfg.max_block_frames
                             / (double)e->cfg.sample_rate;
    int budget_100us = (int)(block_ms * 40.0) + 200;
    if (budget_100us > 5000) budget_100us = 5000;      /* 500 ms ceiling */

    for (int i = 0; i < budget_100us; i++) {
        if (bt_load_seq(&e->in_render) == 0) return;
        bt_sleep_100us();
    }
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

    /* Build into the slot render() is NOT walking. Waiting first is what
     * guarantees no render is still inside it from the previous swap. */
    const int cur = bt_load(&e->active);
    const int nxt = 1 - cur;
    bt_engine_sync(e);

    bt_slot *sl = &e->slot[nxt];
    memset(sl, 0, sizeof(*sl));
    sl->tempo = song->tempo;

    for (int32_t i = 0; i < song->ntracks; i++) {
        const bt_track *t = &song->track[i];
        bt_rtrack      *r = &sl->trk[sl->ntrk];

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
        sl->ntrk++;
    }

    sl->end_frame = bt_song_length(song, e->cfg.sample_rate);

    int64_t beats_in = (int64_t)song->count_in_bars * (int64_t)song->tempo.sig_num;
    sl->count_in_frame = bt_tempo_beat_frame(&sl->tempo, -beats_in, e->cfg.sample_rate);

    /* Publish. Release pairs with the acquire in render(), so a thread that
     * sees the new index is guaranteed to see the fully written slot. */
    bt_store_rel(&e->active, nxt);
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
    bt_store(&e->playhead, e->slot[bt_load_acq(&e->active)].count_in_frame);
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

static void render_click(const bt_engine *e, const bt_slot *sl,
                         const bt_rtrack *r,
                         float *const *out, bt_frame ph, int32_t n) {
    const int32_t clen = e->click_len;

    /* Beats whose burst could overlap this block: one that began up to clen
     * frames ago is still sounding, so the search starts behind the playhead.
     * Handling the tail this way means the click needs no voice state, which
     * is what keeps render() free of anything that has to be reset on seek. */
    int64_t b0 = bt_tempo_frame_beat(&sl->tempo, ph - clen, e->cfg.sample_rate);
    int64_t b1 = bt_tempo_frame_beat(&sl->tempo, ph + n,    e->cfg.sample_rate) + 1;

    for (int64_t b = b0; b <= b1; b++) {
        bt_frame bf = bt_tempo_beat_frame(&sl->tempo, b, e->cfg.sample_rate);

        bt_frame start = bf - ph;             /* offset of the burst in block */
        int32_t  lo    = (start > 0) ? (int32_t)start : 0;
        if (lo >= n) continue;

        int32_t src_off = (start < 0) ? (int32_t)(-start) : 0;
        if (src_off >= clen) continue;

        int32_t len = clen - src_off;
        if (len > n - lo) len = n - lo;
        if (len <= 0) continue;

        const float *src = bt_tempo_is_downbeat(&sl->tempo, b)
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

    /* Announce before choosing a slot, so bt_engine_sync() cannot miss us. */
    bt_inc_seq(&e->in_render);

    /* Read the slot index exactly once. Re-reading it mid-block could mix the
     * first half of one song with the second half of another. */
    const bt_slot *sl = &e->slot[bt_load_acq(&e->active)];

    if (bt_load(&e->playing) != 0) {
        const bt_frame ph = bt_load(&e->playhead);

        for (int32_t i = 0; i < sl->ntrk; i++) {
            const bt_rtrack *r = &sl->trk[i];
            if (!r->active || r->gain == 0.0f) continue;
            if (r->is_click) render_click(e, sl, r, out, ph, nframes);
            else             render_audio(r, out, ph, nframes);
        }

        const bt_frame next = ph + nframes;
        bt_store(&e->playhead, next);

        /* End of song: stop on the sample, and let the UI thread decide what
         * on_end means. The engine never loads anything itself. */
        if (next >= sl->end_frame) {
            bt_store(&e->playing, 0);
            bt_store(&e->finished, 1);
        }
    }

    /* Released last: a thread that sees the count reach zero knows the whole
     * block above is finished with `sl`. */
    bt_dec_rel(&e->in_render);
}
