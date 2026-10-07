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

    /* api is optional and defaults to empty, meaning "pick the best". */
    BT_CHECK_EQI(cfg.api[0], 0);

    const char *dev =
    "{\"device\":\"Focusrite\",\"api\":\"ASIO\",\"sample_rate\":48000,\"buffer_frames\":512,"
    "\"buses\":[{\"name\":\"foh\",\"channels\":[0,1]},"
    "{\"name\":\"inear\",\"channels\":[2,3]}]}";
    int line = 0;
    BT_CHECK_EQI(bt_device_cfg_load_mem(dev, strlen(dev), &cfg, &line), BT_OK);
    BT_CHECK_EQI(cfg.nbuses, 2);
    BT_CHECK_EQI(cfg.buffer_frames, 512);
    BT_CHECK(strcmp(cfg.api, "ASIO") == 0);
    BT_CHECK(strcmp(cfg.device, "Focusrite") == 0);
    const bt_bus *b = bt_device_find_bus(&cfg, "inear");
    BT_CHECK(b != NULL);
    if (b) { BT_CHECK_EQI(b->nch, 2); BT_CHECK_EQI(b->ch[0], 2); BT_CHECK_EQI(b->ch[1], 3); }

    const char *bad = "{\"sample_rate\":1}";
    BT_CHECK(bt_device_cfg_load_mem(bad, strlen(bad), &cfg, &line) != BT_OK);
    const char *badch =
    "{\"buses\":[{\"name\":\"x\",\"channels\":[999]}]}";
    BT_CHECK(bt_device_cfg_load_mem(badch, strlen(badch), &cfg, &line) != BT_OK);

    /* The lighting port is machine-local, like the audio interface, and is
     * absent from a device.json that has none. */
    {
        bt_device_cfg d;
        bt_device_cfg_defaults(&d);
        BT_CHECK(d.midi_out[0] == '\0');
    }
}

static void test_error_strings(void) {
    /* Every code must have a message, and they must be distinguishable - an
     * error the UI renders as "unknown error" is one nobody can act on. */
    const bt_err all[] = {
        BT_OK, BT_ERR_ALLOC, BT_ERR_IO, BT_ERR_PARSE, BT_ERR_SCHEMA,
        BT_ERR_FORMAT, BT_ERR_RATE, BT_ERR_RANGE, BT_ERR_NOT_FOUND, BT_ERR_STATE,
        BT_ERR_EXISTS
    };
    const size_t n = sizeof(all) / sizeof(all[0]);
    for (size_t i = 0; i < n; i++) {
        const char *m = bt_strerror(all[i]);
        BT_CHECK(m != NULL && strlen(m) > 0);
        for (size_t k = i + 1; k < n; k++)
            BT_CHECK(strcmp(m, bt_strerror(all[k])) != 0);
    }
    BT_CHECK(strcmp(bt_strerror((bt_err)9999), "unknown error") == 0);
}


/* A song with no stems - a click to play along to and nothing else - used to
 * have no length at all, so it counted in and stopped on the same frame.
 * This is Holiday in a set where the band plays and only the drummer needs
 * the click. */
static void test_click_only_song_has_a_length(void) {
    bt_song s;
    memset(&s, 0, sizeof(s));
    snprintf(s.title, sizeof(s.title), "Holiday");
    s.tempo.seg[0].bpm = 148.0;
    s.tempo.nseg    = 1;
    s.tempo.sig_num = 4;
    s.tempo.sig_den = 4;
    s.track[0].type = BT_TRACK_CLICK;
    s.ntracks = 1;

    BT_CHECK_EQI(bt_song_length(&s, 48000), 0);      /* the old behaviour */

    /* 64 bars of 4/4 at 148 bpm is 256 beats, a bit over 103 seconds. */
    s.length_bars = 64;
    bt_frame n = bt_song_length(&s, 48000);
    BT_CHECK(n > 0);
    BT_CHECK_NEAR((double)n / 48000.0, 256.0 * 60.0 / 148.0, 0.01);
}

/* A declared length longer than the stems extends the song: the click keeps
 * going for an outro the backing track does not cover. Shorter than the
 * stems, the stems win - truncating audio would be a surprise. */
