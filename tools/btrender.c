/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Offline renderer: set list in, WAV out, as fast as the CPU allows.
 *
 * This is not a demo. It is the harness the whole project is verified through:
 * because bt_engine_render() is a pure function of engine state, rendering a
 * song offline produces exactly the samples the audio device would have been
 * handed. Every question about timing, alignment and routing can be answered
 * here - on any platform, with no audio hardware attached.
 */
#include "backtrack/bt_engine.h"
#include "backtrack/bt_player.h"
#include "backtrack/bt_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define _USE_MATH_DEFINES
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int usage(void) {
    fprintf(stderr,
        "usage: btrender <setlist.json> <device.json> <song-index> <out.wav>\n"
        "                [--no-count-in] [--block N]\n"
        "       btrender <setlist.json> <device.json> --set <out.wav> [--block N]\n"
        "       btrender --make-demo <dir>\n"
        "\n"
        "  --set        render the whole set list as one continuous file,\n"
        "               following each song's on_end. Songs marked \"stop\"\n"
        "               would wait for a human on stage; an offline render\n"
        "               continues through them.\n"
        "  --make-demo  write a complete, runnable demo into <dir>: stems,\n"
        "               setlist.json and device.json. Needs nothing else\n"
        "               installed - the machine that plays a show should not\n"
        "               have to grow a toolchain to try this.\n");
    return 2;
}

/* ------------------------------------------------------------ demo set */

#ifdef _WIN32
  #include <direct.h>
  #define bt_mkdir(p) _mkdir(p)
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #define bt_mkdir(p) mkdir((p), 0777)
#endif

/* Create a directory, treating "it already exists" as success. Only one level
 * deep is needed here, and pretending otherwise would be more code than the
 * caller has use for. */
static bt_err make_dir(const char *path) {
    if (bt_mkdir(path) == 0) return BT_OK;
    return (errno == EEXIST) ? BT_OK : BT_ERR_IO;
}

static bt_err write_tone(const char *path, int32_t channels, double seconds,
                         int32_t rate, double hz, double amp) {
    bt_frame n = (bt_frame)(seconds * rate);
    float *buf[2] = { NULL, NULL };
    for (int32_t c = 0; c < channels; c++) {
        buf[c] = (float *)malloc((size_t)n * sizeof(float));
        if (!buf[c]) { for (int32_t k = 0; k < c; k++) free(buf[k]); return BT_ERR_ALLOC; }
        /* A slight detune per channel so a stereo stem is audibly stereo and
         * a routing mistake is obvious by ear. */
        double f = hz * (c ? 1.005 : 1.0);
        for (bt_frame i = 0; i < n; i++)
            buf[c][i] = (float)(amp * sin(2.0 * M_PI * f * (double)i / rate));
    }
    const float *p[2] = { buf[0], buf[1] };
    bt_err e = bt_wav_write_file(path, p, channels, rate, n);
    for (int32_t c = 0; c < channels; c++) free(buf[c]);
    return e;
}

static bt_err write_text_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return BT_ERR_IO;
    size_t n = strlen(text);
    bool ok = fwrite(text, 1, n, f) == n;
    if (fclose(f) != 0) ok = false;
    return ok ? BT_OK : BT_ERR_IO;
}

