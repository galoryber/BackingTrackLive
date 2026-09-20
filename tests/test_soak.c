/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Long-run behaviour: a full-length set, and timing at the far end of one.
 *
 * The README has claimed from the first commit that residency stays bounded
 * across a 40-song set and that beat positions do not drift over three hours.
 * Both were true by construction and neither was actually tested. They are
 * now.
 */
#include "bt_test.h"
#include "backtrack/bt_player.h"
#include "backtrack/bt_wav.h"

#define SR         48000
#define SOAK_SONGS 40
#define SONG_SEC   0.05           /* tiny: this tests bookkeeping, not audio */

static void stem_name(char *dst, size_t cap, int32_t song) {
    snprintf(dst, cap, "soak_stem_%d.wav", song);
}

static const char *SETLIST = "soak_setlist.json";

static void build_set(void) {
    /* One stem per song, each a distinct length so a mix-up would show. */
    char *json = (char *)malloc(1 << 16);
    size_t o = 0;
    o += (size_t)snprintf(json + o, (1 << 16) - o,
                          "{\"version\":1,\"name\":\"Soak\",\"songs\":[");

    for (int32_t i = 0; i < SOAK_SONGS; i++) {
        bt_frame n = (bt_frame)(SR * SONG_SEC) + i * 10;
        float *buf = (float *)malloc((size_t)n * sizeof(float));
        bt_lcg_seed((uint32_t)i + 1u);
        for (bt_frame k = 0; k < n; k++) buf[k] = bt_lcg_sample();

        char path[64];
        stem_name(path, sizeof(path), i);
        const float *p[1] = { buf };
        BT_CHECK_EQI(bt_wav_write_file(path, p, 1, SR, n), BT_OK);
        free(buf);

        /* Every song runs into the next, so the whole set plays unattended -
         * the worst case for both advance and the preload window. */
        o += (size_t)snprintf(json + o, (1 << 16) - o,
            "%s{\"title\":\"S%d\",\"tempo\":{\"bpm\":%d},\"count_in_bars\":0,"
            "\"on_end\":\"next\",\"tracks\":["
            "{\"type\":\"click\",\"bus\":\"inear\"},"
            "{\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"%s\"}]}",
            i ? "," : "", i, 90 + i, path);
    }
    o += (size_t)snprintf(json + o, (1 << 16) - o, "]}");

    FILE *f = fopen(SETLIST, "wb");
    BT_CHECK(f != NULL);
    if (f) { fwrite(json, 1, o, f); fclose(f); }
    free(json);
}

