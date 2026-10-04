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

    /* The audio device, on screen rather than in the window title.
     *
     * The title bar was where this lived, which is invisible in fullscreen
     * and easy to miss anywhere - so a working interface looked identical to
     * a missing one, and an interface that vanished mid-song explained itself
     * somewhere nobody was looking. */
    bool        device_live;
    const char *device_name;     /* may be NULL */
    const char *device_note;     /* why it is not live, or NULL */

    /* The song finished and the set is waiting on a human.
     *
     * A song that ends used to drop straight back to the set list, which is
     * tidy and is not what the moment is: the band has just finished, someone
     * is talking to the room, and what matters is that the next song is ready
     * and one key starts it. The list is still a key away for choosing
     * something else. */
    /* A start that is waiting for the loader to bring a song into memory.
     * Shown plainly: a key that appears to do nothing is the worst thing a
     * stage program can do, and "it is loading" is the whole explanation. */
    bool     loading;
    int32_t  loading_song;

    bool     armed;          /* stopped, holding on the playing screen */
    int32_t  armed_song;     /* what SPACE will start                  */

    bool     show_clock;     /* off by default: a number that invites you to
                              * read it and then tells you nothing you need */
};

/* What the mouse asked for this frame.
 *
 * Play mode was built keyboard-first for the stage, which is right, and it
 * meant a new user clicking a song got nothing at all - the set list looked
 * like a list of buttons and behaved like a wall. Pointing at a song is the
 * most obvious thing anyone will try. */
enum class bt_ui_click { none, select, play };

struct bt_ui_result {
    bt_ui_click click = bt_ui_click::none;
    int32_t     song  = -1;
};

bt_ui_result bt_ui_draw(const bt_ui_state &st);

#endif /* BT_UI_H */
