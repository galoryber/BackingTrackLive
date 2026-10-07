/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#ifndef BT_MODEL_H
#define BT_MODEL_H

#include "bt_types.h"
#include "bt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Tempo
 *
 * A song's tempo is stored as a *map* from the outset even though the UI will
 * initially expose a single BPM. Adding the song with a tempo change in the
 * bridge must not require a file-format migration.
 *
 * Beat 0 is the first downbeat and sits at `downbeat_frames` into the audio.
 * Downloaded stems routinely start with silence or a pickup bar, so this
 * offset is what actually makes the click line up.
 * ------------------------------------------------------------------------ */
typedef struct {
    int64_t start_beat;   /* this tempo takes effect at this beat index */
    double  bpm;
} bt_tempo_seg;

typedef struct {
    bt_tempo_seg seg[BT_MAX_TEMPO_SEG];
    int32_t      nseg;
    int32_t      sig_num, sig_den;
    double       downbeat_ms;    /* where beat 0 lands in the audio */
} bt_tempo_map;

/* Frame at which `beat` occurs. Computed from the beat index, never
 * accumulated, so a 3-hour set does not drift. Beats may be negative
 * (count-in). Sample rate is passed in rather than stored: the model is
 * portable between machines, the device rate is not. */
bt_frame bt_tempo_beat_frame(const bt_tempo_map *tm, int64_t beat, int32_t sample_rate);

/* Inverse: the highest beat index whose frame is <= `f`. */
int64_t  bt_tempo_frame_beat(const bt_tempo_map *tm, bt_frame f, int32_t sample_rate);

/* True when `beat` is the first beat of a bar. */
bool     bt_tempo_is_downbeat(const bt_tempo_map *tm, int64_t beat);

/* ---------------------------------------------------------------------------
 * Tracks and songs
 * ------------------------------------------------------------------------ */
typedef enum {
    BT_TRACK_AUDIO = 0,   /* a decoded stem                                */
    BT_TRACK_CLICK        /* synthesised from the tempo map                */
} bt_track_type;

typedef struct {
    char          name[BT_MAX_NAME];
    bt_track_type type;
    char          bus[BT_MAX_NAME];   /* LOGICAL bus name, e.g. "inear"    */
    char          file[BT_MAX_PATH];  /* relative to the set list file     */
    double        gain_db;
    int32_t       offset_ms;          /* nudge; may be negative            */
    bool          muted;

    /* Populated by the loader, not by the file. */
    float       **pcm;                /* planar, [channels][frames]        */
    int32_t       channels;
    bt_frame      frames;
} bt_track;

typedef enum {
    BT_ON_END_STOP = 0,   /* default: wait for a human                     */
    BT_ON_END_NEXT        /* advance and play the next song                */
} bt_on_end;

/* A lighting cue: a bar, and the MIDI note sent when the song reaches it.
 *
 * The note is usually the set list's "next cue" note, because a QLC+ cue list
 * advances a step at a time and the lighting design lives in QLC+ rather than
 * here. A cue may override it - the end of a song is a different note - and
 * zero means "use the set list's default", which is what almost every cue
 * says.
 *
 * Cues fire from the UI thread, never from the audio callback: sending MIDI
 * is a syscall. A frame of jitter is around 16 ms, and DMX refreshes at about
 * 44 Hz, so nothing downstream can tell. */
typedef struct {
    int32_t bar;                  /* 1-based, as everybody counts bars */
    int32_t note;                 /* 0 = the set list's next-cue note  */
    char    desc[BT_MAX_CUE];     /* for the person editing, not sent  */
} bt_light_cue;

typedef struct {
    char         title[BT_MAX_NAME];
    char         artist[BT_MAX_NAME];
    bt_tempo_map tempo;
    /* How the guitars are tuned for this song, in whatever notation the band
     * uses - "E1" for standard a half step down, "D1" for drop D then a half
     * step, and so on. Free text and displayed verbatim: it is a note to a
     * human on stage, and a format that enforced one band's shorthand would
     * be wrong for the next one and for this one when the shorthand changes.
     * Empty means nothing is shown. */
    char         tuning[BT_MAX_TUNING];
    /* Anything worth seeing while the song plays, in your own words. Shown
     * under the title on the playing screen and nowhere else. Empty shows
     * nothing. */
    char         cue[BT_MAX_CUE];

    int32_t      count_in_bars;
    /* How long the song runs, in bars, when the audio does not say.
     * A song with no stems - just a click to play along to - has no length
     * of its own otherwise, and would count in and stop immediately. Zero
     * means "as long as the longest stem", which is the normal case. */
    int32_t      length_bars;
    bt_on_end    on_end;
    bt_track     track[BT_MAX_TRACKS];
    int32_t      ntracks;

    /* MIDI program change sent when this song starts, so the lighting desk
     * loads this song's cue list before the count-in. -1 sends nothing.
     *
     * Sent on starting rather than on selecting: browsing the set with the
     * arrow keys would otherwise fire a program change per keypress. */
    int32_t      midi_program;

    bt_light_cue light_cue[BT_MAX_LIGHT_CUES];
    int32_t      nlight_cues;
} bt_song;