static int make_demo(const char *dir) {
    char path[BT_MAX_PATH * 2];

    if (make_dir(dir) != BT_OK) {
        fprintf(stderr, "could not create %s\n", dir);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/tracks", dir);
    if (make_dir(path) != BT_OK) {
        fprintf(stderr, "could not create %s\n", path);
        return 1;
    }

    struct { const char *name; int32_t ch; double sec; int32_t rate; double hz, amp; }
    stems[] = {
        /* The second song's pad is 44.1 kHz on purpose: a real set list mixes
         * rates, and this makes the demo exercise load-time resampling. */
        { "tracks/synth.wav", 2, 8.0, 48000, 440.0, 0.30 },
        { "tracks/bass.wav",  1, 8.0, 48000, 110.0, 0.40 },
        { "tracks/pad.wav",   2, 6.0, 44100, 220.0, 0.25 },
    };

    for (size_t i = 0; i < sizeof(stems) / sizeof(stems[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, stems[i].name);
        bt_err e = write_tone(path, stems[i].ch, stems[i].sec, stems[i].rate,
                              stems[i].hz, stems[i].amp);
        if (e != BT_OK) {
            fprintf(stderr, "%s: %s\n", path, bt_strerror(e));
            return 1;
        }
        printf("  %s  %dch  %.1fs  %d Hz\n", stems[i].name, stems[i].ch,
               stems[i].sec, stems[i].rate);
    }

    snprintf(path, sizeof(path), "%s/setlist.json", dir);
    bt_err e = write_text_file(path,
"{\n"
"  \"version\": 1,\n"
"  \"name\": \"Demo Set\",\n"
"  \"songs\": [\n"
"    {\n"
"      \"title\": \"Demo One\", \"artist\": \"BackingTrackLive\",\n"
"      \"tempo\": { \"bpm\": 120.0, \"sig\": [4, 4], \"downbeat_ms\": 0 },\n"
"      \"count_in_bars\": 1, \"on_end\": \"next\",\n"
"      \"tracks\": [\n"
"        { \"name\": \"Click\", \"type\": \"click\", \"bus\": \"inear\" },\n"
"        { \"name\": \"Synth\", \"type\": \"audio\", \"bus\": \"foh\",\n"
"          \"file\": \"tracks/synth.wav\", \"gain_db\": -2.0, \"offset_ms\": 0 },\n"
"        { \"name\": \"Bass\",  \"type\": \"audio\", \"bus\": \"foh\",\n"
"          \"file\": \"tracks/bass.wav\", \"gain_db\": 0.0, \"offset_ms\": 0 }\n"
"      ]\n"
"    },\n"
"    {\n"
"      \"title\": \"Demo Two\", \"artist\": \"BackingTrackLive\",\n"
"      \"tempo\": { \"bpm\": 96.0, \"sig\": [4, 4], \"downbeat_ms\": 0 },\n"
"      \"count_in_bars\": 2, \"on_end\": \"stop\",\n"
"      \"tracks\": [\n"
"        { \"name\": \"Click\", \"type\": \"click\", \"bus\": \"inear\" },\n"
"        { \"name\": \"Pad\",   \"type\": \"audio\", \"bus\": \"foh\",\n"
"          \"file\": \"tracks/pad.wav\", \"gain_db\": -4.0, \"offset_ms\": 0 }\n"
"      ]\n"
"    }\n"
"  ]\n"
"}\n");
    if (e != BT_OK) { fprintf(stderr, "%s: %s\n", path, bt_strerror(e)); return 1; }
    printf("  setlist.json\n");

    /* Stereo by default because every interface has two outputs. The comment
     * in the file says how to make it four. */
    snprintf(path, sizeof(path), "%s/device.json", dir);
    e = write_text_file(path,
"{\n"
"  \"_comment\": [\n"
"    \"Edit this for THIS machine; it is never committed.\",\n"
"    \"Run  btplay --list-devices  to see what is present.\",\n"
"    \"Stereo by default: band mix left, click right, split at the desk.\",\n"
"    \"For four separate outputs, name the 4-channel endpoint and use\",\n"
"    \"  \\\"device\\\": \\\"OUT 1-4\\\", foh [0,1] and inear [2,3].\"\n"
"  ],\n"
"  \"device\": \"default\",\n"
"  \"api\": \"\",\n"
"  \"sample_rate\": 48000,\n"
"  \"buffer_frames\": 512,\n"
"  \"buses\": [\n"
"    { \"name\": \"foh\",   \"channels\": [0] },\n"
"    { \"name\": \"inear\", \"channels\": [1] }\n"
"  ]\n"
"}\n");
    if (e != BT_OK) { fprintf(stderr, "%s: %s\n", path, bt_strerror(e)); return 1; }
    printf("  device.json\n");

    printf("\nwrote a runnable demo to %s\n", dir);
    printf("next:  btcheck %s/setlist.json %s/device.json\n", dir, dir);
    return 0;
}

/* Growable planar output. The length of a whole set is not known up front. */
typedef struct {
    float  **ch;
    int32_t  nch;
    bt_frame cap, len;
} growbuf;

static bool grow_to(growbuf *g, bt_frame need) {
    if (need <= g->cap) return true;
    bt_frame cap = g->cap ? g->cap : 1 << 16;
    while (cap < need) cap *= 2;
    for (int32_t c = 0; c < g->nch; c++) {
        float *q = (float *)realloc(g->ch[c], (size_t)cap * sizeof(float));
        if (!q) return false;
        memset(q + g->cap, 0, (size_t)(cap - g->cap) * sizeof(float));
        g->ch[c] = q;
    }
    g->cap = cap;
    return true;
}

/* Renders the whole set list through bt_player, which is what exercises song
 * advance, on_end and the preload window against real files. */
static int render_set(bt_setlist *sl, const bt_device_cfg *dev,
                      int32_t nch, int32_t block, const char *out_path) {
    /* 30 minutes is a guard against a malformed set list, not a real limit. */
    const bt_frame LIMIT = (bt_frame)dev->sample_rate * 60 * 30;

    bt_player_cfg pc = { dev->sample_rate, nch, block, 1, 30000 };
    bt_player *pl = NULL;
    bt_err e = bt_player_create(&pc, sl, dev, &pl);
    if (e != BT_OK) { fprintf(stderr, "player: %s\n", bt_strerror(e)); return 1; }

    e = bt_player_select(pl, 0);
    if (e != BT_OK) {
        fprintf(stderr, "selecting song 0: %s\n", bt_strerror(e));
        bt_player_destroy(pl);
        return 1;
    }

    growbuf g;
    memset(&g, 0, sizeof(g));
    g.nch = nch;
    g.ch = (float **)calloc((size_t)nch, sizeof(float *));
    if (!g.ch || !grow_to(&g, block)) { bt_player_destroy(pl); return 1; }

    printf("rendering set \"%s\" (%d songs)\n", sl->name, sl->nsongs);
    printf("  %2d. %s - %s\n", 1, sl->song[0].title, sl->song[0].artist);

    bt_player_start(pl);      /* count-in on the first song only */

    float *win[BT_MAX_OUT_CH];
    bool done = false;
    while (!done && g.len < LIMIT) {
        if (!grow_to(&g, g.len + block)) break;
        for (int32_t c = 0; c < nch; c++) win[c] = g.ch[c] + g.len;
        bt_player_render(pl, win, block);
        g.len += block;

        bt_tick_result t = BT_TICK_IDLE;
        if (bt_player_tick(pl, &t) != BT_OK) break;

        if (t == BT_TICK_ADVANCED) {
            printf("  %2d. %s - %s   (segue)\n", bt_player_current(pl) + 1,
                   bt_player_song(pl)->title, bt_player_song(pl)->artist);
        } else if (t == BT_TICK_SONG_ENDED) {
            if (bt_player_current(pl) + 1 >= bt_player_count(pl)) {
                done = true;
            } else {
                if (bt_player_next(pl) != BT_OK) { done = true; break; }
                printf("  %2d. %s - %s\n", bt_player_current(pl) + 1,
                       bt_player_song(pl)->title, bt_player_song(pl)->artist);
                bt_player_play(pl);
            }
        }
    }

    printf("  peak resident %.1f MB\n",
           (double)bt_player_resident_bytes(pl) / (1024.0 * 1024.0));

    e = bt_wav_write_file(out_path, (const float *const *)g.ch, nch,
                          dev->sample_rate, g.len);
    if (e != BT_OK) fprintf(stderr, "%s: %s\n", out_path, bt_strerror(e));
    else printf("  wrote %s: %d ch, %lld frames (%.2f s)\n", out_path, nch,
                (long long)g.len, (double)g.len / dev->sample_rate);

    for (int32_t c = 0; c < nch; c++) free(g.ch[c]);
    free(g.ch);
    bt_player_destroy(pl);
    return e == BT_OK ? 0 : 1;
}

static int32_t max_channel(const bt_device_cfg *d) {
    int32_t m = 0;
    for (int32_t i = 0; i < d->nbuses; i++)
        for (int32_t k = 0; k < d->bus[i].nch; k++)
            if (d->bus[i].ch[k] + 1 > m) m = d->bus[i].ch[k] + 1;
    return m;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--make-demo") == 0) return make_demo(argv[2]);
    if (argc < 5) return usage();

    const char *setlist_path = argv[1];
    const char *device_path  = argv[2];
    const bool  whole_set    = (strcmp(argv[3], "--set") == 0);
    int         song_index   = whole_set ? 0 : atoi(argv[3]);
    const char *out_path     = argv[4];

    bool    count_in = true;
    int32_t block    = 512;
    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "--no-count-in") == 0) count_in = false;
        else if (strcmp(argv[i], "--block") == 0 && i + 1 < argc) block = atoi(argv[++i]);
        else return usage();
    }
    if (block < 16 || block > 8192) { fprintf(stderr, "bad --block\n"); return 2; }

    int    line = 0;
    bt_err e;

    bt_device_cfg dev;
    e = bt_device_cfg_load_file(device_path, &dev, &line);
    if (e != BT_OK) {
        fprintf(stderr, "%s: %s (line %d)\n", device_path, bt_strerror(e), line);
        return 1;
    }

    bt_setlist *sl = NULL;
    e = bt_setlist_load_file(setlist_path, &sl, &line);
    if (e != BT_OK) {
        fprintf(stderr, "%s: %s (line %d)\n", setlist_path, bt_strerror(e), line);
        return 1;
    }
    int32_t nch_all = max_channel(&dev);
    if (nch_all <= 0) {
        fprintf(stderr, "device.json defines no buses\n");
        bt_setlist_free(sl);
        return 1;
    }

    if (whole_set) {
        int rc = render_set(sl, &dev, nch_all, block, out_path);
        bt_setlist_free(sl);
        return rc;
    }

    if (song_index < 0 || song_index >= sl->nsongs) {
        fprintf(stderr, "song index %d out of range (%d songs)\n",
                song_index, sl->nsongs);
        bt_setlist_free(sl);
        return 1;
    }

    bt_song *song = &sl->song[song_index];
    e = bt_song_load_audio(song, sl->dir, dev.sample_rate);
    if (e != BT_OK) {
        fprintf(stderr, "loading stems: %s\n", bt_strerror(e));
        bt_setlist_free(sl);
        return 1;
    }

    int32_t nch = max_channel(&dev);
    if (nch <= 0) { fprintf(stderr, "device.json defines no buses\n");
                    bt_setlist_free(sl); return 1; }

    bt_engine_cfg ecfg = { dev.sample_rate, nch, block };
    bt_engine *eng = NULL;
    e = bt_engine_create(&ecfg, &eng);
    if (e != BT_OK) { fprintf(stderr, "engine: %s\n", bt_strerror(e));
                      bt_setlist_free(sl); return 1; }

    e = bt_engine_set_song(eng, song, &dev);
    if (e != BT_OK) {
        fprintf(stderr, "binding song: %s\n", bt_strerror(e));
        if (e == BT_ERR_NOT_FOUND)
            fprintf(stderr, "  a track names a bus this device.json does not define\n");
        bt_engine_destroy(eng);
        bt_setlist_free(sl);
        return 1;
    }

    if (count_in) bt_engine_start_with_count_in(eng);
    else          { bt_engine_seek(eng, 0); bt_engine_play(eng); }

    bt_frame start = bt_engine_playhead(eng);
    bt_frame total = bt_song_length(song, dev.sample_rate) - start;
    if (total <= 0) { fprintf(stderr, "song is empty\n");
                      bt_engine_destroy(eng); bt_setlist_free(sl); return 1; }

    float **buf = (float **)calloc((size_t)nch, sizeof(float *));
    for (int32_t c = 0; c < nch; c++)
        buf[c] = (float *)calloc((size_t)total, sizeof(float));

    float *win[BT_MAX_OUT_CH];
    for (bt_frame pos = 0; pos < total; pos += block) {
        int32_t n = (int32_t)((total - pos < block) ? (total - pos) : block);
        for (int32_t c = 0; c < nch; c++) win[c] = buf[c] + pos;
        bt_engine_render(eng, win, n);
    }

    e = bt_wav_write_file(out_path, (const float *const *)buf, nch,
                          dev.sample_rate, total);
    if (e != BT_OK) fprintf(stderr, "%s: %s\n", out_path, bt_strerror(e));

    printf("%s - %s\n", song->title, song->artist);
    printf("  %.2f BPM  %d/%d  count-in %d bar(s)\n",
           song->tempo.seg[0].bpm, song->tempo.sig_num, song->tempo.sig_den,
           song->count_in_bars);
    printf("  %d track(s), %.1f MB resident\n",
           song->ntracks, (double)bt_song_pcm_bytes(song) / (1024.0 * 1024.0));
    printf("  wrote %s: %d ch, %lld frames (%.2f s)\n",
           out_path, nch, (long long)total, (double)total / dev.sample_rate);

    for (int32_t c = 0; c < nch; c++) free(buf[c]);
    free(buf);
    bt_engine_destroy(eng);
    bt_setlist_free(sl);
    return e == BT_OK ? 0 : 1;
}
