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

/* bt_light_kind and bt_light_event come from bt_model.h. A cue typed into a
 * song and a cue generated into this file are the same thing, which is what
 * keeps there being one piece of code that fires them. */

typedef struct {
    char            title[BT_MAX_NAME];  /* matched against the set list */
    bt_light_event *ev;
    int32_t         nev;
} bt_light_song;

/* A note that is not tied to a bar - something a person presses.
 *
 * Kept by name rather than as a fixed set of fields, because this file is
 * written back out and a special this program has never heard of must
 * survive that. The two it knows about by name are "between_songs" and
 * "stop_all"; the rest are carried. */
typedef struct {
    char    name[BT_MAX_NAME];
    int32_t ch;
    int32_t note;
} bt_light_special;

typedef struct {
    char             port[BT_MAX_NAME];   /* substring for bt_midi_find    */
    int32_t          velocity;
    bt_light_special *special;
    int32_t          nspecials;
    bt_light_song   *song;
    int32_t          nsongs;
} bt_lightshow;

/* The two a person can press, looked up by the names the contract gives
 * them. Returns NULL when the file does not define one. */
const bt_light_special *bt_lightshow_special(const bt_lightshow *show,
                                             const char *name);

/* Refuses a file whose "format" is not BT_LIGHTSHOW_FORMAT, with
 * BT_ERR_SCHEMA: a show built against a different contract would send the
 * wrong notes at the wrong bars, which is worse than sending none. */
bt_err bt_lightshow_load_file(const char *path, bt_lightshow **out, int *err_line);
bt_err bt_lightshow_load_mem (const char *text, size_t len,
                              bt_lightshow **out, int *err_line);
void   bt_lightshow_free(bt_lightshow *show);

/* Writes the show back out, faithfully enough to be the file's only editor.
 *
 * Cues live in this file and nowhere else, so changing a bar means changing
 * this. Written whole rather than patched, which is safe because the format
 * tag is versioned: anything this program does not model would have come
 * with a new version, and a new version is refused at load rather than
 * loaded and silently stripped.
 *
 * Saved through a temporary file and renamed, so an interrupted save cannot
 * leave a half-written show where a working one was.
 */
/* Adds one cue to a song, creating the song's entry if the show has never
 * heard of it - which is how a set list that started with no cue file gets
 * one. `after` may be NULL, in which case the cue is placed at the end and
 * takes its channel and note from the cue before it; consecutive looks on a
 * desk are usually neighbours.
 *
 * Returns a pointer to the new cue through `out` if it is wanted. The song's
 * event array may be reallocated, so any pointer into it from before this
 * call is stale. */
bt_err bt_lightshow_add_cue(bt_lightshow *show, const char *title,
                            bt_light_event **out);

/* Removes one. Indices after it shift down. */
bt_err bt_lightshow_remove_cue(bt_lightshow *show, const char *title, int32_t at);

bt_err bt_lightshow_save_file(const bt_lightshow *show, const char *path);
bt_err bt_lightshow_to_json  (const bt_lightshow *show, char **out, size_t *len);

/* Matched exactly on title, as the contract says. A song with no match has no
 * lighting, which is not an error - a set list may hold songs the show was
 * never built for. */
const bt_light_song *bt_lightshow_find(const bt_lightshow *show, const char *title);

/* The same, for editing. Changing a bar here and saving is how a cue moves;
 * editing is a first-class use of this file now, not a borrowed read. */
bt_light_song *bt_lightshow_song(bt_lightshow *show, const char *title);

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
