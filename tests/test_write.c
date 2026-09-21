/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Serialising the model back out.
 *
 * Two properties carry the weight here, and both are things you only notice
 * are broken much later: that a save loses nothing, and that saving an
 * unchanged set list changes nothing on disk.
 */
#include "bt_test.h"
#include "backtrack/bt_model.h"

static bool tempo_eq(const bt_tempo_map *a, const bt_tempo_map *b) {
    if (a->nseg != b->nseg) return false;
    if (a->sig_num != b->sig_num || a->sig_den != b->sig_den) return false;
    if (a->downbeat_ms != b->downbeat_ms) return false;   /* exact, on purpose */
    for (int32_t i = 0; i < a->nseg; i++) {
        if (a->seg[i].start_beat != b->seg[i].start_beat) return false;
        if (a->seg[i].bpm != b->seg[i].bpm) return false;
    }
    return true;
}

static bool track_eq(const bt_track *a, const bt_track *b) {
    return strcmp(a->name, b->name) == 0
        && strcmp(a->bus,  b->bus)  == 0
        && strcmp(a->file, b->file) == 0
        && a->type      == b->type
        && a->gain_db   == b->gain_db
        && a->offset_ms == b->offset_ms
        && a->muted     == b->muted;
}

static bool song_eq(const bt_song *a, const bt_song *b) {
    if (strcmp(a->title, b->title) != 0)   return false;
    if (strcmp(a->artist, b->artist) != 0) return false;
    if (a->count_in_bars != b->count_in_bars) return false;
    if (a->on_end != b->on_end)   return false;
    if (a->ntracks != b->ntracks) return false;
    if (!tempo_eq(&a->tempo, &b->tempo)) return false;
    for (int32_t i = 0; i < a->ntracks; i++)
        if (!track_eq(&a->track[i], &b->track[i])) return false;
    return true;
}

static bool setlist_eq(const bt_setlist *a, const bt_setlist *b) {
    if (strcmp(a->name, b->name) != 0) return false;
    if (a->nsongs != b->nsongs) return false;
    for (int32_t i = 0; i < a->nsongs; i++)
        if (!song_eq(&a->song[i], &b->song[i])) return false;
    return true;
}

static bt_setlist *load_mem(const char *json) {
    bt_setlist *sl = NULL;
    int line = 0;
    bt_err e = bt_setlist_load_mem(json, strlen(json), "", &sl, &line);
    BT_CHECK_EQI(e, BT_OK);
    return sl;
}

/* load -> save -> load, asserting the model survives and the text is stable. */
static void round_trip(const char *json, const char *what) {
    bt_setlist *a = load_mem(json);
    if (!a) { fprintf(stderr, "  (could not load %s)\n", what); return; }

    char  *s1 = NULL; size_t l1 = 0;
    BT_CHECK_EQI(bt_setlist_to_json(a, &s1, &l1), BT_OK);
    if (!s1) { bt_setlist_free(a); return; }

    bt_setlist *b = NULL;
    int line = 0;
    bt_err e = bt_setlist_load_mem(s1, l1, "", &b, &line);
    bt_checks++;
    if (e != BT_OK) {
        bt_fails++;
        fprintf(stderr, "  FAIL %s: output did not parse (%s, line %d)\n",
                what, bt_strerror(e), line);
        fprintf(stderr, "  ---\n%s  ---\n", s1);
        free(s1); bt_setlist_free(a);
        return;
    }

    bt_checks++;
    if (!setlist_eq(a, b)) {
        bt_fails++;
        fprintf(stderr, "  FAIL %s: model changed across save/load\n", what);
    }

    /* Saving an unchanged set list must not change the file. */
    char *s2 = NULL; size_t l2 = 0;
    BT_CHECK_EQI(bt_setlist_to_json(b, &s2, &l2), BT_OK);
    bt_checks++;
    if (!s2 || l1 != l2 || memcmp(s1, s2, l1) != 0) {
        bt_fails++;
        fprintf(stderr, "  FAIL %s: re-saving produced different text\n", what);
    }

    free(s1); free(s2);
    bt_setlist_free(a);
    bt_setlist_free(b);
}

/* ------------------------------------------------------------------ tests */

