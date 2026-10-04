/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Edit mode: building the set list, not performing it.
 *
 * Two deliberate differences from the play screens. First, this is drawn with
 * real ImGui widgets rather than a hand-composed draw list - editing needs
 * text fields, steppers and focus behaviour, and reimplementing those would
 * be a waste. Second, it is only reachable while stopped: `E` does nothing at
 * all during a song, because the one thing worse than a fiddly editor is one
 * that can appear over the top of a performance.
 *
 * Edits mutate the set list in memory and set a dirty flag. Nothing touches
 * disk until a save, and a save goes through bt_setlist_save_file, which is
 * already lossless and byte-stable - so saving a set list you did not change
 * produces no diff at all.
 */
#ifndef BT_UI_EDIT_H
#define BT_UI_EDIT_H

#include "backtrack/bt_model.h"
#include "backtrack/bt_validate.h"
#include "backtrack/bt_peaks.h"

enum class bt_edit_screen { setlist, song, align, audio, check };

struct bt_ui_edit {
    bt_setlist    *sl = nullptr;          /* mutable, unlike the play view */
    bt_device_cfg *dev = nullptr;         /* for the list of bus names     */
    char           path[BT_MAX_PATH] = {0};
    bool           can_save = false;      /* false for the built-in demo   */

    bt_edit_screen screen = bt_edit_screen::setlist;
    int32_t        song  = 0;
    int32_t        track = -1;
    bool           dirty = false;

    /* Set when the user asks to hear something; the host clears it. */
    bool           want_audition = false;
    int32_t        audition_bar  = 1;

    char           status[160] = {0};      /* last save result, or an error */
    bool           leave = false;          /* set by ESC off the top screen  */

    /* Audio routing. device.json is machine-local, so the editor writes it
     * beside the set list and the host re-opens the stream afterwards. */
    char           device_path[BT_MAX_PATH] = {0};
    bool           can_save_device = false;
    bool           device_dirty    = false;
    bool           reopen_device   = false;  /* host clears after acting    */
    bool           show_all_apis   = false;
    int32_t        picked_device   = -1;     /* backend index, -1 = none    */

    /* Validation, the btcheck report in a screen. Held rather than recomputed
     * per frame: it decodes every stem, which is seconds of work. */
    bt_issue        *issues = nullptr;
    size_t           nissues = 0;
    bt_setlist_stats stats{};
    bool             checked = false;

    /* Export. The host does the rendering; this only asks. */
    /* Audition. The host owns the player; the editor only asks.
     *
     * Checking alignment used to mean leaving edit mode, listening, coming
     * back, nudging, and leaving again. The nudge and the ear belong on the
     * same screen. */
    bool             want_play   = false;   /* start from the top          */
    bool             want_stop   = false;
    bool             want_reapply = false;  /* an edit the engine must see */
    bool             playing     = false;   /* host fills this in          */
    double           play_sec    = 0.0;
    double           song_sec    = 0.0;      /* host fills in; for the scrub */
    bool             want_seek   = false;
    double           seek_sec    = 0.0;
    bool             want_open_setlist = false;
    uint64_t         last_sig = 0;          /* see render_signature */

    /* Align view. The envelope is cached per track and rebuilt when the
     * track or its PCM changes; building it scans the whole stem, which is
     * not something to do at sixty frames a second. */
    bt_peaks         peaks{};
    int32_t          peaks_song  = -1;
    int32_t          peaks_track = -1;
    const void      *peaks_src   = nullptr;   /* the pcm it was built from */

    double           view_start  = 0.0;       /* seconds, song time        */
    double           view_len    = 8.0;
    bool             want_select = false;     /* host: make this song live */
    bt_frame         first_sound = -1;        /* cached with the peaks     */

    bool             want_export = false;
    bool             export_whole_set = false;
    char             export_path[BT_MAX_PATH] = {0};
};

/* Returns true while edit mode should stay open; false when the user leaves. */
bool bt_ui_edit_draw(bt_ui_edit &ed);

/* Applied by the host so edit mode does not own the keyboard map. */
void bt_ui_edit_key(bt_ui_edit &ed, int vk);

#endif /* BT_UI_EDIT_H */
