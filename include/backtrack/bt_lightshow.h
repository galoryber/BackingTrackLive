/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The lighting show: a list of notes to send at bar positions, produced by
 * the lighting side and read here.
 *
 * This file is not ours. QLC+ and its generator own what every look is, which
 * bar each section starts on, and which note number means which look; all we
 * do is send the note when the playhead reaches the bar. The contract is
 * `random-riot-lighting/1` and is written down in docs/lighting.md.
 *
 * Every event is absolute - "show look number N" - rather than "advance".
 * That is the whole reason this is simpler than what it replaces: a song that
 * is skipped, restarted or scrubbed cannot leave the desk a step out, because
 * there is no step to be out by.
 */
#ifndef BT_LIGHTSHOW_H
#define BT_LIGHTSHOW_H

#include "backtrack/bt_error.h"
#include "backtrack/bt_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_LIGHTSHOW_FORMAT "random-riot-lighting/1"

/* Informational: the sender treats every event alike. The one place it is
 * load bearing is seeking, where a missed look has to be caught up and a
 * missed hit - a strobe, a blast of fog - must not be. */
typedef enum {
    BT_LIGHT_LOOK = 0,   /* the stage now looks like this                */
    BT_LIGHT_HIT,        /* a moment: blinder, fog. Never replayed late. */
    BT_LIGHT_END         /* the song is over                             */
} bt_light_kind;

typedef struct {
    int32_t       bar;     /* 1-based, bar 1 is the downbeat after the count-in */
    int32_t       ch;      /* 1-16, as bt_midi_* counts                        */
    int32_t       note;    /* 0-127                                            */
    bt_light_kind kind;
    char          label[BT_MAX_CUE];   /* for the screen and the log           */
} bt_light_event;

typedef struct {
    char            title[BT_MAX_NAME];  /* matched against the set list */
    bt_light_event *ev;
    int32_t         nev;
} bt_light_song;

/* A note that is not tied to a bar - something a person presses. */
typedef struct {
    int32_t ch;
    int32_t note;
} bt_light_special;

typedef struct {
    char             port[BT_MAX_NAME];   /* substring for bt_midi_find    */
    int32_t          velocity;
    bt_light_special between_songs, stop_all, blinder;
    bt_light_special fog_left, fog_right, fog_dual;
    bt_light_song   *song;
    int32_t          nsongs;
} bt_lightshow;

/* Refuses a file whose "format" is not BT_LIGHTSHOW_FORMAT, with
 * BT_ERR_SCHEMA: a show built against a different contract would send the
 * wrong notes at the wrong bars, which is worse than sending none. */
bt_err bt_lightshow_load_file(const char *path, bt_lightshow **out, int *err_line);
bt_err bt_lightshow_load_mem (const char *text, size_t len,
                              bt_lightshow **out, int *err_line);
void   bt_lightshow_free(bt_lightshow *show);

/* Matched exactly on title, as the contract says. A song with no match has no
 * lighting, which is not an error - a set list may hold songs the show was
 * never built for. */
const bt_light_song *bt_lightshow_find(const bt_lightshow *show, const char *title);

/* Where an event lands, using the song's own tempo - the lighting side
 * assumes nothing about tempo and neither does this. */
bt_frame bt_light_event_frame(const bt_song *song, const bt_light_event *ev,
                              int32_t sample_rate);

/* Events strictly after `after` and up to and including `upto`, in order.
 * Half-open at the start so an event fires once however often this is
 * called. Returns how many indices were written. */
int32_t bt_light_events_between(const bt_light_song *ls, const bt_song *song,
                                bt_frame after, bt_frame upto,
                                int32_t sample_rate, int32_t *out, int32_t cap);

/* The last look at or before `at`, or -1. This is the whole of seeking: the
 * stage should look like whatever the most recent look says, and the hits
 * that were missed on the way stay missed. */
int32_t bt_light_look_at(const bt_light_song *ls, const bt_song *song,
                         bt_frame at, int32_t sample_rate);

#ifdef __cplusplus
}
#endif
#endif /* BT_LIGHTSHOW_H */