static void test_basic_round_trip(void) {
    round_trip(
      "{\"version\":1,\"name\":\"Set List #1\",\"songs\":[{"
      "\"title\":\"1985\",\"artist\":\"Bowling for Soup\","
      "\"tempo\":{\"bpm\":156,\"sig\":[4,4],\"downbeat_ms\":0},"
      "\"count_in_bars\":2,\"on_end\":\"next\",\"tracks\":["
      "{\"name\":\"Click\",\"type\":\"click\",\"bus\":\"inear\"},"
      "{\"name\":\"Synth\",\"type\":\"audio\",\"bus\":\"foh\","
      "\"file\":\"1985/synth.wav\",\"gain_db\":-2,\"offset_ms\":-15}"
      "]}]}", "basic");
}

static void test_awkward_numbers_survive(void) {
    /* The values that break a lazy serialiser: a BPM with a long decimal
     * expansion, a fractional downbeat, a fractional gain. If any of these
     * comes back as 156.36999999999999 the file is still valid and the click
     * is still right, but the diff churns every time anyone saves. */
    round_trip(
      "{\"version\":1,\"name\":\"n\",\"songs\":[{"
      "\"tempo\":{\"bpm\":156.37,\"sig\":[7,8],\"downbeat_ms\":12.5},"
      "\"count_in_bars\":0,\"on_end\":\"stop\",\"tracks\":["
      "{\"name\":\"T\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"a.wav\","
      "\"gain_db\":-2.5,\"offset_ms\":-1234,\"muted\":true}"
      "]}]}", "awkward numbers");

    round_trip(
      "{\"version\":1,\"name\":\"n\",\"songs\":[{"
      "\"tempo\":{\"bpm\":93.333333333333329,\"sig\":[4,4]},"
      "\"tracks\":[{\"type\":\"click\",\"bus\":\"inear\"}]}]}",
      "long decimal bpm");
}

static void test_tempo_map_survives(void) {
    round_trip(
      "{\"version\":1,\"name\":\"n\",\"songs\":[{"
      "\"tempo\":{\"map\":[{\"beat\":0,\"bpm\":96},{\"beat\":8,\"bpm\":132},"
      "{\"beat\":64,\"bpm\":100.5}],\"sig\":[3,4],\"downbeat_ms\":250},"
      "\"tracks\":[{\"type\":\"click\",\"bus\":\"inear\"}]}]}", "tempo map");
}

static void test_strings_are_escaped(void) {
    /* Song titles come from the internet and from people. */
    round_trip(
      "{\"version\":1,\"name\":\"He said \\\"hi\\\"\",\"songs\":[{"
      "\"title\":\"Tab\\there\",\"artist\":\"back\\\\slash\","
      "\"tempo\":{\"bpm\":120},"
      "\"tracks\":[{\"name\":\"caf\\u00e9\",\"type\":\"click\",\"bus\":\"inear\"}]}]}",
      "escapes");
}

static void test_empty_set_round_trips(void) {
    round_trip("{\"version\":1,\"name\":\"empty\",\"songs\":[]}", "empty set");
}

static void test_example_setlist_round_trips(void) {
    /* The real file in the repository, not a synthetic one. */
    bt_setlist *a = NULL;
    int line = 0;
    bt_err e = bt_setlist_load_file(BT_EXAMPLE_SETLIST, &a, &line);
    BT_CHECK_EQI(e, BT_OK);
    if (e != BT_OK || !a) return;

    const char *tmp = "write_example_out.json";
    BT_CHECK_EQI(bt_setlist_save_file(a, tmp), BT_OK);

    bt_setlist *b = NULL;
    BT_CHECK_EQI(bt_setlist_load_file(tmp, &b, &line), BT_OK);
    bt_checks++;
    if (!b || !setlist_eq(a, b)) {
        bt_fails++;
        fprintf(stderr, "  FAIL example set list changed across save/load\n");
    }
    remove(tmp);
    bt_setlist_free(a);
    bt_setlist_free(b);
}

