/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The stage screen.
 *
 * Two views, switched automatically by transport state, because the thing you
 * need when stopped and the thing you need mid-song are different and there
 * is no spare attention on stage for a mode you have to remember.
 *
 *   stopped  the set list fills the screen - you are choosing what is next
 *   playing  bar and beat dominate, with the following song underneath
 *
 * What is deliberately NOT large in the playing view: the song title and the
 * time remaining. You know what you are playing, and the arrangement tells
 * you how long is left. What you cannot recover on your own, if you lose your
 * place, is which bar you are in.
 */
#ifndef BT_UI_H
#define BT_UI_H

#include "backtrack/bt_model.h"

/* Everything the screen draws, gathered once per frame so the draw code never
 * reaches into the player or the engine. That keeps rendering testable with
 * fabricated state, which is how the screenshots below are produced without a
 * device or a loaded set list. */
struct bt_ui_state {
    const bt_setlist *setlist;
    int32_t  current;        /* index of the bound song, -1 for none */
    int32_t  selected;       /* highlighted row; may differ while browsing */
    bool     playing;
    bt_frame playhead;       /* negative during the count-in */
    int32_t  sample_rate;

    /* Derived, so the drawing code does no arithmetic of consequence. */
    int64_t  beat;           /* absolute beat index; negative = count-in */
    int32_t  count_in_left;  /* beats still to go; only read when counting  */
    int32_t  bar;            /* 1-based bar number within the song */
    int32_t  beat_in_bar;    /* 0-based */
    int32_t  beats_per_bar;
    double   bpm;
    double   elapsed_sec;
    double   total_sec;
    uint64_t xruns;
    bool     show_clock;     /* off by default: a number that invites you to
                              * read it and then tells you nothing you need */
};

void bt_ui_draw(const bt_ui_state &st);

#endif /* BT_UI_H */
