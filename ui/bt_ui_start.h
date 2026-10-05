/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The screen you get with nothing open, and the settings that stop you
 * needing it twice.
 *
 * Until now btui had to be started with --setlist and --device, so a
 * double-clicked shortcut opened an unsaveable demo and there was no way from
 * inside the program to open anything real. An application that cannot open
 * its own documents is a developer's tool wearing a UI.
 */
#ifndef BT_UI_START_H
#define BT_UI_START_H

#include "backtrack/bt_model.h"

#define BT_RECENT_MAX 6

struct bt_ui_settings {
    char setlist[BT_MAX_PATH]  = {0};
    char device[BT_MAX_PATH]   = {0};
    char recent[BT_RECENT_MAX][BT_MAX_PATH] = {{0}};
    int32_t nrecent = 0;
};

/* %APPDATA%\BackingTrackLive\settings.json, or the equivalent. Failures are
 * silent on purpose: not remembering the last set list is a small loss, and
 * refusing to start over it would be a large one. */
void bt_ui_settings_load(bt_ui_settings &s);
void bt_ui_settings_save(const bt_ui_settings &s);
void bt_ui_settings_touch(bt_ui_settings &s, const char *setlist, const char *device);

/* What the start screen is asking the host to do. */
enum class bt_start_action { none, open, create_new, create_demo, open_recent,
                            open_found, quit };

/* A set list found in the set lists folder: its name and where it lives. */
struct bt_ui_found {
    char name[BT_MAX_NAME];
    char path[BT_MAX_PATH];     /* ...the setlist.json inside it */
};
#define BT_FOUND_MAX 64

/* Scans the default set lists folder for subfolders holding a setlist.json.
 * Returns how many were written. */
int32_t bt_ui_scan_setlists(bt_ui_found *out, int32_t cap);

struct bt_ui_start {
    bt_ui_settings *settings = nullptr;
    bt_start_action action   = bt_start_action::none;
    char            path[BT_MAX_PATH] = {0};   /* chosen set list        */
    char            device[BT_MAX_PATH] = {0}; /* chosen device.json     */
    char            status[200] = {0};
    int32_t         recent_index = -1;

    /* Naming a new set list, before anything is written. */
    bool            naming = false;
    bool            focus_name = false;   /* take focus once, not every frame */
    char            new_name[BT_MAX_NAME] = {0};

    bt_ui_found     found[BT_FOUND_MAX];
    int32_t         nfound = 0;
    bool            rescan = true;        /* set when the folder may have changed */
    int32_t         found_index = -1;
    int32_t         clone_from = -1;   /* naming a clone of this one */
};

void bt_ui_start_draw(bt_ui_start &st);

/* Native file and folder pickers. Windows-only, like the rest of the UI. */
bool bt_ui_pick_setlist(char *out, size_t cap);
bool bt_ui_pick_folder(const char *title, char *out, size_t cap);
bool bt_ui_pick_save_wav(char *out, size_t cap);

/* Routing is a property of the machine and its interface, not of a set list -
 * the same four outputs serve every set you own. So device.json lives beside
 * the settings, and a set list folder only overrides it when it carries one
 * of its own (which is also what every version before this wrote).
 *
 * Returns false only if the settings folder itself cannot be found. */
bool bt_ui_machine_device_path(char *out, size_t cap);

/* Documents\BackingTrackLive, created on demand.
 *
 * Set lists are the user's own content and they are bulky - a forty-song set
 * is gigabytes of stems. That rules out %APPDATA%, which is hidden, is meant
 * for small machine-local state, and in its roaming form may try to sync.
 * Documents is visible in Explorer, which matters because the whole point of
 * the folder is that you can copy it to the backup laptop, and it is what
 * Windows' own backup covers.
 *
 * This is only where the pickers start. Nothing stops a set list living on a
 * USB stick, and the band's probably should. */
bool bt_ui_default_setlist_root(char *out, size_t cap);

/* Writes a set list with one empty song to <dir>/setlist.json. */
bt_err bt_ui_new_setlist(const char *dir, const char *name);

/* Copy a whole set list folder to a new one: the set list file, the device
 * file if it has one, and every stem.
 *
 * Cloning is how a second set gets built - most of a working set carries over,
 * already aligned, and what changes is a handful of songs. Doing that by
 * re-adding and re-aligning forty stems is the work this avoids.
 *
 * Stems are hard-linked where the filesystem allows it, which it does for two
 * folders side by side in Documents, so a clone is instant and costs no disk.
 * Nothing ever writes to a stem - alignment lives in the set list file - so
 * the sharing cannot be noticed. Falls back to copying when linking fails,
 * which is what happens across volumes. */
bt_err bt_ui_clone_setlist(const char *src_dir, const char *dst_dir);

#endif /* BT_UI_START_H */
