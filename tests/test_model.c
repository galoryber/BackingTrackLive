/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "bt_test.h"
#include "backtrack/bt_model.h"

static const char *GOOD =
"{\n"
"  \"version\": 1,\n"
"  \"name\": \"Set List #1\",\n"
"  \"songs\": [\n"
"    {\n"
"      \"title\": \"1985\", \"artist\": \"Bowling for Soup\",\n"
"      \"tempo\": { \"bpm\": 156.0, \"sig\": [4,4], \"downbeat_ms\": 12.5 },\n"
"      \"count_in_bars\": 2, \"on_end\": \"next\",\n"
"      \"tracks\": [\n"
"        { \"name\": \"Click\", \"type\": \"click\", \"bus\": \"inear\" },\n"
"        { \"name\": \"Synth\", \"type\": \"audio\", \"bus\": \"foh\",\n"
"          \"file\": \"1985/synth.wav\", \"gain_db\": -2.0, \"offset_ms\": -15 }\n"
"      ]\n"
"    }\n"
"  ]\n"
"}\n";

static bt_err load(const char *text, bt_setlist **out) {
    int line = 0;
    return bt_setlist_load_mem(text, strlen(text), "", out, &line);
}

static void reject(const char *text, const char *why) {
    bt_setlist *sl = NULL;
    bt_err e = load(text, &sl);
    bt_checks++;
    if (e == BT_OK) {
        bt_fails++;
        fprintf(stderr, "  FAIL accepted bad set list (%s)\n", why);
        bt_setlist_free(sl);
    }
    BT_CHECK(sl == NULL);
}

static void test_good(void) {
    bt_setlist *sl = NULL;
    BT_CHECK_EQI(load(GOOD, &sl), BT_OK);
    if (!sl) return;

    BT_CHECK(strcmp(sl->name, "Set List #1") == 0);
    BT_CHECK_EQI(sl->nsongs, 1);

    const bt_song *s = &sl->song[0];
    BT_CHECK(strcmp(s->title, "1985") == 0);
    BT_CHECK(strcmp(s->artist, "Bowling for Soup") == 0);
    BT_CHECK_EQI(s->count_in_bars, 2);
    BT_CHECK_EQI(s->on_end, BT_ON_END_NEXT);
    BT_CHECK_EQI(s->tempo.nseg, 1);
    BT_CHECK_NEAR(s->tempo.seg[0].bpm, 156.0, 1e-9);
    BT_CHECK_NEAR(s->tempo.downbeat_ms, 12.5, 1e-9);
    BT_CHECK_EQI(s->tempo.sig_num, 4);

    BT_CHECK_EQI(s->ntracks, 2);
    BT_CHECK_EQI(s->track[0].type, BT_TRACK_CLICK);
    BT_CHECK(strcmp(s->track[0].bus, "inear") == 0);
    BT_CHECK_EQI(s->track[1].type, BT_TRACK_AUDIO);
    BT_CHECK(strcmp(s->track[1].file, "1985/synth.wav") == 0);
    BT_CHECK_NEAR(s->track[1].gain_db, -2.0, 1e-9);
    BT_CHECK_EQI(s->track[1].offset_ms, -15);

    bt_setlist_free(sl);
}

static void test_defaults(void) {
    const char *min =
    "{\"version\":1,\"songs\":[{\"tempo\":{\"bpm\":100},"
    "\"tracks\":[{\"type\":\"click\",\"bus\":\"inear\"}]}]}";
    bt_setlist *sl = NULL;
    BT_CHECK_EQI(load(min, &sl), BT_OK);
    if (!sl) return;
    BT_CHECK(strcmp(sl->song[0].title, "Untitled") == 0);
    BT_CHECK_EQI(sl->song[0].on_end, BT_ON_END_STOP);   /* stop is the default */
    BT_CHECK_EQI(sl->song[0].count_in_bars, 1);
    BT_CHECK_EQI(sl->song[0].tempo.sig_num, 4);
    bt_setlist_free(sl);
}

static void test_tempo_map_binding(void) {
    const char *tm =
    "{\"version\":1,\"songs\":[{\"tempo\":{\"map\":["
    "{\"beat\":0,\"bpm\":120},{\"beat\":16,\"bpm\":90}],\"sig\":[3,4]},"
    "\"tracks\":[{\"type\":\"click\",\"bus\":\"inear\"}]}]}";
    bt_setlist *sl = NULL;
    BT_CHECK_EQI(load(tm, &sl), BT_OK);
    if (!sl) return;
    BT_CHECK_EQI(sl->song[0].tempo.nseg, 2);
    BT_CHECK_NEAR(sl->song[0].tempo.seg[1].bpm, 90.0, 1e-9);
    BT_CHECK_EQI(sl->song[0].tempo.seg[1].start_beat, 16);
    BT_CHECK_EQI(sl->song[0].tempo.sig_num, 3);
    bt_setlist_free(sl);
}