static void cleanup_set(void) {
    remove(SETLIST);
    for (int32_t i = 0; i < SOAK_SONGS; i++) {
        char path[64];
        stem_name(path, sizeof(path), i);
        remove(path);
    }
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

/* ---------------------------------------------------------------------- */

static void test_whole_set_residency_is_bounded(void) {
    build_set();

    bt_setlist *sl = NULL;
    int line = 0;
    BT_CHECK_EQI(bt_setlist_load_file(SETLIST, &sl, &line), BT_OK);
    if (!sl) { cleanup_set(); return; }

    bt_device_cfg d = mk_dev();
    bt_player_cfg c = { SR, 4, 512, 1 };         /* current + next resident */
    bt_player *p = NULL;
    BT_CHECK_EQI(bt_player_create(&c, sl, &d, &p), BT_OK);

    float *buf[4], *win[4];
    for (int i = 0; i < 4; i++) { buf[i] = (float *)calloc(512, sizeof(float));
                                  win[i] = buf[i]; }

    BT_CHECK_EQI(bt_player_select(p, 0), BT_OK);
    bt_player_play(p);

    /* The largest a two-song window can legitimately be: the two longest
     * songs in the set, which are the last two. */
    bt_frame longest  = (bt_frame)(SR * SONG_SEC) + (SOAK_SONGS - 1) * 10;
    bt_frame second   = (bt_frame)(SR * SONG_SEC) + (SOAK_SONGS - 2) * 10;
    size_t   ceiling  = (size_t)(longest + second) * sizeof(float);

    size_t   peak     = 0;
    int32_t  visited  = 1;
    int32_t  last_seen = 0;
    bool     done     = false;

    for (int guard = 0; guard < 200000 && !done; guard++) {
        bt_player_render(p, win, 512);

        bt_tick_result t = BT_TICK_IDLE;
        BT_CHECK_EQI(bt_player_tick(p, &t), BT_OK);

        size_t res = bt_player_resident_bytes(p);
        if (res > peak) peak = res;

        if (t == BT_TICK_ADVANCED) {
            visited++;
            BT_CHECK_EQI(bt_player_current(p), last_seen + 1);
            last_seen = bt_player_current(p);
        } else if (t == BT_TICK_SONG_ENDED) {
            done = true;
        }
    }

    /* Played the set from end to end, unattended. */
    BT_CHECK(done);
    BT_CHECK_EQI(visited, SOAK_SONGS);
    BT_CHECK_EQI(bt_player_current(p), SOAK_SONGS - 1);

    /* And never held more than the preload window, however far it travelled.
     * This is the assertion that fails the day the window stops freeing. */
    bt_checks++;
    if (peak > ceiling) {
        bt_fails++;
        fprintf(stderr, "  FAIL residency peaked at %zu bytes, ceiling %zu "
                        "(%d songs)\n", peak, ceiling, SOAK_SONGS);
    }

    for (int i = 0; i < 4; i++) free(buf[i]);
    bt_player_destroy(p);
    bt_setlist_free(sl);
    cleanup_set();
}

static void test_no_drift_three_hours_in(void) {
    /* The tempo maths is exercised exhaustively in test_tempo. What this adds
     * is the engine actually placing a click at the right sample that far out,
     * without rendering three hours of audio to get there. */
    const double  BPM  = 137.37;
    const int64_t BEAT = 889000;               /* ~3h 14m at 137.37 BPM */

    bt_song s;
    memset(&s, 0, sizeof(s));
    s.tempo.seg[0].bpm = BPM;
    s.tempo.nseg    = 1;
    s.tempo.sig_num = 4;
    s.tempo.sig_den = 4;
    s.track[0].type = BT_TRACK_CLICK;
    snprintf(s.track[0].bus, BT_MAX_NAME, "inear");
    s.ntracks = 1;

    bt_device_cfg d = mk_dev();
    bt_engine_cfg ec = { SR, 4, 1024 };
    bt_engine *e = NULL;
    BT_CHECK_EQI(bt_engine_create(&ec, &e), BT_OK);
    BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_OK);

    bt_frame want = bt_tempo_beat_frame(&s.tempo, BEAT, SR);
    /* Sanity: this really is about three hours in. */
    BT_CHECK((double)want / SR > 3.0 * 3600.0);

    float *buf[4], *win[4];
    for (int i = 0; i < 4; i++) { buf[i] = (float *)calloc(1024, sizeof(float));
                                  win[i] = buf[i]; }

    bt_engine_seek(e, want - 100);
    bt_engine_play(e);
    bt_engine_render(e, win, 1024);

    /* Silence before the beat, energy immediately after it. The click's first
     * sample is legitimately zero - it is a sine starting at phase zero. */
    for (int i = 0; i < 100; i++) BT_CHECK(fabsf(buf[2][i]) < 1e-6f);

    int onset = -1;
    for (int i = 100; i < 1024; i++)
        if (fabsf(buf[2][i]) > 0.05f) { onset = i; break; }
    bt_checks++;
    if (onset < 100 || onset > 102) {
        bt_fails++;
        fprintf(stderr, "  FAIL click at beat %lld landed %d samples off\n",
                (long long)BEAT, onset - 100);
    }

    for (int i = 0; i < 4; i++) free(buf[i]);
    bt_engine_destroy(e);
}

int main(void) {
    BT_RUN(test_whole_set_residency_is_bounded);
    BT_RUN(test_no_drift_three_hours_in);
    BT_REPORT();
}
