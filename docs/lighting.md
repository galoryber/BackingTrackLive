# Lighting

BackingTrackLive sends MIDI notes. QLC+ owns the fixtures, the looks and the
DMX, and a separate project owns the show design and generates the cue file
this reads.

The split is deliberate, and it is now a written contract:
`docs/lighting-contract.md`, format `random-riot-lighting/1`. Neither side
changes it alone.

## What this program knows

Almost nothing. It knows where the playhead is in bars, and it knows that when
the playhead reaches a bar listed in the cue file it should send the note the
file gives for that bar. It does not know what any look is, what colours
exist, how many fixtures there are, or anything about QLC+.

That is why adding songs, changing colours or moving a cue needs no change
here at all: it is a rebuild on the lighting side and a new `lighting_cues.json`.

## What goes down the wire

One note on and its note off, per event:

```
Note On   0x90 | (ch-1)   note   velocity
Note Off  0x80 | (ch-1)   note   0
```

No program changes, no controllers, no clock, no "next". **Every event is
absolute** - "show look number N" - which is the property that makes the rest
simple. There is no cue list position to stay in step with, so a song that is
skipped, restarted or scrubbed cannot leave the lights out of step, and there
is nothing to resync.

## The rules it follows

**Bar 1 is the downbeat after the count-in.** The count-in sends nothing; the
between-songs look stays up while the band hears the click.

**Seeking asks one question: what should the stage look like here.** The most
recent *look* at or before the playhead is sent, and nothing else. Hits -
blinders, fog - that were missed stay missed, because a blast of fog owed from
four minutes ago is not a debt worth paying. An end is not a look either, so
scrubbing back into a song does not leave the between-songs state up.

**A look that is already showing is not sent again.** QLC+'s buttons toggle,
so re-sending would switch the look off. What is showing is tracked as a
channel and a note together, since note 37 on channel 16 and note 37 on
channel 1 are different buttons.

**Stopping sends nothing.** Not a blackout, not the between-songs look,
nothing. The band may still be playing the song live, and stale light beats a
surprise every time. The between-songs look arrives from the song's own end
event, or because somebody pressed the button for it.

**Cues are sent from the UI thread, driven by the sample counter** - never
from a wall clock, and never from the audio callback, where a syscall would
cost a dropout. That puts them within about 16 ms of the bar.

## Setting it up

1. **loopMIDI**: a port named as the cue file asks - `BackTrackQLC` today. Set
   it to start with Windows; a port that vanished is the most boring possible
   reason for a dark stage.
2. **The cue file**: copy `lighting_cues.json` into the set list folder, beside
   `setlist.json`. It is read from there, so a set list folder carried to the
   backup laptop takes its lighting with it.
3. **QLC+**: open the same port as an input, and open the show.
4. **Edit mode -> lighting** confirms what loaded, which songs matched, and
   which port is open.

Song titles are matched **exactly** between `setlist.json` and
`lighting_cues.json`. The lighting screen says how many matched and names the
ones that did not, because a title that drifted by one character is otherwise
silent.

## Proving it without a rig

**QLC+ runs a show with no fixtures patched.** Its Virtual Console shows a
button per cue and lights the active one, so a whole set can be watched
on screen in time with the music.

**Watch the wire** when something disagrees:

```
btmidi --list
btmidi --listen "BackTrackQLC" 60
```

Cues appear there but nothing happens in QLC+ - it is the QLC+ side. Nothing
appears - it is this side. That one question is most of the debugging, and a
dark room with a band waiting is no place to work it out.

## If there are no lights

No `lighting_cues.json` in the set list folder means no lighting, one line on
the lighting screen, and everything else works exactly as before. A cue file
in a format this does not implement is refused outright rather than guessed
at: a show built against a different contract would send the wrong notes at
the wrong bars, which is worse than sending none.
