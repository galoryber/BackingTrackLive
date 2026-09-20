/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The set list player: song selection, end-of-song behaviour and the preload
 * window. All of it offline - the device never enters into it.
 */
#include "bt_test.h"
#include "backtrack/bt_player.h"
#include "backtrack/bt_wav.h"

#define SR       48000
#define SONGS    6
#define SONG_SEC 0.2

static char g_setlist_path[] = "player_setlist.json";

/* Fixtures live flat in the working directory rather than a subdirectory, so
 * the test needs no portable mkdir to run identically on Windows CI. */
static void stem_name(char *dst, size_t cap, int32_t song) {
    snprintf(dst, cap, "player_stem_%d.wav", song);
}

static void write_stem(int32_t song) {
    const bt_frame n = (bt_frame)(SR * SONG_SEC);
    float *buf = (float *)malloc((size_t)n * sizeof(float));
    bt_lcg_seed((uint32_t)song + 1u);
    for (bt_frame i = 0; i < n; i++) buf[i] = bt_lcg_sample();

    char path[64];
    stem_name(path, sizeof(path), song);
    const float *p[1] = { buf };
    BT_CHECK_EQI(bt_wav_write_file(path, p, 1, SR, n), BT_OK);
    free(buf);
}

/* Builds a set list of SONGS songs; `next_mask` bit i sets song i to
 * on_end: next. */
static void build_fixture(uint32_t next_mask) {
    char json[8192];
    size_t o = 0;
    o += (size_t)snprintf(json + o, sizeof(json) - o,
                          "{\"version\":1,\"name\":\"P\",\"songs\":[");
    for (int32_t i = 0; i < SONGS; i++) {
        char stem[64];
        stem_name(stem, sizeof(stem), i);
        write_stem(i);
        o += (size_t)snprintf(json + o, sizeof(json) - o,
            "%s{\"title\":\"S%d\",\"tempo\":{\"bpm\":120},"
            "\"count_in_bars\":1,\"on_end\":\"%s\",\"tracks\":["
            "{\"type\":\"click\",\"bus\":\"inear\"},"
            "{\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"%s\"}]}",
            i ? "," : "", i,
            (next_mask & (1u << i)) ? "next" : "stop", stem);
    }
    o += (size_t)snprintf(json + o, sizeof(json) - o, "]}");

    FILE *f = fopen(g_setlist_path, "wb");
    BT_CHECK(f != NULL);
    if (f) { fwrite(json, 1, o, f); fclose(f); }
}

