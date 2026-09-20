/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Checks a set list and prints everything wrong with it, in one pass.
 *
 * The audience for this is somebody sitting down with forty real songs the
 * week before a gig, not somebody debugging the engine.
 */
#include "backtrack/bt_validate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void) {
    fprintf(stderr,
        "usage: btcheck <setlist.json> [device.json] [--rate N]\n"
        "\n"
        "  Decodes every stem once and reports problems. Exits non-zero if\n"
        "  anything would stop the set list playing.\n");
    return 2;
}

static void fmt_mb(char *dst, size_t cap, size_t bytes) {
    double mb = (double)bytes / (1024.0 * 1024.0);
    if (mb >= 1024.0) snprintf(dst, cap, "%.2f GB", mb / 1024.0);
    else              snprintf(dst, cap, "%.1f MB", mb);
}

static void fmt_hms(char *dst, size_t cap, double s) {
    int h = (int)(s / 3600.0);
    int m = (int)((s - h * 3600.0) / 60.0);
    int sec = (int)(s - h * 3600.0 - m * 60.0);
    if (h) snprintf(dst, cap, "%dh %02dm %02ds", h, m, sec);
    else   snprintf(dst, cap, "%dm %02ds", m, sec);
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();

    const char *setlist_path = argv[1];
    const char *device_path  = NULL;
    int32_t     rate         = 48000;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc) rate = atoi(argv[++i]);
        else if (argv[i][0] != '-' && !device_path)         device_path = argv[i];
        else return usage();
    }
    if (rate < 8000 || rate > 192000) { fprintf(stderr, "bad --rate\n"); return 2; }

    int line = 0;

    bt_device_cfg  dev;
    bt_device_cfg *devp = NULL;
    if (device_path) {
        bt_err e = bt_device_cfg_load_file(device_path, &dev, &line);
        if (e != BT_OK) {
            fprintf(stderr, "%s: %s (line %d)\n", device_path, bt_strerror(e), line);
            return 1;
        }
        devp = &dev;
        rate = dev.sample_rate;
    }

    bt_setlist *sl = NULL;
    bt_err e = bt_setlist_load_file(setlist_path, &sl, &line);
    if (e != BT_OK) {
        fprintf(stderr, "%s: %s (line %d)\n", setlist_path, bt_strerror(e), line);
        return 1;
    }

    bt_issue        *issues = NULL;
    size_t           n      = 0;
    bt_setlist_stats st;
    e = bt_setlist_validate(sl, devp, rate, &issues, &n, &st);
    if (e != BT_OK) {
        fprintf(stderr, "validating: %s\n", bt_strerror(e));
        bt_setlist_free(sl);
        return 1;
    }

    printf("%s - %d song(s), %d track(s)\n", sl->name, st.songs, st.tracks);
    if (devp) printf("checked against %s at %d Hz\n\n", device_path, rate);
    else      printf("no device.json given, so bus names are unchecked\n\n");

    /* Errors first: they are what stops the show. */
    for (int pass = 0; pass < 2; pass++) {
        bt_issue_level want = pass == 0 ? BT_ISSUE_ERROR : BT_ISSUE_WARN;
        for (size_t i = 0; i < n; i++) {
            if (issues[i].level != want) continue;
            /* Song titles are up to BT_MAX_NAME; two of them plus indices
             * need the room, and GCC is right to insist. */
            char where[2 * BT_MAX_NAME + 64];
            if (issues[i].song < 0)
                snprintf(where, sizeof(where), "set list");
            else if (issues[i].track < 0)
                snprintf(where, sizeof(where), "song %d (%s)",
                         issues[i].song + 1, sl->song[issues[i].song].title);
            else
                snprintf(where, sizeof(where), "song %d (%s) track %d (%s)",
                         issues[i].song + 1, sl->song[issues[i].song].title,
                         issues[i].track + 1,
                         sl->song[issues[i].song].track[issues[i].track].name);
            printf("  %-7s %s: %s\n", bt_issue_level_name(issues[i].level),
                   where, issues[i].msg);
        }
    }
    if (n) printf("\n");

    char peak[32], all[32], total[32], longest[32];
    fmt_mb(peak, sizeof(peak), st.peak_resident_bytes);
    fmt_mb(all,  sizeof(all),  st.all_resident_bytes);
    fmt_hms(total,   sizeof(total),   st.total_seconds);
    fmt_hms(longest, sizeof(longest), st.longest_seconds);

    printf("  set length      %s of audio (longest song %s)\n", total, longest);
    printf("  preload peak    %s   (current + next song)\n", peak);
    printf("  whole set       %s   if every song were held at once\n", all);
    printf("\n  %d error(s), %d warning(s)\n", st.errors, st.warnings);

    free(issues);
    bt_setlist_free(sl);
    return st.errors ? 1 : 0;
}