static void test_declared_length_against_stems(void) {
    bt_song s;
    memset(&s, 0, sizeof(s));
    s.tempo.seg[0].bpm = 120.0;
    s.tempo.nseg    = 1;
    s.tempo.sig_num = 4;
    s.tempo.sig_den = 4;

    static float l[48000 * 10];
    static float *pcm[1] = { l };
    s.track[0].type     = BT_TRACK_AUDIO;
    s.track[0].pcm      = pcm;
    s.track[0].channels = 1;
    s.track[0].frames   = 48000 * 10;          /* ten seconds of stem */
    s.ntracks = 1;

    BT_CHECK_EQI(bt_song_length(&s, 48000), 48000 * 10);

    /* Shorter than the stems now ends the song there: "we play it to bar 114
     * and segue" is an arrangement, not a mistake. This used to let the stems
     * win, which meant the only way to shorten a song was to edit its audio. */
    s.length_bars = 2;                          /* 4 seconds: shorter */
    BT_CHECK_EQI(bt_song_length(&s, 48000), 48000 * 4);

    s.length_bars = 10;                         /* 20 seconds: longer */
    BT_CHECK_EQI(bt_song_length(&s, 48000), 48000 * 20);
}

/* Cutting a stem off mid-note is a click, so a song that ends early fades.
 * One that ends where its stems end does not - there is nothing to fade, and
 * adding one would quietly alter audio that had already finished. */
static void test_fade_only_when_the_song_is_cut_short(void) {
    bt_song s;
    memset(&s, 0, sizeof(s));
    s.tempo.seg[0].bpm = 120.0;
    s.tempo.nseg    = 1;
    s.tempo.sig_num = 4;
    s.tempo.sig_den = 4;

    static float l[48000 * 10];
    static float *pcm[1] = { l };
    s.track[0].type     = BT_TRACK_AUDIO;
    s.track[0].pcm      = pcm;
    s.track[0].channels = 1;
    s.track[0].frames   = 48000 * 10;           /* ten seconds of stem */
    s.ntracks = 1;

    /* No declared length: nothing is cut, so nothing fades. */
    BT_CHECK_EQI(bt_song_fade_frames(&s, 48000), 0);

    /* Declared longer than the stems: still nothing cut. */
    s.length_bars = 10;                          /* 20 seconds */
    BT_CHECK_EQI(bt_song_fade_frames(&s, 48000), 0);

    /* Declared shorter: the stem is still sounding there, so it fades. */
    s.length_bars = 2;                           /* 4 seconds */
    BT_CHECK_EQI(bt_song_fade_frames(&s, 48000), 48000);

    /* And the fade never swallows the bar you chose to end on. */
    s.length_bars = 1;                           /* 2 seconds */
    BT_CHECK_EQI(bt_song_fade_frames(&s, 48000), 48000 * 2 / 4);
}


/* Reordering tracks must carry everything about a track with it - including
 * the loader's pcm pointer, which is the one that would be silently wrong:
 * a mismatched pointer plays the wrong audio, or frees something twice. */
static void test_move_track_carries_everything(void) {
    bt_song s;
    memset(&s, 0, sizeof(s));
    static float a[4], b[4], c[4];
    static float *pa[1] = { a }, *pb[1] = { b }, *pc[1] = { c };

    snprintf(s.track[0].name, BT_MAX_NAME, "Click");
    s.track[0].type = BT_TRACK_CLICK;
    snprintf(s.track[1].name, BT_MAX_NAME, "Synth");
    s.track[1].type = BT_TRACK_AUDIO; s.track[1].pcm = pa; s.track[1].offset_ms = -300;
    snprintf(s.track[2].name, BT_MAX_NAME, "Bass");
    s.track[2].type = BT_TRACK_AUDIO; s.track[2].pcm = pb; s.track[2].gain_db = -6.0;
    snprintf(s.track[3].name, BT_MAX_NAME, "Pad");
    s.track[3].type = BT_TRACK_AUDIO; s.track[3].pcm = pc; s.track[3].muted = true;
    s.ntracks = 4;

    /* Pad to the top. The rest keep their order, rotated down. */
    BT_CHECK_EQI(bt_song_move_track(&s, 3, 0), BT_OK);
    BT_CHECK(strcmp(s.track[0].name, "Pad")   == 0);
    BT_CHECK(strcmp(s.track[1].name, "Click") == 0);
    BT_CHECK(strcmp(s.track[2].name, "Synth") == 0);
    BT_CHECK(strcmp(s.track[3].name, "Bass")  == 0);

    /* Each track still owns its own audio and its own settings. */
    BT_CHECK(s.track[0].pcm == pc && s.track[0].muted);
    BT_CHECK(s.track[2].pcm == pa);
    BT_CHECK_EQI(s.track[2].offset_ms, -300);
    BT_CHECK(s.track[3].pcm == pb);
    BT_CHECK_NEAR(s.track[3].gain_db, -6.0, 1e-9);

    /* And back down again returns the original order. */
    BT_CHECK_EQI(bt_song_move_track(&s, 0, 3), BT_OK);
    BT_CHECK(strcmp(s.track[0].name, "Click") == 0);
    BT_CHECK(strcmp(s.track[3].name, "Pad")   == 0);
    BT_CHECK(s.track[1].pcm == pa && s.track[2].pcm == pb && s.track[3].pcm == pc);
}