/* What the lighting desk is listening for, for this show.
 *
 * In the set list rather than in device.json, because it is show design and
 * not machine configuration: a set list carried to the backup laptop should
 * drive the lights the same way. Which MIDI *port* to send down is the
 * machine's business and lives in device.json, as the audio interface does. */
typedef struct {
    int32_t channel;      /* 1-16; 0 means "no lighting", the default      */
    int32_t next_note;    /* advance the cue list - QLC+ "Next Cue"        */
    int32_t end_note;     /* the song is over - the between-songs look     */
    int32_t velocity;     /* 1-127; most desks ignore it                   */

    /* Step the cue list *backwards*. Optional - 0 means the desk has no such
     * binding, and a rewind is done by reloading the song and replaying.
     *
     * Worth having. Scrubbing back four bars while aligning a stem should
     * send four of these, not reload the cue list and race forward through
     * every look in the song to catch up. */
    int32_t prev_note;

    /* An instant blackout, for a song that ends on the chord rather than
     * fading to the between-songs look. 0 means the desk has no such
     * binding. A cue selects it by naming this note. */
    int32_t blackout_note;
} bt_light_cfg;

typedef struct {
    char     name[BT_MAX_NAME];
    char     dir[BT_MAX_PATH];   /* directory of the set list file         */
    bt_song *song;
    int32_t  nsongs;
    int32_t  cap;
    bt_light_cfg light;
} bt_setlist;

/* ---------------------------------------------------------------------------
 * Buses  (machine-local; loaded from device.json, never from setlist.json)
 * ------------------------------------------------------------------------ */
typedef struct {
    char    name[BT_MAX_NAME];
    int32_t ch[BT_MAX_BUS_CH];   /* physical output channel indices        */
    int32_t nch;                 /* 1 = mono, 2 = stereo                   */
} bt_bus;

typedef struct {
    bt_bus  bus[BT_MAX_BUSES];
    int32_t nbuses;
    int32_t sample_rate;
    int32_t buffer_frames;
    char    device[BT_MAX_NAME];
    /* Host API to prefer: "ASIO", "WASAPI", "WDM-KS", "DirectSound", "MME",
     * "Core Audio", "ALSA", "JACK". Matched as a case-insensitive substring.
     *
     * This matters more than it looks. On Windows the same speakers appear
     * under four APIs with wildly different latency - measured on one machine:
     * WASAPI 2.7 ms, WDM-KS 10 ms, DirectSound 120 ms, MME 90 ms - and
     * PortAudio's "default device" is the MME one. Leaving this empty picks
     * the best API present rather than the default. */
    char    api[BT_MAX_NAME];

    /* Which MIDI output the lighting desk is listening on, matched as a
     * substring of the port name - a loopMIDI port appears under whatever it
     * was named there.
     *
     * Machine-local, like the audio interface and for the same reason: the
     * backup laptop has its own ports. What is *sent* down it is show design
     * and lives in the set list. Empty means no lighting from this machine,
     * which is the default. */
    char    midi_out[BT_MAX_NAME];
} bt_device_cfg;

/* ---------------------------------------------------------------------------
 * Loading
 * ------------------------------------------------------------------------ */
bt_err bt_setlist_load_file(const char *path, bt_setlist **out, int *err_line);
bt_err bt_setlist_load_mem (const char *text, size_t len, const char *dir,
                            bt_setlist **out, int *err_line);
void   bt_setlist_free(bt_setlist *sl);

bt_err bt_device_cfg_load_file(const char *path, bt_device_cfg *out, int *err_line);
bt_err bt_device_cfg_load_mem (const char *text, size_t len, bt_device_cfg *out, int *err_line);
void   bt_device_cfg_defaults(bt_device_cfg *cfg);

/* ---------------------------------------------------------------------------
 * Saving
 *
 * Serialisation is exact: numbers are emitted at the shortest precision that
 * parses back to the identical value, so load -> save -> load is lossless and
 * saving twice produces byte-identical output. A set list is a text file the
 * user may also edit by hand and keep in git; gratuitous churn in a diff is a
 * defect.
 *
 * The *_to_json forms allocate; the caller frees. The *_save_file forms write
 * to a temporary alongside the target and rename over it, so an interrupted
 * save cannot leave a half-written set list where a working one used to be.
 * ------------------------------------------------------------------------ */
