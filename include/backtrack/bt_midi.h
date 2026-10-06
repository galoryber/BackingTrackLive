/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * MIDI output.
 *
 * A SEPARATE library from libbacktrack, for the same reason the audio device
 * layer is: the engine has no platform dependency, which is what lets every
 * question about timing be answered offline on any machine. Nothing in tests/
 * links this.
 *
 * Output only, and deliberately so. This exists to drive a lighting desk -
 * QLC+ over a loopMIDI port, which then owns the fixtures and the DMX. Taking
 * MIDI in, for a footswitch, is a different job with different timing needs
 * and is not this.
 *
 * Nothing here may be called from the audio callback. Sending a MIDI message
 * is a syscall; the audio thread does no syscalls. Cues are fired from the UI
 * thread, where a frame of jitter - about 16 ms - is far below anything a
 * lighting rig can express.
 */
#ifndef BT_MIDI_H
#define BT_MIDI_H

#include "bt_types.h"
#include "bt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_MIDI_NAME_MAX 128

typedef struct {
    int32_t index;                     /* pass to bt_midi_open           */
    char    name[BT_MIDI_NAME_MAX];
} bt_midi_info;

typedef struct bt_midi bt_midi;

/* How many MIDI outputs the machine has, and what they are called.
 *
 * On Windows a virtual port from loopMIDI appears here by the name given to
 * it there, which is what QLC+ opens at the other end. */
int32_t bt_midi_count(void);
bt_err  bt_midi_get(int32_t index, bt_midi_info *out);

/* First output whose name contains `name_substr`, or -1. A NULL or empty
 * substring matches nothing: picking a MIDI port by accident is worse than
 * picking none, because the wrong one is usually a synth that will make
 * noise. */
int32_t bt_midi_find(const char *name_substr);

bt_err  bt_midi_open(int32_t index, bt_midi **out);
void    bt_midi_close(bt_midi *m);

/* One channel-voice message. `channel` is 1-16 as everybody writes it, not
 * 0-15 as the wire format has it - the translation belongs here rather than
 * in everyone's head. */
bt_err  bt_midi_send(bt_midi *m, uint8_t status, uint8_t d1, uint8_t d2);
bt_err  bt_midi_note_on (bt_midi *m, int32_t channel, int32_t note, int32_t velocity);
bt_err  bt_midi_note_off(bt_midi *m, int32_t channel, int32_t note);

/* A cue is a moment, not a held note, so this is what a cue sends: note on,
 * then note off. QLC+ triggers on the note on and ignores the rest, and
 * leaving a note held would stick if the program exited mid-song. */
bt_err  bt_midi_trigger(bt_midi *m, int32_t channel, int32_t note, int32_t velocity);

/* All notes off on every channel. For stopping cleanly, and for the moment
 * somebody closes the program with a rig lit. */
bt_err  bt_midi_panic(bt_midi *m);

#ifdef __cplusplus
}
#endif
#endif /* BT_MIDI_H */
