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
enum class bt_start_action { none, open, create_demo, open_recent, quit };

struct bt_ui_start {
    bt_ui_settings *settings = nullptr;
    bt_start_action action   = bt_start_action::none;
    char            path[BT_MAX_PATH] = {0};   /* chosen set list        */
    char            device[BT_MAX_PATH] = {0}; /* chosen device.json     */
    char            status[200] = {0};
    int32_t         recent_index = -1;
};

void bt_ui_start_draw(bt_ui_start &st);

/* Native file and folder pickers. Windows-only, like the rest of the UI. */
bool bt_ui_pick_setlist(char *out, size_t cap);
bool bt_ui_pick_folder(const char *title, char *out, size_t cap);
bool bt_ui_pick_save_wav(char *out, size_t cap);

#endif /* BT_UI_START_H */
