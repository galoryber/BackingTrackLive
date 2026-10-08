/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The lighting side's cue file, and the three questions the sender asks of
 * it: what fires between these two playheads, what should the stage look
 * like at this position, and what does a file we do not understand do.
 */
#include "backtrack/bt_lightshow.h"
#include "bt_test.h"

#include <string.h>
#include <stdlib.h>

static const char *SHOW =
"{\n"
"  \"format\": \"random-riot-lighting/1\",\n"
"  \"midi_port\": \"BackTrackQLC\",\n"
"  \"velocity\": 127,\n"
"  \"specials\": {\n"
"    \"between_songs\": { \"ch\": 16, \"note\": 37 },\n"
"    \"stop_all\":      { \"ch\": 16, \"note\": 39 },\n"
"    \"blinder\":       { \"ch\": 16, \"note\": 72 },\n"
"    \"fog_dual\":      { \"ch\": 16, \"note\": 77 }\n"
"  },\n"
"  \"songs\": [\n"
"    { \"title\": \"Corduroy\", \"events\": [\n"
"      { \"bar\": 1,  \"ch\": 1,  \"note\": 0, \"type\": \"look\", \"label\": \"Intro Guitar Riff\" },\n"
"      { \"bar\": 17, \"ch\": 1,  \"note\": 1, \"type\": \"look\", \"label\": \"Verse 1\" },\n"
"      { \"bar\": 33, \"ch\": 16, \"note\": 72, \"type\": \"hit\", \"label\": \"Blinder\" },\n"
"      { \"bar\": 49, \"ch\": 1,  \"note\": 2, \"type\": \"look\", \"label\": \"Chorus\" },\n"
"      { \"bar\": 65, \"ch\": 16, \"note\": 37, \"type\": \"end\", \"label\": \"Between Songs\" }\n"
"    ] }\n"
"  ]\n"
"}\n";

static bt_song a_song(void) {
    bt_song s;
    memset(&s, 0, sizeof(s));
    snprintf(s.title, sizeof(s.title), "Corduroy");
    s.tempo.seg[0].bpm = 120.0;   /* two seconds a bar in 4/4 */
    s.tempo.nseg    = 1;
    s.tempo.sig_num = 4;
    s.tempo.sig_den = 4;
    s.count_in_bars = 2;
    return s;
}

static void test_reads_the_contract(void) {
    bt_lightshow *sh = NULL;
    BT_CHECK(bt_lightshow_load_mem(SHOW, strlen(SHOW), &sh, NULL) == BT_OK);
    BT_CHECK(sh != NULL);
    BT_CHECK(!strcmp(sh->port, "BackTrackQLC"));
    BT_CHECK_EQI(sh->velocity, 127);
    BT_CHECK_EQI(sh->nsongs, 1);
    BT_CHECK_EQI(sh->between_songs.ch, 16);
    BT_CHECK_EQI(sh->between_songs.note, 37);
    BT_CHECK_EQI(sh->stop_all.note, 39);
    BT_CHECK_EQI(sh->fog_dual.note, 77);

    /* A special the file does not mention is absent, not note 0 - sending
     * note 0 to channel 0 because a key was missing would be a real bug. */
    BT_CHECK_EQI(sh->fog_left.note, -1);

    const bt_light_song *ls = bt_lightshow_find(sh, "Corduroy");
    BT_CHECK(ls != NULL);
    BT_CHECK_EQI(ls->nev, 5);
    BT_CHECK(ls->ev[0].kind == BT_LIGHT_LOOK);
    BT_CHECK(ls->ev[2].kind == BT_LIGHT_HIT);
    BT_CHECK(ls->ev[4].kind == BT_LIGHT_END);
    BT_CHECK(!strcmp(ls->ev[1].label, "Verse 1"));

    /* Matched exactly, as the contract says. A near miss is not a match, and
     * a song with no lighting is not an error. */
    BT_CHECK(bt_lightshow_find(sh, "corduroy") == NULL);
    BT_CHECK(bt_lightshow_find(sh, "Numb") == NULL);

    bt_lightshow_free(sh);
}

/* Exactly means exactly, and the interesting case is a title that contains
 * another. Matching on a substring would light "Low" to "Low Rider"'s show,
 * or the other way about, and set lists really do carry titles like that. */
static void test_a_title_that_contains_another(void) {
    bt_lightshow *sh = NULL;
    const char *two =
        "{\"format\":\"random-riot-lighting/1\",\"songs\":["
        "{\"title\":\"Low\",\"events\":[{\"bar\":1,\"ch\":1,\"note\":5}]},"
        "{\"title\":\"Low Rider\",\"events\":[{\"bar\":1,\"ch\":1,\"note\":9}]}]}";
    BT_CHECK(bt_lightshow_load_mem(two, strlen(two), &sh, NULL) == BT_OK);

    const bt_light_song *a = bt_lightshow_find(sh, "Low");
    const bt_light_song *b = bt_lightshow_find(sh, "Low Rider");
    BT_CHECK(a != NULL && b != NULL);
    BT_CHECK_EQI(a->ev[0].note, 5);
    BT_CHECK_EQI(b->ev[0].note, 9);

    /* And a title that merely contains one of them is neither. */
    BT_CHECK(bt_lightshow_find(sh, "Low Rider (live)") == NULL);
    BT_CHECK(bt_lightshow_find(sh, "Lowdown") == NULL);
    bt_lightshow_free(sh);
}

/* A show built against a contract we do not implement would send the wrong
 * notes at the wrong bars. Refusing is the safe answer. */
