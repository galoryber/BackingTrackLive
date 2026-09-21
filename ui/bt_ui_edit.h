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

enum class bt_edit_screen { setlist, song, align };

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
};

/* Returns true while edit mode should stay open; false when the user leaves. */
bool bt_ui_edit_draw(bt_ui_edit &ed);

/* Applied by the host so edit mode does not own the keyboard map. */
void bt_ui_edit_key(bt_ui_edit &ed, int vk);

#endif /* BT_UI_EDIT_H */
