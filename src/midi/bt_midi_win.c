/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * MIDI output through winmm. Windows only; the whole point of this library is
 * to be the one place that is.
 *
 * winmm needs no initialisation and no teardown beyond closing what you
 * opened, which is why there is no bt_midi_init to match bt_device_init.
 */
#include "backtrack/bt_midi.h"

#include <windows.h>
#include <mmsystem.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct bt_midi {
    HMIDIOUT h;
};

void bt_sleep_10ms(void) { Sleep(10); }

int32_t bt_midi_count(void) {
    return (int32_t)midiOutGetNumDevs();
}

bt_err bt_midi_get(int32_t index, bt_midi_info *out) {
    if (!out || index < 0 || index >= bt_midi_count()) return BT_ERR_RANGE;

    MIDIOUTCAPSA caps;
    if (midiOutGetDevCapsA((UINT_PTR)index, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
        return BT_ERR_IO;

    memset(out, 0, sizeof(*out));
    out->index = index;
    snprintf(out->name, sizeof(out->name), "%s", caps.szPname);
    return BT_OK;
}

int32_t bt_midi_find(const char *name_substr) {
    /* No substring means no match. Opening whatever happened to be first is
     * how a lighting cue ends up playing a piano note through the built-in
     * synth in front of an audience. */
    if (!name_substr || !*name_substr) return -1;

    const int32_t n = bt_midi_count();
    for (int32_t i = 0; i < n; i++) {
        bt_midi_info info;
        if (bt_midi_get(i, &info) != BT_OK) continue;
        if (strstr(info.name, name_substr)) return i;
    }
    return -1;
}

bt_err bt_midi_open(int32_t index, bt_midi **out) {
    if (!out) return BT_ERR_RANGE;
    *out = NULL;
    if (index < 0 || index >= bt_midi_count()) return BT_ERR_RANGE;

    bt_midi *m = (bt_midi *)calloc(1, sizeof(*m));
    if (!m) return BT_ERR_ALLOC;

    MMRESULT r = midiOutOpen(&m->h, (UINT)index, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) { free(m); return BT_ERR_IO; }

    *out = m;
    return BT_OK;
}

void bt_midi_close(bt_midi *m) {
    if (!m) return;
    /* Closes quietly. This used to panic and reset on the way out, which put
     * sixteen "all notes off" controllers on the wire every time the port was
     * closed - visible the moment a monitor was pointed at it.
     *
     * For a synth that is good manners. For a lighting desk it is noise that
     * someone may well have bound to something, and it is unnecessary:
     * bt_midi_trigger sends its note off immediately, so a cue never leaves
     * anything held. bt_midi_panic is still there for a caller who wants it. */
    midiOutClose(m->h);
    free(m);
}

bt_err bt_midi_send(bt_midi *m, uint8_t status, uint8_t d1, uint8_t d2) {
    if (!m) return BT_ERR_RANGE;
    /* A short message is the three bytes packed little-endian into a DWORD. */
    const DWORD msg = (DWORD)status | ((DWORD)(d1 & 0x7F) << 8)
                                    | ((DWORD)(d2 & 0x7F) << 16);
    return midiOutShortMsg(m->h, msg) == MMSYSERR_NOERROR ? BT_OK : BT_ERR_IO;
}

/* 1-16 as people write it, 0-15 as the wire wants it. */
static bt_err channel_status(int32_t channel, uint8_t base, uint8_t *out) {
    if (channel < 1 || channel > 16) return BT_ERR_RANGE;
    *out = (uint8_t)(base | (uint8_t)(channel - 1));
    return BT_OK;
}

bt_err bt_midi_note_on(bt_midi *m, int32_t channel, int32_t note, int32_t velocity) {
    if (note < 0 || note > 127 || velocity < 0 || velocity > 127) return BT_ERR_RANGE;
    uint8_t st;
    bt_err e = channel_status(channel, 0x90, &st);
    if (e != BT_OK) return e;
    return bt_midi_send(m, st, (uint8_t)note, (uint8_t)velocity);
}

bt_err bt_midi_note_off(bt_midi *m, int32_t channel, int32_t note) {
    if (note < 0 || note > 127) return BT_ERR_RANGE;
    uint8_t st;
    bt_err e = channel_status(channel, 0x80, &st);
    if (e != BT_OK) return e;
    return bt_midi_send(m, st, (uint8_t)note, 0);
}

bt_err bt_midi_trigger(bt_midi *m, int32_t channel, int32_t note, int32_t velocity) {
    bt_err e = bt_midi_note_on(m, channel, note, velocity);
    if (e != BT_OK) return e;
    return bt_midi_note_off(m, channel, note);
}

bt_err bt_midi_panic(bt_midi *m) {
    if (!m) return BT_ERR_RANGE;
    for (int32_t ch = 1; ch <= 16; ch++) {
        uint8_t st;
        if (channel_status(ch, 0xB0, &st) != BT_OK) continue;
        /* CC 123: all notes off. */
        bt_err e = bt_midi_send(m, st, 123, 0);
        if (e != BT_OK) return e;
    }
    return BT_OK;
}

/* ------------------------------------------------------------------ input */

#define BT_MIDI_IN_QUEUE 256

struct bt_midi_in {
    HMIDIIN      h;
    CRITICAL_SECTION lock;
    bt_midi_msg  q[BT_MIDI_IN_QUEUE];
    int32_t      head, tail;       /* head == tail means empty */
    DWORD        opened_ms;
};

int32_t bt_midi_in_count(void) { return (int32_t)midiInGetNumDevs(); }

bt_err bt_midi_in_get(int32_t index, bt_midi_info *out) {
    if (!out || index < 0 || index >= bt_midi_in_count()) return BT_ERR_RANGE;
    MIDIINCAPSA caps;
    if (midiInGetDevCapsA((UINT_PTR)index, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
        return BT_ERR_IO;
    memset(out, 0, sizeof(*out));
    out->index = index;
    snprintf(out->name, sizeof(out->name), "%s", caps.szPname);
    return BT_OK;
}

int32_t bt_midi_in_find(const char *name_substr) {
    if (!name_substr || !*name_substr) return -1;
    const int32_t n = bt_midi_in_count();
    for (int32_t i = 0; i < n; i++) {
        bt_midi_info info;
        if (bt_midi_in_get(i, &info) != BT_OK) continue;
        if (strstr(info.name, name_substr)) return i;
    }
    return -1;
}

/* Called by winmm on its own thread. Does no allocation and takes one lock
 * that is never held for more than a few stores. */
static void CALLBACK in_cb(HMIDIIN h, UINT msg, DWORD_PTR user,
                           DWORD_PTR p1, DWORD_PTR p2) {
    (void)h; (void)p2;
    if (msg != MIM_DATA) return;

    bt_midi_in *m = (bt_midi_in *)user;
    const DWORD packed = (DWORD)p1;

    EnterCriticalSection(&m->lock);
    const int32_t next = (m->tail + 1) % BT_MIDI_IN_QUEUE;
    if (next == m->head) {
        /* Full: drop the oldest. Blocking the MIDI callback to keep a
         * monitor's history complete would be the wrong trade. */
        m->head = (m->head + 1) % BT_MIDI_IN_QUEUE;
    }
    m->q[m->tail].status = (uint8_t)(packed & 0xFF);
    m->q[m->tail].d1     = (uint8_t)((packed >> 8) & 0x7F);
    m->q[m->tail].d2     = (uint8_t)((packed >> 16) & 0x7F);
    m->q[m->tail].ms     = (uint32_t)(GetTickCount() - m->opened_ms);
    m->tail = next;
    LeaveCriticalSection(&m->lock);
}

bt_err bt_midi_in_open(int32_t index, bt_midi_in **out) {
    if (!out) return BT_ERR_RANGE;
    *out = NULL;
    if (index < 0 || index >= bt_midi_in_count()) return BT_ERR_RANGE;

    bt_midi_in *m = (bt_midi_in *)calloc(1, sizeof(*m));
    if (!m) return BT_ERR_ALLOC;
    InitializeCriticalSection(&m->lock);
    m->opened_ms = GetTickCount();

    MMRESULT r = midiInOpen(&m->h, (UINT)index, (DWORD_PTR)in_cb,
                            (DWORD_PTR)m, CALLBACK_FUNCTION);
    if (r != MMSYSERR_NOERROR) {
        DeleteCriticalSection(&m->lock);
        free(m);
        return BT_ERR_IO;
    }
    midiInStart(m->h);
    *out = m;
    return BT_OK;
}

void bt_midi_in_close(bt_midi_in *m) {
    if (!m) return;
    midiInStop(m->h);
    midiInReset(m->h);
    midiInClose(m->h);
    DeleteCriticalSection(&m->lock);
    free(m);
}

bt_err bt_midi_in_poll(bt_midi_in *m, bt_midi_msg *out) {
    if (!m || !out) return BT_ERR_RANGE;
    bt_err e = BT_ERR_NOT_FOUND;
    EnterCriticalSection(&m->lock);
    if (m->head != m->tail) {
        *out = m->q[m->head];
        m->head = (m->head + 1) % BT_MIDI_IN_QUEUE;
        e = BT_OK;
    }
    LeaveCriticalSection(&m->lock);
    return e;
}

const char *bt_midi_describe(const bt_midi_msg *m, char *buf, size_t cap) {
    if (!m || !buf || cap == 0) return "";
    const int ch = (m->status & 0x0F) + 1;
    switch (m->status & 0xF0) {
    case 0x80: snprintf(buf, cap, "note off  ch%-2d note %3d", ch, m->d1); break;
    case 0x90: snprintf(buf, cap, m->d2 ? "note on   ch%-2d note %3d vel %d"
                                        : "note on   ch%-2d note %3d vel %d (= off)",
                        ch, m->d1, m->d2); break;
    case 0xB0: snprintf(buf, cap, "control   ch%-2d cc %3d val %d", ch, m->d1, m->d2); break;
    case 0xC0: snprintf(buf, cap, "program   ch%-2d program %d", ch, m->d1); break;
    default:   snprintf(buf, cap, "status 0x%02X  %d %d", m->status, m->d1, m->d2); break;
    }
    return buf;
}
