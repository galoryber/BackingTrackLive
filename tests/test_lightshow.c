/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The lighting side's cue file, and the three questions the sender asks of
 * it: what fires between these two playheads, what should the stage look
 * like at this position, and what does a file we do not understand do.
 */
#include "backtrack/bt_lightshow.h"
#include "bt_test.h"

#include <stdio.h>
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
    const bt_light_special *bs = bt_lightshow_special(sh, "between_songs");
    BT_CHECK(bs != NULL);
    BT_CHECK_EQI(bs->ch, 16);
    BT_CHECK_EQI(bs->note, 37);
    BT_CHECK_EQI(bt_lightshow_special(sh, "stop_all")->note, 39);
    BT_CHECK_EQI(bt_lightshow_special(sh, "fog_dual")->note, 77);

    /* A special the file does not mention is absent, not note 0 - sending
     * note 0 to channel 0 because a key was missing would be a real bug. */
    BT_CHECK(bt_lightshow_special(sh, "fog_left") == NULL);

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

/* The cue file is read off disk in the real thing, and the disk has its own
 * failure modes: it is not there at all, which is the ordinary case for a set
 * list without lighting, and it is there but unreadable. */
static void test_reading_it_off_disk(void) {
    const char *tmp = "lightshow_test_cues.json";

    FILE *f = fopen(tmp, "wb");
    BT_CHECK(f != NULL);
    fwrite(SHOW, 1, strlen(SHOW), f);
    fclose(f);

    bt_lightshow *sh = NULL;
    int line = 0;
    BT_CHECK_EQI(bt_lightshow_load_file(tmp, &sh, &line), BT_OK);
    BT_CHECK(sh != NULL);
    BT_CHECK_EQI(sh->nsongs, 1);
    BT_CHECK(!strcmp(sh->port, "BackTrackQLC"));
    BT_CHECK(bt_lightshow_find(sh, "Corduroy") != NULL);
    bt_lightshow_free(sh);
    remove(tmp);

    /* No cue file is how most set lists look, and it must not be an error
     * the caller has to special-case beyond "no lighting". */
    sh = NULL;
    BT_CHECK_EQI(bt_lightshow_load_file("no_such_cue_file.json", &sh, &line),
                 BT_ERR_IO);
    BT_CHECK(sh == NULL);

    /* A file that is there but is not a cue file. */
    f = fopen(tmp, "wb");
    BT_CHECK(f != NULL);
    fputs("{\"format\":\"something-else/9\"}", f);
    fclose(f);
    sh = NULL;
    BT_CHECK_EQI(bt_lightshow_load_file(tmp, &sh, &line), BT_ERR_SCHEMA);
    BT_CHECK(sh == NULL);
    remove(tmp);

    /* And one that is not JSON at all. */
    f = fopen(tmp, "wb");
    BT_CHECK(f != NULL);
    fputs("not json", f);
    fclose(f);
    sh = NULL;
    BT_CHECK(bt_lightshow_load_file(tmp, &sh, &line) == BT_ERR_PARSE);
    BT_CHECK(sh == NULL);
    remove(tmp);

    /* Nothing left behind: the fixture above bit this project once. */
    BT_CHECK(fopen(tmp, "rb") == NULL);
}

/* Arguments that cannot be honoured, rather than crashed on. */
static void test_refuses_nonsense_arguments(void) {
    bt_lightshow *sh = NULL;
    BT_CHECK_EQI(bt_lightshow_load_file(NULL, &sh, NULL), BT_ERR_RANGE);
    BT_CHECK_EQI(bt_lightshow_load_file("x", NULL, NULL), BT_ERR_RANGE);
    BT_CHECK_EQI(bt_lightshow_load_mem(NULL, 0, &sh, NULL), BT_ERR_RANGE);
    BT_CHECK(bt_lightshow_find(NULL, "a") == NULL);
    BT_CHECK(bt_light_event_frame(NULL, NULL, 48000) == 0);
    BT_CHECK_EQI(bt_light_look_at(NULL, NULL, 0, 48000), -1);
    BT_CHECK_EQI(bt_light_events_between(NULL, NULL, 0, 1, 48000, NULL, 0), 0);
    /* Freeing nothing is allowed, as everywhere else here. */
    bt_lightshow_free(NULL);
}

/* The cue file is edited in place now, so this program is one of its two
 * authors. Everything it reads it must be able to write back unchanged -
 * including a special it has never heard of, which is the part a writer that
 * only knows its own fields gets wrong. */