static void test_save_replaces_existing(void) {
    const char *path = "write_replace.json";

    /* Something already there, longer than what replaces it - a partial
     * overwrite would leave trailing garbage and still "succeed". */
    FILE *f = fopen(path, "wb");
    BT_CHECK(f != NULL);
    if (f) {
        for (int i = 0; i < 500; i++) fputs("XXXXXXXXXXXXXXXXXXXX\n", f);
        fclose(f);
    }

    bt_setlist *a = load_mem(
      "{\"version\":1,\"name\":\"small\",\"songs\":[{"
      "\"tempo\":{\"bpm\":120},"
      "\"tracks\":[{\"type\":\"click\",\"bus\":\"inear\"}]}]}");
    if (!a) return;

    BT_CHECK_EQI(bt_setlist_save_file(a, path), BT_OK);

    bt_setlist *b = NULL;
    int line = 0;
    BT_CHECK_EQI(bt_setlist_load_file(path, &b, &line), BT_OK);
    BT_CHECK(b != NULL && setlist_eq(a, b));

    /* The temporary must not be left behind.
     *
     * Note what this does NOT prove: that the write is genuinely atomic. That
     * would need the process killed mid-write, which no unit test here can
     * arrange. What is verified is that the replace works over a larger
     * existing file and that no .tmp survives - the observable half. */
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *leftover = fopen(tmp, "rb");
    BT_CHECK(leftover == NULL);
    if (leftover) { fclose(leftover); remove(tmp); }

    remove(path);
    bt_setlist_free(a);
    bt_setlist_free(b);
}

static void test_device_cfg_round_trip(void) {
    bt_device_cfg a;
    bt_device_cfg_defaults(&a);
    snprintf(a.device, sizeof(a.device), "UMC404HD");
    snprintf(a.api,    sizeof(a.api),    "ASIO");
    a.sample_rate   = 48000;
    a.buffer_frames = 512;
    snprintf(a.bus[0].name, BT_MAX_NAME, "foh");
    a.bus[0].ch[0] = 0; a.bus[0].ch[1] = 1; a.bus[0].nch = 2;
    snprintf(a.bus[1].name, BT_MAX_NAME, "inear");
    a.bus[1].ch[0] = 2; a.bus[1].ch[1] = 3; a.bus[1].nch = 2;
    a.nbuses = 2;

    char  *js = NULL; size_t len = 0;
    BT_CHECK_EQI(bt_device_cfg_to_json(&a, &js, &len), BT_OK);
    if (!js) return;

    bt_device_cfg b;
    int line = 0;
    BT_CHECK_EQI(bt_device_cfg_load_mem(js, len, &b, &line), BT_OK);

    BT_CHECK(strcmp(a.device, b.device) == 0);
    BT_CHECK(strcmp(a.api, b.api) == 0);
    BT_CHECK_EQI(a.sample_rate, b.sample_rate);
    BT_CHECK_EQI(a.buffer_frames, b.buffer_frames);
    BT_CHECK_EQI(a.nbuses, b.nbuses);
    for (int32_t i = 0; i < a.nbuses; i++) {
        BT_CHECK(strcmp(a.bus[i].name, b.bus[i].name) == 0);
        BT_CHECK_EQI(a.bus[i].nch, b.bus[i].nch);
        for (int32_t k = 0; k < a.bus[i].nch; k++)
            BT_CHECK_EQI(a.bus[i].ch[k], b.bus[i].ch[k]);
    }

    /* Stable, like the set list. */
    char *js2 = NULL; size_t l2 = 0;
    BT_CHECK_EQI(bt_device_cfg_to_json(&b, &js2, &l2), BT_OK);
    BT_CHECK(js2 && l2 == len && memcmp(js, js2, len) == 0);

    free(js); free(js2);
}

static void test_bad_arguments(void) {
    char  *out = NULL;
    size_t len = 0;
    BT_CHECK(bt_setlist_to_json(NULL, &out, &len) != BT_OK);
    BT_CHECK(bt_setlist_save_file(NULL, "x.json") != BT_OK);
    BT_CHECK(bt_device_cfg_to_json(NULL, &out, &len) != BT_OK);

    bt_device_cfg cfg;
    bt_device_cfg_defaults(&cfg);
    BT_CHECK(bt_device_cfg_save_file(&cfg, NULL) != BT_OK);
    /* A directory that does not exist is an IO failure, not a crash. */
    BT_CHECK(bt_device_cfg_save_file(&cfg, "no_such_dir_here/x.json") != BT_OK);
}

int main(void) {
    BT_RUN(test_basic_round_trip);
    BT_RUN(test_awkward_numbers_survive);
    BT_RUN(test_tempo_map_survives);
    BT_RUN(test_strings_are_escaped);
    BT_RUN(test_empty_set_round_trips);
    BT_RUN(test_example_setlist_round_trips);
    BT_RUN(test_save_replaces_existing);
    BT_RUN(test_device_cfg_round_trip);
    BT_RUN(test_bad_arguments);
    BT_REPORT();
}
