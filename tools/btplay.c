/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Plays a set list through a real audio device.
 *
 * btrender answers "are the samples right". This answers "does the hardware
 * keep up", which is a different question and the only one an offline
 * renderer cannot touch.
 */
#include "backtrack/bt_player.h"
#include "backtrack/bt_device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void) {
    fprintf(stderr,
        "usage: btplay --list-devices\n"
        "       btplay <setlist.json> <device.json> <song-index>\n"
        "       btplay <setlist.json> <device.json> --set\n");
    return 2;
}

/* The audio callback. Forwards to the player and does nothing else. */
static void audio_cb(float *const *out, int32_t nframes, void *user) {
    bt_player_render((bt_player *)user, out, nframes);
}

static int list_devices(void) {
    bt_err e = bt_device_init();
    if (e != BT_OK) {
        fprintf(stderr, "audio backend (%s) failed to start\n", bt_device_backend());
        return 1;
    }
    int32_t n = bt_device_count();
    if (n == 0) {
        printf("no audio devices found (backend: %s)\n", bt_device_backend());
        printf("  on a headless machine this is expected; btrender works without one\n");
        bt_device_term();
        return 0;
    }

    printf("%-4s %-44s %-12s %4s %9s %8s\n",
           "idx", "name", "api", "out", "rate", "latency");
    for (int32_t i = 0; i < n; i++) {
        bt_device_info d;
        if (bt_device_get(i, &d) != BT_OK) continue;
        if (d.max_out_channels <= 0) continue;        /* input-only */
        printf("%-4d %-44.44s %-12.12s %4d %9.0f %6.1fms%s\n",
               d.index, d.name, d.api, d.max_out_channels,
               d.default_sample_rate, d.default_low_latency * 1000.0,
               d.is_default_output ? "  (default)" : "");
    }
    bt_device_term();
    return 0;
}