bt_err bt_setlist_to_json  (const bt_setlist *sl, char **out, size_t *len);
bt_err bt_setlist_save_file(const bt_setlist *sl, const char *path);

bt_err bt_device_cfg_to_json  (const bt_device_cfg *cfg, char **out, size_t *len);
bt_err bt_device_cfg_save_file(const bt_device_cfg *cfg, const char *path);

const bt_bus *bt_device_find_bus(const bt_device_cfg *cfg, const char *name);

/* Decode every audio stem of `song` into RAM. Off the RT thread. */
bt_err bt_song_load_audio(bt_song *song, const char *dir, int32_t sample_rate);
void   bt_song_free_audio(bt_song *song);

/* Total frames of the song: the longest (stem length + its offset), plus the
 * count-in. Zero when the song has no audio. */
bt_frame bt_song_length(const bt_song *song, int32_t sample_rate);

/* Move a track within a song, rotating the ones between rather than swapping,
 * so the others keep their relative order.
 *
 * Order has no effect on what is heard - the mixer sums the tracks and
 * addition does not care - so this is purely how the song reads to the person
 * editing it. Which is reason enough: a forty-song set is easier to work on
 * when the click is where you expect it and the stems are in the order you
 * think about them.
 *
 * Moves the loader's pcm pointers with their tracks, which is the part worth
 * getting right. */
/* How long the song should fade for at its end, in frames, or 0 for no fade.
 *
 * Non-zero only when a declared length cuts the stems short: ending a song at
 * bar 114 means the audio is still playing there, and stopping a stem
 * mid-note is a click. A song that ends where its stems end needs nothing. */
/* The shortest text that parses back to exactly this double, into `out`.
 *
 * This is how numbers are written to a set list, and it is the only honest way
 * to show one: a tempo of 96.515 displayed as 96.52 is a different tempo, and
 * by beat 1000 it is most of a beat out. Screens that round deliberately -
 * the set list, the playing screen - do their own thing; the editor should
 * show what the file says. */
void bt_format_number(double v, char *out, size_t cap);

bt_frame bt_song_fade_frames(const bt_song *song, int32_t sample_rate);

/* ---------------------------------------------------------------------------
 * Lighting cues
 *
 * Firing is a question about two playhead positions: which cues lie in
 * (after, upto]? Half-open at the start so a cue fires exactly once however
 * often the UI ticks, and inclusive at the end so a cue landing on this
 * frame is not held over to the next.
 * ------------------------------------------------------------------------ */

/* Frame at which a cue's bar begins. Bars are 1-based, so bar 1 is beat 0. */
bt_frame bt_light_cue_frame(const bt_song *song, const bt_light_cue *cue,
                            int32_t sample_rate);

/* Indices of the cues in (after, upto], in order, into `out`.
 *
 * Returns how many were written, up to `cap`. `after` of less than the song
 * start catches a cue on bar 1: the playhead is negative through the
 * count-in, so the first tick after starting spans from there. */
int32_t bt_song_cues_between(const bt_song *song, bt_frame after, bt_frame upto,
                             int32_t sample_rate, int32_t *out, int32_t cap);

/* How many cues lie at or before `at`.
 *
 * This is what makes jumping around safe: a QLC+ cue list has no notion of
 * position, so after a jump the desk is told to reload the song - which puts
 * it back at step zero - and then advanced this many times. */
int32_t bt_song_cues_before(const bt_song *song, bt_frame at, int32_t sample_rate);

/* How many of those cues actually step the cue list: the ones that send the
 * "next" note, not the ones that name their own.
 *
 * This is the number that says where the desk's cue list is standing, and it
 * is not the same as the number of cues passed. An end-of-song cue sends the
 * between-songs look or a blackout; the desk does that and stays where it
 * was. Counting it would leave everything after it one step out. */
int32_t bt_song_steps_before(const bt_song *song, const bt_light_cfg *cfg,
                             bt_frame at, int32_t sample_rate);

/* The note a cue actually sends, resolving 0 to the set list's default. */
int32_t bt_light_cue_note(const bt_light_cfg *cfg, const bt_light_cue *cue);

bt_err bt_song_move_track(bt_song *song, int32_t from, int32_t to);

/* Bytes of PCM currently resident for this song. Surfaced in the UI so nobody
 * is surprised by the preload budget at soundcheck. */
size_t   bt_song_pcm_bytes(const bt_song *song);

#ifdef __cplusplus
}
#endif
#endif /* BT_MODEL_H */
