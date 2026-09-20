/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The audio thread and the UI thread, doing what they actually do.
 *
 * Until the device layer existed every test here was single-threaded, because
 * btrender renders on the calling thread. On a real device the callback runs
 * on a thread owned by the driver while the UI thread selects songs - and
 * bt_engine_set_song rewrites the very array bt_engine_render walks.
 *
 * Run this under ThreadSanitizer (`make tsan`). Without it the race is
 * invisible almost every time, which is exactly what makes it dangerous.
 */
#include "bt_test.h"
#include "backtrack/bt_engine.h"

#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>

#define SR 48000

static bt_engine *g_eng;
/* Atomic, not volatile: volatile says nothing about inter-thread ordering,
 * and TSan is right to complain about it. */
static _Atomic int g_stop;

/* Stands in for the driver's callback thread: renders continuously, never
 * synchronising with anybody, exactly as a real audio callback does. */
static void *render_thread(void *arg) {
    (void)arg;
    float *buf[4], *win[4];
    for (int c = 0; c < 4; c++) { buf[c] = (float *)calloc(256, sizeof(float));
                                  win[c] = buf[c]; }
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed))
        bt_engine_render(g_eng, win, 256);
    for (int c = 0; c < 4; c++) free(buf[c]);
    return NULL;
}

static bt_device_cfg mk_dev(void) {
    bt_device_cfg d;
    memset(&d, 0, sizeof(d));
    d.sample_rate = SR;
    snprintf(d.bus[0].name, BT_MAX_NAME, "foh");
    d.bus[0].ch[0] = 0; d.bus[0].ch[1] = 1; d.bus[0].nch = 2;
    snprintf(d.bus[1].name, BT_MAX_NAME, "inear");
    d.bus[1].ch[0] = 2; d.bus[1].ch[1] = 3; d.bus[1].nch = 2;
    d.nbuses = 2;
    return d;
}

static void mk_song(bt_song *s, int seed, bt_frame frames) {
    memset(s, 0, sizeof(*s));
    s->tempo.seg[0].bpm = 100.0 + seed;
    s->tempo.nseg = 1;
    s->tempo.sig_num = 4;
    s->tempo.sig_den = 4;

    s->track[0].type = BT_TRACK_CLICK;
    snprintf(s->track[0].bus, BT_MAX_NAME, "inear");

    s->track[1].type = BT_TRACK_AUDIO;
    snprintf(s->track[1].bus, BT_MAX_NAME, "foh");
    snprintf(s->track[1].file, BT_MAX_PATH, "x.wav");
    s->track[1].channels = 1;
    s->track[1].frames   = frames;
    s->track[1].pcm = (float **)calloc(1, sizeof(float *));
    s->track[1].pcm[0] = (float *)calloc((size_t)frames, sizeof(float));
    for (bt_frame i = 0; i < frames; i++) s->track[1].pcm[0][i] = 0.1f;

    s->ntracks = 2;
}

static void test_swap_song_while_rendering(void) {
    bt_device_cfg d = mk_dev();
    bt_engine_cfg ec = { SR, 4, 256 };
    BT_CHECK_EQI(bt_engine_create(&ec, &g_eng), BT_OK);

    /* Two songs of deliberately different lengths: binding one while the
     * other is being walked is how a stale `frames` meets a fresh `pcm`. */
    bt_song a, b;
    mk_song(&a, 1, SR * 2);
    mk_song(&b, 2, SR / 4);

    BT_CHECK_EQI(bt_engine_set_song(g_eng, &a, &d), BT_OK);
    bt_engine_play(g_eng);

    atomic_store_explicit(&g_stop, 0, memory_order_relaxed);
    pthread_t th;
    BT_CHECK_EQI(pthread_create(&th, NULL, render_thread, NULL), 0);

    /* What bt_player_select does on every song change. */
    for (int i = 0; i < 400; i++) {
        const bt_song *next = (i & 1) ? &b : &a;
        bt_engine_stop(g_eng);
        BT_CHECK_EQI(bt_engine_set_song(g_eng, next, &d), BT_OK);
        bt_engine_play(g_eng);
        usleep(200);
    }

    atomic_store_explicit(&g_stop, 1, memory_order_relaxed);
    pthread_join(th, NULL);

    bt_engine_destroy(g_eng);
    g_eng = NULL;
    bt_song_free_audio(&a);
    bt_song_free_audio(&b);
}

int main(void) {
    BT_RUN(test_swap_song_while_rendering);
    BT_REPORT();
}