static void test_refuses_a_format_it_does_not_know(void) {
    bt_lightshow *sh = NULL;
    const char *other =
        "{\"format\":\"random-riot-lighting/2\",\"songs\":[]}";
    BT_CHECK(bt_lightshow_load_mem(other, strlen(other), &sh, NULL) == BT_ERR_SCHEMA);
    BT_CHECK(sh == NULL);

    const char *none = "{\"songs\":[]}";
    BT_CHECK(bt_lightshow_load_mem(none, strlen(none), &sh, NULL) == BT_ERR_SCHEMA);
    BT_CHECK(sh == NULL);
}

/* Bar 1 is the downbeat after the count-in, so it is frame 0 however many
 * count-in bars the song has. */
static void test_bar_one_is_the_downbeat(void) {
    bt_lightshow *sh = NULL;
    BT_CHECK(bt_lightshow_load_mem(SHOW, strlen(SHOW), &sh, NULL) == BT_OK);
    const bt_light_song *ls = bt_lightshow_find(sh, "Corduroy");
    bt_song s = a_song();
    const int32_t sr = 48000;

    BT_CHECK_EQI((int)bt_light_event_frame(&s, &ls->ev[0], sr), 0);
    /* Bar 17 at 120bpm 4/4 is sixteen bars of two seconds. */
    BT_CHECK_EQI((int)bt_light_event_frame(&s, &ls->ev[1], sr), 32 * sr);
    bt_lightshow_free(sh);
}

static void test_what_fires_between_two_playheads(void) {
    bt_lightshow *sh = NULL;
    BT_CHECK(bt_lightshow_load_mem(SHOW, strlen(SHOW), &sh, NULL) == BT_OK);
    const bt_light_song *ls = bt_lightshow_find(sh, "Corduroy");
    bt_song s = a_song();
    const int32_t sr = 48000;
    int32_t out[8];

    /* The count-in sends nothing: bar 1 has not arrived. */
    BT_CHECK_EQI(bt_light_events_between(ls, &s, -4 * sr, -1, sr, out, 8), 0);

    /* Crossing bar 1 fires exactly the one event, once. */
    BT_CHECK_EQI(bt_light_events_between(ls, &s, -1, 0, sr, out, 8), 1);
    BT_CHECK_EQI(out[0], 0);
    /* And calling again from the new position does not fire it twice. */
    BT_CHECK_EQI(bt_light_events_between(ls, &s, 0, 100, sr, out, 8), 0);

    /* A long block picks up everything in it, in order. */
    BT_CHECK_EQI(bt_light_events_between(ls, &s, 0, 200 * sr, sr, out, 8), 4);
    BT_CHECK_EQI(out[0], 1);
    BT_CHECK_EQI(out[3], 4);
    bt_lightshow_free(sh);
}

/* Seeking is one question: what should the stage look like here. Hits that
 * were missed stay missed - a blast of fog owed from four minutes ago is not
 * a debt worth paying - and an end is not a look. */
static void test_seeking_restores_the_look_and_not_the_hits(void) {
    bt_lightshow *sh = NULL;
    BT_CHECK(bt_lightshow_load_mem(SHOW, strlen(SHOW), &sh, NULL) == BT_OK);
    const bt_light_song *ls = bt_lightshow_find(sh, "Corduroy");
    bt_song s = a_song();
    const int32_t sr = 48000;

    /* Before the song, nothing is showing yet. */
    BT_CHECK_EQI(bt_light_look_at(ls, &s, -1, sr), -1);
    /* On bar 1, the intro. */
    BT_CHECK_EQI(bt_light_look_at(ls, &s, 0, sr), 0);

    /* Landing at bar 40 - past the blinder hit at 33 - restores Verse 1, the
     * look, and not the blinder. */
    bt_light_event at40 = { 40, 1, 0, BT_LIGHT_LOOK, "" };
    BT_CHECK_EQI(bt_light_look_at(ls, &s, bt_light_event_frame(&s, &at40, sr), sr), 1);

    /* Past the end, the last *look* is still the chorus: seeking back into a
     * song must not leave the between-songs state up. */
    BT_CHECK_EQI(bt_light_look_at(ls, &s, (bt_frame)sr * 600, sr), 3);
    bt_lightshow_free(sh);
}

/* The file is somebody else's build output. One bad row should cost one
 * look, not the whole rig. */
static void test_a_bad_row_costs_one_look(void) {
    bt_lightshow *sh = NULL;
    const char *bad =
        "{\"format\":\"random-riot-lighting/1\",\"songs\":[{\"title\":\"x\",\"events\":["
        "{\"bar\":1,\"ch\":1,\"note\":5},"
        "{\"bar\":0,\"ch\":1,\"note\":5},"      /* bar 0 does not exist   */
        "{\"bar\":9,\"ch\":17,\"note\":5},"     /* no channel 17          */
        "{\"bar\":9,\"ch\":1,\"note\":200},"    /* no note 200            */
        "{\"bar\":17,\"ch\":1,\"note\":6}]}]}";
    BT_CHECK(bt_lightshow_load_mem(bad, strlen(bad), &sh, NULL) == BT_OK);
    const bt_light_song *ls = bt_lightshow_find(sh, "x");
    BT_CHECK(ls != NULL);
    BT_CHECK_EQI(ls->nev, 2);
    BT_CHECK_EQI(ls->ev[0].bar, 1);
    BT_CHECK_EQI(ls->ev[1].bar, 17);
    bt_lightshow_free(sh);
}

int main(void) {
    BT_RUN(test_reads_the_contract);
    BT_RUN(test_a_title_that_contains_another);
    BT_RUN(test_refuses_a_format_it_does_not_know);
    BT_RUN(test_bar_one_is_the_downbeat);
    BT_RUN(test_what_fires_between_two_playheads);
    BT_RUN(test_seeking_restores_the_look_and_not_the_hits);
    BT_RUN(test_a_bad_row_costs_one_look);
    BT_REPORT();
}