static void test_rejects(void) {
    reject("{\"version\":2,\"songs\":[]}", "wrong version");
    reject("{\"version\":1}", "no songs array");
    reject("{\"version\":1,\"songs\":{}}", "songs not an array");
    reject("{\"version\":1,\"songs\":[{\"tempo\":{\"bpm\":0},"
           "\"tracks\":[{\"type\":\"click\",\"bus\":\"a\"}]}]}", "bpm zero");
    reject("{\"version\":1,\"songs\":[{\"tempo\":{\"bpm\":900},"
           "\"tracks\":[{\"type\":\"click\",\"bus\":\"a\"}]}]}", "bpm absurd");
    reject("{\"version\":1,\"songs\":[{\"tempo\":{\"bpm\":100},\"tracks\":[]}]}",
           "no tracks");
    reject("{\"version\":1,\"songs\":[{\"tempo\":{\"bpm\":100},"
           "\"tracks\":[{\"type\":\"synth\",\"bus\":\"a\"}]}]}", "unknown type");
    reject("{\"version\":1,\"songs\":[{\"tempo\":{\"bpm\":100},"
           "\"tracks\":[{\"type\":\"audio\",\"bus\":\"a\"}]}]}", "audio with no file");
    reject("{\"version\":1,\"songs\":[{\"tempo\":{\"bpm\":100},\"on_end\":\"loop\","
           "\"tracks\":[{\"type\":\"click\",\"bus\":\"a\"}]}]}", "unknown on_end");
    reject("{\"version\":1,\"songs\":[{\"tempo\":{\"map\":["
           "{\"beat\":4,\"bpm\":120}]},"
           "\"tracks\":[{\"type\":\"click\",\"bus\":\"a\"}]}]}",
           "tempo map not starting at beat 0");
}

static void test_path_containment(void) {
    /* A set list folder is meant to be a self-contained unit you copy to the
     * backup laptop - and one day, a file someone else hands you. */
    reject("{\"version\":1,\"songs\":[{\"tempo\":{\"bpm\":100},"
           "\"tracks\":[{\"type\":\"audio\",\"bus\":\"a\",\"file\":\"/etc/passwd\"}]}]}",
           "absolute unix path");
    reject("{\"version\":1,\"songs\":[{\"tempo\":{\"bpm\":100},"
           "\"tracks\":[{\"type\":\"audio\",\"bus\":\"a\",\"file\":\"C:\\\\x.wav\"}]}]}",
           "absolute windows path");
    reject("{\"version\":1,\"songs\":[{\"tempo\":{\"bpm\":100},"
           "\"tracks\":[{\"type\":\"audio\",\"bus\":\"a\","
           "\"file\":\"../../secrets.wav\"}]}]}", "parent traversal");
}

static void test_device_cfg(void) {
    bt_device_cfg cfg;
    bt_device_cfg_defaults(&cfg);
    /* The stereo fallback most bar bands already own. */
    BT_CHECK_EQI(cfg.nbuses, 2);
    BT_CHECK(bt_device_find_bus(&cfg, "foh")   != NULL);
    BT_CHECK(bt_device_find_bus(&cfg, "inear") != NULL);
    BT_CHECK(bt_device_find_bus(&cfg, "nope")  == NULL);

    const char *dev =
    "{\"device\":\"ASIO Focusrite\",\"sample_rate\":48000,\"buffer_frames\":512,"
    "\"buses\":[{\"name\":\"foh\",\"channels\":[0,1]},"
    "{\"name\":\"inear\",\"channels\":[2,3]}]}";
    int line = 0;
    BT_CHECK_EQI(bt_device_cfg_load_mem(dev, strlen(dev), &cfg, &line), BT_OK);
    BT_CHECK_EQI(cfg.nbuses, 2);
    BT_CHECK_EQI(cfg.buffer_frames, 512);
    const bt_bus *b = bt_device_find_bus(&cfg, "inear");
    BT_CHECK(b != NULL);
    if (b) { BT_CHECK_EQI(b->nch, 2); BT_CHECK_EQI(b->ch[0], 2); BT_CHECK_EQI(b->ch[1], 3); }

    const char *bad = "{\"sample_rate\":1}";
    BT_CHECK(bt_device_cfg_load_mem(bad, strlen(bad), &cfg, &line) != BT_OK);
    const char *badch =
    "{\"buses\":[{\"name\":\"x\",\"channels\":[999]}]}";
    BT_CHECK(bt_device_cfg_load_mem(badch, strlen(badch), &cfg, &line) != BT_OK);
}

int main(void) {
    BT_RUN(test_good);
    BT_RUN(test_defaults);
    BT_RUN(test_tempo_map_binding);
    BT_RUN(test_rejects);
    BT_RUN(test_path_containment);
    BT_RUN(test_device_cfg);
    BT_REPORT();
}
