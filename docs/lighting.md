# Lighting

BackingTrackLive sends MIDI. QLC+ owns the fixtures and the DMX. The split is
deliberate: a lighting desk is a large program to write and a mature one to
borrow, and the only thing this program knows that QLC+ cannot is where the
music has got to.

## What goes down the wire

Three kinds of message, on one MIDI channel set per set list.

| when | message |
|---|---|
| a song starts | **program change**, the song's number |
| the playhead passes a cue bar | **note on + note off**, the set list's next-cue note |
| a cue that names its own note | **note on + note off**, that note |
| a jump - song change, scrub, seek | program change again, then one note per cue already passed |

That is all of it. A cue is a note on followed immediately by a note off, so
nothing is ever left held, and a song start is one program change.

The program change is what makes a jump work. A QLC+ cue list only knows
"next" - it has no way to be told "go to step 5". Resending the program change
puts it back at the top, and the catch-up notes walk it forward to where the
music actually is. Without that, scrubbing in rehearsal would leave the lights
permanently out of step with the band.

Cues fire from the UI thread, never the audio callback: sending MIDI is a
syscall, and the audio callback may not make one. A frame is about 16 ms and
DMX refreshes at around 44 Hz, so nothing downstream can tell.

## Setting it up

### 1. A port for the two programs to meet on

Windows has no MIDI equivalent of a patch cable, so a virtual one is needed.
Install **loopMIDI**, press **+**, and name the port. Set it to start with
Windows - the port only exists while loopMIDI is running, and a port that
vanished is the most boring possible reason for a dark stage.

### 2. Point this program at it

**Edit mode → lighting**. Pick the port, set the channel, and note the two note
numbers. The port is stored with the audio settings because it belongs to the
laptop; the channel and notes are stored in the set list because they belong to
the show, and travel to the backup laptop with it.

### 3. Point QLC+ at the same port

Inputs/Outputs, find the loopMIDI port, tick it as an **input**.

### 4. Teach QLC+ the messages

The reliable way is to let it listen rather than to type numbers in. Every
binding page in QLC+ has an **Auto Detect**, and the lighting screen has a
**send the next-cue note** button for exactly this: press Auto Detect in QLC+,
press the button here, and QLC+ records what arrived.

- A **Cue List** widget per song, its **Next Cue** bound to the next-cue note.
- A **program change** per song selecting that song's cue list.

How to do the second part depends on how the rig is built, and is worth
settling before a rehearsal rather than during one. The usual shapes are a
solo frame of buttons - one per song, each bound to its program change, each
selecting a cue list - or virtual console pages with the program change bound
to page select.

## Proving it without a rig

Most of this can be checked with no fixtures and no DMX adapter at all, which
is worth doing before everything is in a room together.

**QLC+ will advance a cue list with nothing patched.** Build the cue lists,
play a song, and watch the steps advance on screen in time with the music. That
proves the program changes select the right list, the notes step it, and the
bars land where you meant them to. What it cannot show is light on a wall.

**Watch the wire itself** when something looks wrong:

```
btmidi --list
btmidi --listen "loopMIDI" 60
```

Run that in one window and play a song in another. If the cues appear there,
this program is doing its job and the problem is in QLC+'s input profile or its
cue lists. If they do not, it is this program. That one question - which side -
is most of the debugging, and guessing at it in a dark room with a band waiting
is no way to find out.

A song should produce exactly one program change and two messages per cue.

## Getting cues in without typing them

A band that planned its show in a spreadsheet should not retype it:

```
tools/add-cues.py setlist.json cues.tsv --dry-run
tools/add-cues.py setlist.json cues.tsv
```

One row per song: title, program number, the cue bars, the end bar. Titles are
matched ignoring case and punctuation, because a spreadsheet and a set list
will disagree about apostrophes. It reports what matched in both directions and
backs the set list up before writing. `examples/band-cues.tsv` is a real one.

## If there are no lights

Leave the channel at 0, or choose no port. Nothing is sent, and the lighting
section does not appear in the song editor.