static void test_writing_it_back_loses_nothing(void) {
    const char *with_odd_special =
        "{\n"
        "  \"format\": \"random-riot-lighting/1\",\n"
        "  \"midi_port\": \"BackTrackQLC\",\n"
        "  \"velocity\": 110,\n"
        "  \"specials\": {\n"
        "    \"between_songs\": { \"ch\": 16, \"note\": 37 },\n"
        "    \"stop_all\":      { \"ch\": 16, \"note\": 39 },\n"
        "    \"confetti\":      { \"ch\": 16, \"note\": 90 }\n"
        "  },\n"
        "  \"songs\": [\n"
        "    { \"title\": \"Corduroy\", \"events\": [\n"
        "      { \"bar\": 1,  \"ch\": 1,  \"note\": 0, \"type\": \"look\", \"label\": \"Intro\" },\n"
        "      { \"bar\": 33, \"ch\": 16, \"note\": 72, \"type\": \"hit\" },\n"
        "      { \"bar\": 65, \"ch\": 16, \"note\": 37, \"type\": \"end\", \"label\": \"Between\" }\n"
        "    ] },\n"
        "    { \"title\": \"Low Rider\", \"events\": [\n"
        "      { \"bar\": 1, \"ch\": 1, \"note\": 4, \"type\": \"look\" }\n"
        "    ] }\n"
        "  ]\n"
        "}\n";

    bt_lightshow *a = NULL;
    BT_CHECK(bt_lightshow_load_mem(with_odd_special, strlen(with_odd_special),
                                   &a, NULL) == BT_OK);
    char *text = NULL; size_t len = 0;
    BT_CHECK(bt_lightshow_to_json(a, &text, &len) == BT_OK);
    BT_CHECK(text != NULL);

    bt_lightshow *b = NULL;
    BT_CHECK(bt_lightshow_load_mem(text, len, &b, NULL) == BT_OK);

    BT_CHECK(!strcmp(a->port, b->port));
    BT_CHECK_EQI(a->velocity, b->velocity);
    BT_CHECK_EQI(a->nsongs, b->nsongs);

    /* "confetti" is not a special this program knows by name, and it has to
     * come back anyway. */
    BT_CHECK_EQI(a->nspecials, b->nspecials);
    for (int32_t i = 0; i < a->nspecials; i++) {
        const bt_light_special *x = &a->special[i];
        const bt_light_special *y = bt_lightshow_special(b, x->name);
        BT_CHECK(y != NULL);
        BT_CHECK_EQI(x->ch, y->ch);
        BT_CHECK_EQI(x->note, y->note);
    }
    BT_CHECK(bt_lightshow_special(b, "confetti") != NULL);
    BT_CHECK_EQI(bt_lightshow_special(b, "confetti")->note, 90);

    for (int32_t i = 0; i < a->nsongs; i++) {
        BT_CHECK(!strcmp(a->song[i].title, b->song[i].title));
        BT_CHECK_EQI(a->song[i].nev, b->song[i].nev);
        for (int32_t k = 0; k < a->song[i].nev; k++) {
            const bt_light_event *x = &a->song[i].ev[k];
            const bt_light_event *y = &b->song[i].ev[k];
            BT_CHECK_EQI(x->bar, y->bar);
            BT_CHECK_EQI(x->ch, y->ch);
            BT_CHECK_EQI(x->note, y->note);
            BT_CHECK(x->kind == y->kind);
            BT_CHECK(!strcmp(x->label, y->label));
        }
    }

    /* And writing what we just read gives the identical bytes, so saving a
     * file nobody changed does not show up as a change. */
    char *again = NULL; size_t alen = 0;
    BT_CHECK(bt_lightshow_to_json(b, &again, &alen) == BT_OK);
    BT_CHECK_EQI((int)alen, (int)len);
    BT_CHECK(!memcmp(text, again, len));

    free(again);
    free(text);
    bt_lightshow_free(a);
    bt_lightshow_free(b);
}

/* Saving goes through a temporary and a rename, so an interrupted save
 * cannot leave a half-written show where a working one was. */