static void test_move_track_rejects_nonsense(void) {
    bt_song s;
    memset(&s, 0, sizeof(s));
    s.ntracks = 2;
    BT_CHECK_EQI(bt_song_move_track(NULL, 0, 1), BT_ERR_RANGE);
    BT_CHECK_EQI(bt_song_move_track(&s, -1, 0),  BT_ERR_RANGE);
    BT_CHECK_EQI(bt_song_move_track(&s, 0, 2),   BT_ERR_RANGE);
    BT_CHECK_EQI(bt_song_move_track(&s, 2, 0),   BT_ERR_RANGE);
    BT_CHECK_EQI(bt_song_move_track(&s, 1, 1),   BT_OK);   /* a no-op is fine */
}


/* The editor shows a tempo through this, so it has to be the truth as typed.
 * 96.515 shown as 96.52 is a different song - about a third of a beat out by
 * beat 1000 - and the file would disagree with the screen. */
static void test_format_number_is_the_truth(void) {
    char b[64];

    bt_format_number(96.515, b, sizeof(b));
    BT_CHECK(strcmp(b, "96.515") == 0);

    bt_format_number(138.78, b, sizeof(b));
    BT_CHECK(strcmp(b, "138.78") == 0);

    /* Whole numbers stay whole rather than growing decimals. */
    bt_format_number(120.0, b, sizeof(b));
    BT_CHECK(strcmp(b, "120") == 0);

    /* And it is shortest-that-round-trips, not longest: no float noise. */
    bt_format_number(0.1, b, sizeof(b));
    BT_CHECK(strcmp(b, "0.1") == 0);

    /* Never scientific notation for anything this model holds. A set list is
     * meant to be readable and editable by hand, and "1.2e+02" is neither -
     * which is what %g produced for a tempo of 120 before this preferred a
     * fixed form. */
    const double plain[] = { 120.0, 93.0, 156.0, 180.0, 1000.0, -6.0, 0.0 };
    for (size_t i = 0; i < sizeof(plain) / sizeof(plain[0]); i++) {
        bt_format_number(plain[i], b, sizeof(b));
        BT_CHECK(strchr(b, 'e') == NULL && strchr(b, 'E') == NULL);
    }

    /* Whatever comes out must parse back to the identical double. */
    const double vals[] = { 96.515, 138.78, 120.0, 0.1, 156.37, -12.5, 93.0 };
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        bt_format_number(vals[i], b, sizeof(b));
        BT_CHECK(strtod(b, NULL) == vals[i]);
    }
}


/* Mr Brightside as the band actually has it: 148 BPM, 4/4, cues on bars
 * 1, 17, 33, 41, 57, 73, 81, 97, and the end on 138. */
static bt_song cue_fixture(void) {
    bt_song s;
    memset(&s, 0, sizeof(s));
    snprintf(s.title, sizeof(s.title), "Mr Brightside");
    s.tempo.seg[0].bpm = 148.0;
    s.tempo.nseg    = 1;
    s.tempo.sig_num = 4;
    s.tempo.sig_den = 4;
    s.count_in_bars = 2;
    s.midi_program  = 3;

    const int32_t bars[] = { 1, 17, 33, 41, 57, 73, 81, 97 };
    for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); i++)
        s.light_cue[s.nlight_cues++].bar = bars[i];
    /* The end gets its own note. */
    s.light_cue[s.nlight_cues].bar  = 138;
    s.light_cue[s.nlight_cues].note = 37;
    s.nlight_cues++;
    return s;
}

/* A cue's bar has to land where the click lands, computed from the beat index
 * like everything else - at 148 BPM a cue on bar 97 is four minutes in, and
 * anything accumulated would be audibly adrift by then. */