static void cleanup_fixture(void) {
    remove(g_setlist_path);
    for (int32_t i = 0; i < SONGS; i++) {
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

typedef struct {
    bt_setlist *sl;
    bt_player  *p;
    float      *buf[4];
    float      *win[4];
} rig;

static void rig_up(rig *r, uint32_t next_mask, int32_t preload_ahead) {
    build_fixture(next_mask);

    int line = 0;
    r->sl = NULL;
    BT_CHECK_EQI(bt_setlist_load_file(g_setlist_path, &r->sl, &line), BT_OK);

    bt_device_cfg d = mk_dev();
    bt_player_cfg c = { SR, 4, 1024, preload_ahead };
    r->p = NULL;
    BT_CHECK_EQI(bt_player_create(&c, r->sl, &d, &r->p), BT_OK);

    for (int i = 0; i < 4; i++) {
        r->buf[i] = (float *)calloc(1024, sizeof(float));
        r->win[i] = r->buf[i];
    }
}

static void rig_down(rig *r) {
    for (int i = 0; i < 4; i++) free(r->buf[i]);
    bt_player_destroy(r->p);
    bt_setlist_free(r->sl);
    cleanup_fixture();
}

/* Renders up to `blocks` blocks, ticking between each, stopping once `tick`
 * reports something other than idle/preload. Returns that result. */
static bt_tick_result run_until_event(rig *r, int blocks) {
    for (int i = 0; i < blocks; i++) {
        bt_player_render(r->p, r->win, 1024);
        bt_tick_result t = BT_TICK_IDLE;
        BT_CHECK_EQI(bt_player_tick(r->p, &t), BT_OK);
        if (t == BT_TICK_SONG_ENDED || t == BT_TICK_ADVANCED) return t;
    }
    return BT_TICK_IDLE;
}

/* ------------------------------------------------------------------ tests */

static void test_select_and_navigate(void) {
    rig r;
    rig_up(&r, 0, 1);

    BT_CHECK_EQI(bt_player_current(r.p), -1);
    BT_CHECK_EQI(bt_player_count(r.p), SONGS);

    BT_CHECK_EQI(bt_player_select(r.p, 2), BT_OK);
    BT_CHECK_EQI(bt_player_current(r.p), 2);
    BT_CHECK(bt_player_song(r.p) != NULL);
    BT_CHECK(strcmp(bt_player_song(r.p)->title, "S2") == 0);
    BT_CHECK(!bt_player_playing(r.p));

    BT_CHECK_EQI(bt_player_next(r.p), BT_OK);
    BT_CHECK_EQI(bt_player_current(r.p), 3);
    BT_CHECK_EQI(bt_player_prev(r.p), BT_OK);
    BT_CHECK_EQI(bt_player_current(r.p), 2);

    /* Out of range is refused, not clamped silently. */
    BT_CHECK(bt_player_select(r.p, -1) != BT_OK);
    BT_CHECK(bt_player_select(r.p, SONGS) != BT_OK);
    BT_CHECK_EQI(bt_player_current(r.p), 2);

    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    BT_CHECK(bt_player_prev(r.p) != BT_OK);
    BT_CHECK_EQI(bt_player_select(r.p, SONGS - 1), BT_OK);
    BT_CHECK(bt_player_next(r.p) != BT_OK);

    rig_down(&r);
}

static void test_preload_window(void) {
    rig r;
    rig_up(&r, 0, 1);

    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    bt_tick_result t = BT_TICK_IDLE;
    BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    BT_CHECK_EQI(t, BT_TICK_PRELOADED);          /* song 1 came in */

    BT_CHECK(bt_player_song_resident(r.p, 0));
    BT_CHECK(bt_player_song_resident(r.p, 1));
    BT_CHECK(!bt_player_song_resident(r.p, 2));
    BT_CHECK(!bt_player_song_resident(r.p, 5));

    /* A second tick has nothing left to do. */
    BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    BT_CHECK_EQI(t, BT_TICK_IDLE);

    size_t two_songs = bt_player_resident_bytes(r.p);
    BT_CHECK(two_songs > 0);

    /* Jumping across the set frees what is behind and loads what is ahead. */
    BT_CHECK_EQI(bt_player_select(r.p, 4), BT_OK);
    BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    BT_CHECK(!bt_player_song_resident(r.p, 0));
    BT_CHECK(!bt_player_song_resident(r.p, 1));
    BT_CHECK(bt_player_song_resident(r.p, 4));
    BT_CHECK(bt_player_song_resident(r.p, 5));
    /* Still two songs' worth - the window does not grow as you move. */
    BT_CHECK_EQI(bt_player_resident_bytes(r.p), (long long)two_songs);

    rig_down(&r);
}

static void test_preload_ahead_zero(void) {
    rig r;
    rig_up(&r, 0, 0);

    BT_CHECK_EQI(bt_player_select(r.p, 2), BT_OK);
    bt_tick_result t = BT_TICK_IDLE;
    BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    BT_CHECK(bt_player_song_resident(r.p, 2));
    BT_CHECK(!bt_player_song_resident(r.p, 3));

    rig_down(&r);
}

static void test_on_end_stop_waits(void) {
    rig r;
    rig_up(&r, 0, 1);          /* every song stops */

    BT_CHECK_EQI(bt_player_select(r.p, 1), BT_OK);
    bt_player_play(r.p);

    bt_tick_result t = run_until_event(&r, 200);
    BT_CHECK_EQI(t, BT_TICK_SONG_ENDED);
    BT_CHECK_EQI(bt_player_current(r.p), 1);     /* did not move */
    BT_CHECK(!bt_player_playing(r.p));

    /* The end is reported once, not on every subsequent tick. */
    BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    BT_CHECK_EQI(t, BT_TICK_IDLE);

    rig_down(&r);
}

static void test_on_end_next_advances(void) {
    rig r;
    rig_up(&r, 0x2u, 1);       /* song 1 runs into song 2 */

    BT_CHECK_EQI(bt_player_select(r.p, 1), BT_OK);
    bt_player_play(r.p);

    bt_tick_result t = run_until_event(&r, 200);
    BT_CHECK_EQI(t, BT_TICK_ADVANCED);
    BT_CHECK_EQI(bt_player_current(r.p), 2);
    BT_CHECK(bt_player_playing(r.p));
    /* Started at the top with no count-in: a segue should not be counted in. */
    BT_CHECK(bt_player_playhead(r.p) >= 0);
    BT_CHECK(bt_player_playhead(r.p) < 2048);

    /* Song 2 is on_end: stop, so the chain ends there rather than running on. */
    t = run_until_event(&r, 200);
    BT_CHECK_EQI(t, BT_TICK_SONG_ENDED);
    BT_CHECK_EQI(bt_player_current(r.p), 2);

    rig_down(&r);
}

static void test_on_end_next_chains(void) {
    rig r;
    rig_up(&r, 0x7u, 1);       /* songs 0,1,2 all run on */

    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    bt_player_play(r.p);

    BT_CHECK_EQI(run_until_event(&r, 200), BT_TICK_ADVANCED);
    BT_CHECK_EQI(bt_player_current(r.p), 1);
    BT_CHECK_EQI(run_until_event(&r, 200), BT_TICK_ADVANCED);
    BT_CHECK_EQI(bt_player_current(r.p), 2);
    BT_CHECK_EQI(run_until_event(&r, 200), BT_TICK_ADVANCED);
    BT_CHECK_EQI(bt_player_current(r.p), 3);
    /* Song 3 stops. */
    BT_CHECK_EQI(run_until_event(&r, 200), BT_TICK_SONG_ENDED);

    rig_down(&r);
}

static void test_on_end_next_at_last_song_stops(void) {
    rig r;
    rig_up(&r, 0xFFFFFFFFu, 1);   /* even the last song says "next" */

    BT_CHECK_EQI(bt_player_select(r.p, SONGS - 1), BT_OK);
    bt_player_play(r.p);

    /* There is nothing to advance to, so it behaves as stop rather than
     * wrapping to the top of the set in front of an audience. */
    BT_CHECK_EQI(run_until_event(&r, 200), BT_TICK_SONG_ENDED);
    BT_CHECK_EQI(bt_player_current(r.p), SONGS - 1);
    BT_CHECK(!bt_player_playing(r.p));

    rig_down(&r);
}

static void test_count_in_then_stop_is_not_an_end(void) {
    rig r;
    rig_up(&r, 0, 1);

    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    bt_player_start(r.p);                     /* count-in: negative playhead */
    BT_CHECK(bt_player_playhead(r.p) < 0);

    bt_tick_result t = BT_TICK_IDLE;
    BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    BT_CHECK(t != BT_TICK_SONG_ENDED);
    BT_CHECK(bt_player_playing(r.p));

    rig_down(&r);
}

static void test_render_and_tick_before_select(void) {
    rig r;
    rig_up(&r, 0, 1);

    /* Nothing selected: silence, no crash, nothing resident. */
    bt_player_render(r.p, r.win, 1024);
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 1024; i++) BT_CHECK(r.buf[c][i] == 0.0f);

    bt_tick_result t = BT_TICK_ADVANCED;
    BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    BT_CHECK_EQI(t, BT_TICK_IDLE);
    BT_CHECK_EQI(bt_player_resident_bytes(r.p), 0);
    BT_CHECK(!bt_player_playing(r.p));

    rig_down(&r);
}

static void test_panic_from_player(void) {
    rig r;
    rig_up(&r, 0, 1);

    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    bt_player_play(r.p);
    bt_player_render(r.p, r.win, 1024);
    BT_CHECK(bt_player_playing(r.p));

    bt_player_panic(r.p);
    BT_CHECK(!bt_player_playing(r.p));
    BT_CHECK_EQI(bt_player_playhead(r.p), 0);

    bt_player_render(r.p, r.win, 1024);
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 1024; i++) BT_CHECK(r.buf[c][i] == 0.0f);

    rig_down(&r);
}

int main(void) {
    BT_RUN(test_select_and_navigate);
    BT_RUN(test_preload_window);
    BT_RUN(test_preload_ahead_zero);
    BT_RUN(test_on_end_stop_waits);
    BT_RUN(test_on_end_next_advances);
    BT_RUN(test_on_end_next_chains);
    BT_RUN(test_on_end_next_at_last_song_stops);
    BT_RUN(test_count_in_then_stop_is_not_an_end);
    BT_RUN(test_render_and_tick_before_select);
    BT_RUN(test_panic_from_player);
    BT_REPORT();
}
