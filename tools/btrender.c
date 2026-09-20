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
#include "backtrack/bt_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void) {
    fprintf(stderr,
        "usage: btrender <setlist.json> <device.json> <song-index> <out.wav>\n"
        "                [--no-count-in] [--block N]\n");
    return 2;
}

static int32_t max_channel(const bt_device_cfg *d) {
    int32_t m = 0;
    for (int32_t i = 0; i < d->nbuses; i++)
        for (int32_t k = 0; k < d->bus[i].nch; k++)
            if (d->bus[i].ch[k] + 1 > m) m = d->bus[i].ch[k] + 1;
    return m;
}

int main(int argc, char **argv) {
    if (argc < 5) return usage();

    const char *setlist_path = argv[1];
    const char *device_path  = argv[2];
    int         song_index   = atoi(argv[3]);
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
