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

    /* Where we are inside the current beat, 0 at the beat and approaching 1
     * just before the next. For anything that has to move in time with the
     * click rather than at a rate of its own. */
    float    beat_phase;

    /* Bars left until this song runs into the next one, fractional, or -1
     * when it does not: the song ends, it is the last in the set, or
     * nothing says how long it is. The screen decides how early to start
     * warning; this is only the distance. */
    float    segue_bars;
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

    /* Whether B will do anything: lighting is running and the cue file
     * defines a between_songs special. The footer offers the key only when
     * pressing it would send something. */
    bool     between_songs_ready;

    /* Where the lighting show thinks the song is.
     *
     * The description somebody typed - "Chorus 1", "Bridge" - which until now
     * existed only in the editor. On stage it answers two questions at once:
     * the band's "where are we", and, while a show is being built, whether
     * the change landed on the bar it was meant to.
     *
     * Derived from the playhead rather than from what was last sent, so it is
     * right after a seek, right with no MIDI port open at all, and cannot
     * drift away from what the stage is doing.
     *
     * cue_now is the look showing: hits and ends are not looks and do not
     * take the line over, because fog at bar 49 does not mean the band left
     * the chorus. NULL before the first cue of the song, or for a cue whose
     * description was left blank. */
    const char *cue_now;
    bool        cue_any;        /* this song has cues: draw the line at all */
    bool        cue_more;       /* and one of them is still to come         */
    const char *cue_next;       /* its description, or NULL if it has none  */
    int32_t     cue_next_bar;   /* read only when cue_more                  */

    /* Q has just written a cue down, and this says so for a bar or two.
     *
     * Pressing Q sends nothing and changes nothing on stage, which is what
     * makes it safe mid-song and also means the screen is the only possible
     * confirmation that it worked. Timed in frames by the host, from the
     * sample counter like everything else here. */
    bool     tap_flash;
    int32_t  tap_flash_bar;

    /* Q is armed to tap cues in. Shown on the play screen because it is a
     * mode that writes to the show: a key that quietly adds cues is fine
     * while that is what you are doing and alarming if you have forgotten
     * it is on, and the footer is where the other keys already are. */
    bool     tap_armed;

    /* Whether the window is fullscreen right now.
     *
     * The host owns the window; this is only what it tells the screen, so
     * the corner button can offer the move you do not already have. */
    bool     fullscreen;
};

/* What the mouse asked for this frame.
 *
 * Play mode was built keyboard-first for the stage, which is right, and it
 * meant a new user clicking a song got nothing at all - the set list looked
 * like a list of buttons and behaved like a wall. Pointing at a song is the
 * most obvious thing anyone will try. */
enum class bt_ui_click { none, select, play, fullscreen };

struct bt_ui_result {
    bt_ui_click click = bt_ui_click::none;
    int32_t     song  = -1;
};

bt_ui_result bt_ui_draw(const bt_ui_state &st);

#endif /* BT_UI_H */
