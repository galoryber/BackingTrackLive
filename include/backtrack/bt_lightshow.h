/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The lighting show: a list of notes to send at bar positions, produced by
 * the lighting side and read here.
 *
 * This file is not ours. QLC+ and its generator own what every look is, which
 * bar each section starts on, and which note number means which look; all we
 * do is send the note when the playhead reaches the bar. The contract is
 * `random-riot-lighting/2` - version 1 is still read - and is written
 * down in docs/lighting.md.
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

/* What we write, and what we accept.
 *
 * Version 2 adds one thing: a cue may sit at bar 0 or below, inside the
 * count-in, which version 1 reserved for "nothing is sent". Both are read,
 * because the band's working cue file says version 1 and refusing it would
 * take the rig down at the next launch for a feature nobody had used yet.
 *
 * Which one is *written* depends on the content: a show with no count-in cue
 * is still a valid version 1 show and is written as one, so the lighting
 * side's generator keeps reading it untouched. The tag moves to version 2
 * only for a file that actually needs version 2 to be understood - at which
 * point an old reader refusing it is the correct outcome, because the
 * alternative is silently losing the cue. */
/* How far before the first downbeat a cue may sit. A count-in is a bar or
 * two, so this is only a guard against a corrupt number, not a musical
 * limit: bar 0 is the last bar of the count-in, bar -1 the one before it. */
#define BT_LIGHT_MIN_BAR         (-64)

#define BT_LIGHTSHOW_FORMAT      "random-riot-lighting/2"
#define BT_LIGHTSHOW_FORMAT_V1   "random-riot-lighting/1"

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

/* Gives `to` a copy of every cue `from` has, which is what copying a song in
 * the set list means for its lighting.
 *
 * Cues are matched to a song by title, so a copied song - which must take a
 * new title, or two songs on stage are indistinguishable - starts with no
 * lighting at all unless its cues are copied across with it. That is a
 * silent loss: the copy looks finished and goes dark.
 *
 * Makes `to`'s cues match `from`'s, creating its entry if the show has never
 * heard of it. A `from` with no cues leaves a `to` that does not exist alone
 * rather than writing an empty entry into a file the lighting side also
 * reads: copying a song that has no lighting should leave no trace. */
bt_err bt_lightshow_copy_song(bt_lightshow *show, const char *from,
                              const char *to);

/* Carries a song's cues across to a new title, which is what renaming a song
 * in the set list means for its lighting.
 *
 * Cues are matched to a song by title and nothing else - §3 of the contract -
 * so without this, typing a new title into the editor silently unhooks the
 * song from every cue it had. That is the whole bug: the cues are still in
 * the file, under a name no song has any more.
 *
 * Renaming the entry rather than giving songs an id is deliberate. The cue
 * file is written by the lighting side as well, from song titles; an id
 * would have to be agreed, generated and kept in step over there too, and a
 * file with two keys has two ways to disagree with the set list instead of
 * one.
 *
 * A `from` with no entry is not an error - a song with no lighting renames
 * like any other. BT_ERR_EXISTS means `to` already has cues of its own: they
 * are left alone, which leaves the song matched to them.
 */
bt_err bt_lightshow_rename_song(bt_lightshow *show, const char *from,
                                const char *to);

/* Puts one song's cues in bar order, which is the order §4 of the contract
 * says a cue file is in. Adding a cue appends it, so this is what keeps that
 * true after an edit; it is also the whole of "the table is sorted", since
 * the table draws this array.
 *
 * Returns where the cue that was at `track` ended up, or -1 if `track` was
 * -1, so a caller can keep following the row somebody is editing. Stable:
 * cues sharing a bar stay in the order they were in, because two looks on
 * the same downbeat may well have to be sent in a particular order. */
int32_t bt_lightshow_sort_song(bt_lightshow *show, const char *title,
                               int32_t track);

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

/* Which bar a frame falls in - the inverse of bt_light_event_frame, and what
 * "put a cue where the playhead is" needs.
 *
 * The bar the frame is *inside*, rounded down, not the nearest one. Somebody
 * tapping a cue in presses the key having heard the section change, a couple
 * of hundred milliseconds after the downbeat, so the bar containing the press
 * is the bar they meant.
 *
 * Rounding down has to mean down through zero as well: C division truncates
 * towards zero, which would map the whole count-in onto bar 1 and put every
 * count-in tap on the downbeat. */
int32_t bt_light_bar_at(const bt_song *song, bt_frame at, int32_t sample_rate);

/* Events strictly after `after` and up to and including `upto`, in order.
 * Half-open at the start so an event fires once however often this is
 * called. Returns how many indices were written. */
int32_t bt_light_events_between(const bt_light_song *ls, const bt_song *song,
                                bt_frame after, bt_frame upto,
                                int32_t sample_rate, int32_t *out, int32_t cap);

/* The next event due strictly after `at`, or -1 when the song has none left.
 * The earliest one, not the first in the array, so an out-of-order file still
 * answers correctly. */
int32_t bt_light_next_at(const bt_light_song *ls, const bt_song *song,
                         bt_frame at, int32_t sample_rate);

/* The last look at or before `at`, or -1. This is the whole of seeking: the
 * stage should look like whatever the most recent look says, and the hits
 * that were missed on the way stay missed. */
int32_t bt_light_look_at(const bt_light_song *ls, const bt_song *song,
                         bt_frame at, int32_t sample_rate);

#ifdef __cplusplus
}
#endif
#endif /* BT_LIGHTSHOW_H */
