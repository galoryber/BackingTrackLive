/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * MIDI output, from a terminal. For proving the port before trusting a show
 * to it: list what is there, and send one note at something.
 */
#include "backtrack/bt_midi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void) {
    printf("btmidi - list MIDI outputs, or send a note\n\n"
           "  btmidi --list\n"
           "  btmidi <port-substring> <note> [channel] [velocity]\n\n"
           "A port substring matches the name shown by --list; a loopMIDI\n"
           "port appears under whatever it was named there.\n\n"
           "  btmidi loopMIDI 36          note 36, channel 1, velocity 127\n"
           "  btmidi loopMIDI 36 2 100    note 36, channel 2, velocity 100\n");
    return 2;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();

    if (!strcmp(argv[1], "--list") || !strcmp(argv[1], "-l")) {
        const int32_t n = bt_midi_count();
        if (n == 0) {
            printf("no MIDI outputs\n\n"
                   "On Windows, install loopMIDI and create a port; it will\n"
                   "appear here, and QLC+ opens the same port as an input.\n");
            return 1;
        }
        printf("%d MIDI output(s):\n", n);
        for (int32_t i = 0; i < n; i++) {
            bt_midi_info info;
            if (bt_midi_get(i, &info) != BT_OK) continue;
            printf("  %2d  %s\n", info.index, info.name);
        }
        return 0;
    }

    if (argc < 3) return usage();

    const char *want = argv[1];
    const int32_t note = atoi(argv[2]);
    const int32_t ch   = argc > 3 ? atoi(argv[3]) : 1;
    const int32_t vel  = argc > 4 ? atoi(argv[4]) : 127;

    const int32_t idx = bt_midi_find(want);
    if (idx < 0) {
        fprintf(stderr, "no MIDI output matching \"%s\" - try --list\n", want);
        return 1;
    }

    bt_midi_info info;
    bt_midi_get(idx, &info);

    bt_midi *m = NULL;
    bt_err e = bt_midi_open(idx, &m);
    if (e != BT_OK) {
        fprintf(stderr, "could not open %s: %s\n", info.name, bt_strerror(e));
        return 1;
    }

    e = bt_midi_trigger(m, ch, note, vel);
    if (e != BT_OK) fprintf(stderr, "send failed: %s\n", bt_strerror(e));
    else            printf("sent note %d on channel %d to %s\n", note, ch, info.name);

    bt_midi_close(m);
    return e == BT_OK ? 0 : 1;
}