static void fmt_time(char *dst, size_t cap, bt_frame f, int32_t sr) {
    bool neg = f < 0;
    double s = (double)(neg ? -f : f) / (double)sr;
    snprintf(dst, cap, "%s%d:%05.2f", neg ? "-" : " ", (int)(s / 60.0),
             s - 60.0 * (double)(int)(s / 60.0));
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--list-devices") == 0) return list_devices();
    if (argc < 4) return usage();

    const char *setlist_path = argv[1];
    const char *device_path  = argv[2];
    const bool  whole_set    = (strcmp(argv[3], "--set") == 0);
    const int   song_index   = whole_set ? 0 : atoi(argv[3]);

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

    /* Widest channel any bus routes to. Opening fewer would silently drop the
     * click, which is the one failure nobody notices until they are on stage. */
    int32_t nch = 0;
    for (int32_t i = 0; i < dev.nbuses; i++)
        for (int32_t k = 0; k < dev.bus[i].nch; k++)
            if (dev.bus[i].ch[k] + 1 > nch) nch = dev.bus[i].ch[k] + 1;
    if (nch <= 0) {
        fprintf(stderr, "device.json defines no buses\n");
        bt_setlist_free(sl);
        return 1;
    }

    if (bt_device_init() != BT_OK) {
        fprintf(stderr, "audio backend (%s) failed to start\n", bt_device_backend());
        bt_setlist_free(sl);
        return 1;
    }
    if (bt_device_count() == 0) {
        fprintf(stderr, "no audio devices found (backend: %s)\n", bt_device_backend());
        fprintf(stderr, "  use btrender to verify the set list offline\n");
        bt_device_term();
        bt_setlist_free(sl);
        return 1;
    }

    /* "default" in device.json means whatever the machine calls default;
     * anything else is matched as a case-insensitive substring, so
     * "UMC404HD" finds it without anyone transcribing the full name. */
    int32_t idx = BT_DEVICE_DEFAULT;
    if (dev.device[0] && strcmp(dev.device, "default") != 0) {
        idx = bt_device_find(dev.device, NULL);
        if (idx == BT_DEVICE_DEFAULT)
            fprintf(stderr, "warning: no device matching \"%s\"; using the default\n",
                    dev.device);
    }

    bt_player_cfg pc = { dev.sample_rate, nch, dev.buffer_frames, 1 };
    bt_player *p = NULL;
    e = bt_player_create(&pc, sl, &dev, &p);
    if (e != BT_OK) {
        fprintf(stderr, "player: %s\n", bt_strerror(e));
        bt_device_term(); bt_setlist_free(sl);
        return 1;
    }

    e = bt_player_select(p, song_index);
    if (e != BT_OK) {
        fprintf(stderr, "loading song %d: %s\n", song_index, bt_strerror(e));
        if (e == BT_ERR_NOT_FOUND)
            fprintf(stderr, "  a track names a bus this device.json does not define\n");
        bt_player_destroy(p); bt_device_term(); bt_setlist_free(sl);
        return 1;
    }

    bt_device_open_cfg oc = { idx, nch, dev.sample_rate, dev.buffer_frames };
    bt_device *d = NULL;
    e = bt_device_open(&oc, audio_cb, p, &d);
    if (e != BT_OK) {
        fprintf(stderr, "opening device: %s\n", bt_strerror(e));
        if (d && bt_device_last_error(d)[0])
            fprintf(stderr, "  %s\n", bt_device_last_error(d));
        bt_device_close(d);
        bt_player_destroy(p); bt_device_term(); bt_setlist_free(sl);
        return 1;
    }

    int32_t got_rate = 0, got_buf = 0;
    double  got_lat  = 0.0;
    bt_device_actual(d, &got_rate, &got_buf, &got_lat);

    bt_device_info di;
    if (bt_device_get(idx == BT_DEVICE_DEFAULT ? bt_device_find("", NULL) : idx,
                      &di) != BT_OK)
        memset(&di, 0, sizeof(di));

    printf("device   %s (%s)\n", di.name[0] ? di.name : "default", di.api);
    printf("stream   %d Hz, %d ch, %.1f ms output latency\n",
           got_rate, nch, got_lat * 1000.0);
    printf("set      %s (%d songs)\n\n", sl->name, sl->nsongs);

    if (bt_device_start(d) != BT_OK) {
        fprintf(stderr, "starting stream: %s\n", bt_device_last_error(d));
        bt_device_close(d); bt_player_destroy(p);
        bt_device_term(); bt_setlist_free(sl);
        return 1;
    }

    bt_player_start(p);      /* count-in */

    int32_t shown = -1;
    bool    done  = false;
    while (!done) {
        /* The UI thread's pump: loading, advancing and every decision. The
         * audio thread is only ever running bt_player_render(). */
        bt_tick_result t = BT_TICK_IDLE;
        if (bt_player_tick(p, &t) != BT_OK) break;

        if (bt_player_current(p) != shown) {
            shown = bt_player_current(p);
            const bt_song *s = bt_player_song(p);
            printf("\n %2d. %s%s%s\n", shown + 1, s->title,
                   s->artist[0] ? " - " : "", s->artist);
        }

        char clk[32];
        fmt_time(clk, sizeof(clk), bt_player_playhead(p), got_rate);
        printf("\r     %s   resident %5.1f MB   xruns %llu   ",
               clk, (double)bt_player_resident_bytes(p) / (1024.0 * 1024.0),
               (unsigned long long)bt_device_xruns(d));
        fflush(stdout);

        if (bt_device_lost(d)) {
            printf("\n\n  ** audio device disappeared **\n");
            break;
        }
        if (t == BT_TICK_SONG_ENDED) {
            if (whole_set && bt_player_current(p) + 1 < bt_player_count(p)) {
                if (bt_player_next(p) != BT_OK) break;
                bt_player_play(p);
            } else {
                done = true;
            }
        }
        bt_device_sleep_ms(50);
    }

    printf("\n\nxruns: %llu\n", (unsigned long long)bt_device_xruns(d));

    bt_device_stop(d);
    bt_device_close(d);
    bt_player_destroy(p);
    bt_device_term();
    bt_setlist_free(sl);
    return 0;
}