static void test_saving_to_disk(void) {
    const char *path = "lightshow_save_test.json";
    bt_lightshow *sh = NULL;
    BT_CHECK(bt_lightshow_load_mem(SHOW, strlen(SHOW), &sh, NULL) == BT_OK);

    /* Move a bar, which is the whole reason this program writes the file. */
    bt_light_song *ls = bt_lightshow_song(sh, "Corduroy");
    BT_CHECK(ls != NULL);
    ls->ev[1].bar = 20;

    BT_CHECK(bt_lightshow_save_file(sh, path) == BT_OK);
    bt_lightshow_free(sh);

    bt_lightshow *back = NULL;
    BT_CHECK(bt_lightshow_load_file(path, &back, NULL) == BT_OK);
    BT_CHECK_EQI(bt_lightshow_find(back, "Corduroy")->ev[1].bar, 20);
    bt_lightshow_free(back);

    remove(path);
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    BT_CHECK(fopen(tmp, "rb") == NULL);   /* nothing left behind */
}

/* Adding a cue to a song the show has never heard of, which is how a set
 * list that started with no cue file gets one. */
static void test_adding_and_removing_cues(void) {
    bt_lightshow *sh = NULL;
    const char *empty = "{\"format\":\"random-riot-lighting/1\",\"songs\":[]}";
    BT_CHECK(bt_lightshow_load_mem(empty, strlen(empty), &sh, NULL) == BT_OK);
    BT_CHECK_EQI(sh->nsongs, 0);

    bt_light_event *c = NULL;
    BT_CHECK(bt_lightshow_add_cue(sh, "Corduroy", &c) == BT_OK);
    BT_CHECK(c != NULL);
    BT_CHECK_EQI(sh->nsongs, 1);
    /* The first cue of a song starts at the top on channel 1. */
    BT_CHECK_EQI(c->bar, 1);
    BT_CHECK_EQI(c->ch, 1);
    BT_CHECK_EQI(c->note, 0);

    /* The next carries the channel over and steps the note, because
     * consecutive looks on a desk are usually neighbours. */
    BT_CHECK(bt_lightshow_add_cue(sh, "Corduroy", &c) == BT_OK);
    BT_CHECK_EQI(c->bar, 9);
    BT_CHECK_EQI(c->ch, 1);
    BT_CHECK_EQI(c->note, 1);

    bt_light_song *ls = bt_lightshow_song(sh, "Corduroy");
    BT_CHECK(ls != NULL);
    BT_CHECK_EQI(ls->nev, 2);

    /* A second song does not disturb the first. */
    BT_CHECK(bt_lightshow_add_cue(sh, "Low Rider", NULL) == BT_OK);
    BT_CHECK_EQI(sh->nsongs, 2);
    BT_CHECK_EQI(bt_lightshow_song(sh, "Corduroy")->nev, 2);

    /* Removing shifts the rest down. */
    ls = bt_lightshow_song(sh, "Corduroy");
    ls->ev[0].bar = 5;
    ls->ev[1].bar = 17;
    BT_CHECK(bt_lightshow_remove_cue(sh, "Corduroy", 0) == BT_OK);
    BT_CHECK_EQI(bt_lightshow_song(sh, "Corduroy")->nev, 1);
    BT_CHECK_EQI(bt_lightshow_song(sh, "Corduroy")->ev[0].bar, 17);

    /* Out of range is refused rather than corrupting anything. */
    BT_CHECK(bt_lightshow_remove_cue(sh, "Corduroy", 9) == BT_ERR_RANGE);
    BT_CHECK(bt_lightshow_remove_cue(sh, "Nope", 0) == BT_ERR_RANGE);
    BT_CHECK(bt_lightshow_add_cue(sh, "", NULL) == BT_ERR_RANGE);
    BT_CHECK(bt_lightshow_add_cue(NULL, "x", NULL) == BT_ERR_RANGE);

    /* And what was added survives being written out and read back. */
    char *text = NULL; size_t len = 0;
    BT_CHECK(bt_lightshow_to_json(sh, &text, &len) == BT_OK);
    bt_lightshow *back = NULL;
    BT_CHECK(bt_lightshow_load_mem(text, len, &back, NULL) == BT_OK);
    BT_CHECK_EQI(back->nsongs, 2);
    BT_CHECK_EQI(bt_lightshow_song(back, "Corduroy")->ev[0].bar, 17);
    free(text);
    bt_lightshow_free(back);
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
    BT_RUN(test_adding_and_removing_cues);
    BT_RUN(test_writing_it_back_loses_nothing);
    BT_RUN(test_saving_to_disk);
    BT_RUN(test_reading_it_off_disk);
    BT_RUN(test_refuses_nonsense_arguments);
    BT_REPORT();
}