static void test_cue_lands_on_its_bar(void) {
    bt_song s = cue_fixture();

    /* Bar 1 is beat 0 is frame 0. */
    BT_CHECK_EQI(bt_light_cue_frame(&s, &s.light_cue[0], 48000), 0);

    /* Bar 17 is beat 64. 64 * 60 / 148 seconds. */
    const double want17 = 64.0 * 60.0 / 148.0;
    BT_CHECK_EQI(bt_light_cue_frame(&s, &s.light_cue[1], 48000),
                 (bt_frame)llround(want17 * 48000.0));

    /* Bar 138 is beat 548, a shade over three and a half minutes. */
    const double want138 = 548.0 * 60.0 / 148.0;
    BT_CHECK_EQI(bt_light_cue_frame(&s, &s.light_cue[8], 48000),
                 (bt_frame)llround(want138 * 48000.0));

    /* A bar is however many beats the signature says. In 3/4 the same bar
     * number is three quarters of the way along, and a cue that assumed four
     * would fire a bar and a half late by bar 17. */
    s.tempo.sig_num = 3;
    const double want17_3 = 48.0 * 60.0 / 148.0;     /* beat 48, not 64 */
    BT_CHECK_EQI(bt_light_cue_frame(&s, &s.light_cue[1], 48000),
                 (bt_frame)llround(want17_3 * 48000.0));
}

/* A UI tick asks "what fired since last time". Each cue must come back
 * exactly once, however the frames are chopped up. */
static void test_each_cue_fires_exactly_once(void) {
    bt_song s = cue_fixture();
    const int32_t sr = 48000;

    int32_t seen[BT_MAX_LIGHT_CUES];
    memset(seen, 0, sizeof(seen));

    /* Walk the song in 16 ms steps, as a vsynced UI would. */
    const bt_frame step = sr / 60;
    const bt_frame end  = bt_light_cue_frame(&s, &s.light_cue[8], sr) + sr;
    int32_t out[BT_MAX_LIGHT_CUES];

    /* Starting at the count-in, so the playhead begins negative. */
    for (bt_frame t = -2 * 4 * sr; t < end; t += step) {
        const int32_t n = bt_song_cues_between(&s, t, t + step, sr,
                                               out, BT_MAX_LIGHT_CUES);
        for (int32_t i = 0; i < n; i++) seen[out[i]]++;
    }

    for (int32_t i = 0; i < s.nlight_cues; i++) BT_CHECK_EQI(seen[i], 1);
}

/* Jumping to the middle of a song: how many cues has the position passed?
 * This is what lets a cue list be resynced - reload the song, then advance
 * this many times. */
static void test_counting_cues_before_a_position(void) {
    bt_song s = cue_fixture();
    const int32_t sr = 48000;

    /* Before the song starts, nothing has gone by. */
    BT_CHECK_EQI(bt_song_cues_before(&s, -1, sr), 0);
    /* On bar 1 exactly, one has. */
    BT_CHECK_EQI(bt_song_cues_before(&s, 0, sr), 1);

    /* Just before and just after bar 41 - the fourth cue. */
    const bt_frame at41 = bt_light_cue_frame(&s, &s.light_cue[3], sr);
    BT_CHECK_EQI(bt_song_cues_before(&s, at41 - 1, sr), 3);
    BT_CHECK_EQI(bt_song_cues_before(&s, at41, sr), 4);

    /* Past the end, all of them. */
    BT_CHECK_EQI(bt_song_cues_before(&s, (bt_frame)sr * 600, sr), s.nlight_cues);
}

/* Most cues use the set list's next-cue note; the end of a song says its own. */
static void test_cue_note_resolution(void) {
    bt_song s = cue_fixture();
    bt_light_cfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.channel = 1;
    cfg.next_note = 38;
    cfg.end_note = 37;

    BT_CHECK_EQI(bt_light_cue_note(&cfg, &s.light_cue[0]), 38);
    BT_CHECK_EQI(bt_light_cue_note(&cfg, &s.light_cue[8]), 37);
}

int main(void) {
    BT_RUN(test_cue_lands_on_its_bar);
    BT_RUN(test_each_cue_fires_exactly_once);
    BT_RUN(test_counting_cues_before_a_position);
    BT_RUN(test_cue_note_resolution);
    BT_RUN(test_format_number_is_the_truth);
    BT_RUN(test_fade_only_when_the_song_is_cut_short);
    BT_RUN(test_move_track_carries_everything);
    BT_RUN(test_move_track_rejects_nonsense);
    BT_RUN(test_click_only_song_has_a_length);
    BT_RUN(test_declared_length_against_stems);
    BT_RUN(test_good);
    BT_RUN(test_defaults);
    BT_RUN(test_tempo_map_binding);
    BT_RUN(test_rejects);
    BT_RUN(test_path_containment);
    BT_RUN(test_device_cfg);
    BT_RUN(test_error_strings);
    BT_REPORT();
}
