# BackingTrackLive ↔ Random Riot Lights — Integration Contract

**Format version:** `random-riot-lighting/1`
**Lighting side (owner):** `C:\Users\rando\OneDrive\Desktop\QLC-Lights\RandomRiot\` (QLC+ 5 show, generator, tests)
**Playback side (owner):** BackingTrackLive (`github.com/galoryber/backingtracklive`)

This document is the only thing the two projects share. If either side needs something to change, change this
document first (bump the format version on breaking changes), then both sides implement against it.

---

## 1. The one-sentence version

During playback, when the song reaches a bar listed in the cue file, BackingTrackLive sends **one MIDI Note On
(and its Note Off)** with the listed channel and note to the MIDI port `BackTrackQLC`. That's the entire job.

BackingTrackLive does **not** know what any look is, what colors exist, or anything about QLC+. The lighting side
decides all of that and bakes it into the cue file.

## 2. Who owns what

| Concern | Owner |
|---|---|
| Which looks exist, what they look like, which bar each song section starts on | Lighting (`show_design.json`) |
| Assigning MIDI channel/note numbers to cues | Lighting (generator) |
| Producing the cue file `lighting_cues.json` | Lighting (`build_show.ps1`) |
| Knowing the playhead position in bars (count-in, tempo maps) | BackingTrackLive |
| Sending the notes at the right moment | BackingTrackLive |
| Opening/closing the MIDI port, failing gracefully if it's absent | BackingTrackLive |

## 3. The cue file — `lighting_cues.json`

Generated at `RandomRiot\build\lighting_cues.json`. BackingTrackLive reads it (path is a BTL setting).
Fields BTL must use are **bold**; everything else is informational.

```json
{
  "format": "random-riot-lighting/1",
  "midi_port": "BackTrackQLC",
  "velocity": 127,
  "specials": {
    "between_songs": { "ch": 16, "note": 37 },
    "stop_all":      { "ch": 16, "note": 39 },
    "blinder":       { "ch": 16, "note": 72 },
    "fog_left":      { "ch": 16, "note": 74 },
    "fog_right":     { "ch": 16, "note": 76 },
    "fog_dual":      { "ch": 16, "note": 77 }
  },
  "songs": [
    { "title": "Corduroy",
      "events": [
        { "bar": 1,   "ch": 1,  "note": 0,  "type": "look", "label": "Intro Guitar Riff" },
        { "bar": 17,  "ch": 1,  "note": 1,  "type": "look", "label": "Verse 1" },
        ...
        { "bar": 161, "ch": 16, "note": 37, "type": "end",  "label": "Between Songs" }
      ] }
  ]
}
```

- **`format`** — refuse to use the file (log a warning, run without lights) if it isn't `random-riot-lighting/1`.
- **`midi_port`** — substring passed to `bt_midi_find()`.
- **`velocity`** — Note On velocity. Always 127 today.
- **`songs[].title`** — matched **exactly** against `title` in BTL's `setlist.json`. A song with no match simply has no lighting.
- **`events[].bar`** — 1-based song bar (see §5). Events are sorted by bar; several events can share a bar.
- **`events[].ch`** — MIDI channel **1–16** (the `bt_midi_*` API convention; on the wire it's `ch-1`).
- **`events[].note`** — 0–127.
- `type` (`look` | `hit` | `end`) and `label` — for logs/UI only. BTL must treat every event identically.
- `specials` — for the explicit actions in §6.

## 4. What goes over the wire

Exactly what `bt_midi_trigger(m, ch, note, velocity)` already does:

```
Note On   status 0x90 | (ch-1), data1 = note, data2 = velocity (127)
Note Off  status 0x80 | (ch-1), data1 = note, data2 = 0
```

One trigger per event. No program changes, no CCs, no clock, no "next" messages. Every event is absolute
("show look #N"), so a skipped, repeated or restarted song can never leave the lights out of step.

## 5. Timing

- **Bar 1** is the first bar of the song itself, i.e. the downbeat right **after** the count-in. Count-in bars send nothing
  (the between-songs look stays up while the band hears the click).
- An event for bar N fires on the **downbeat of bar N** — the same instant the click plays beat 1 of that bar.
  Use BTL's own bar math (including tempo maps); the lighting side assumes nothing about tempo.
- Fire from the playback/transport timing path, not a UI timer. ±20 ms is fine; lights are not sample-accurate.
- **Seeking/starting mid-song:** fire the most recent `look` event at or before the playhead immediately, then continue
  normally. Do **not** re-fire past `hit` events (those are fog/strobe). Skip it if it's the look that is already
  showing (see the repeat rule in §6) - e.g. restarting playback after a glitch in the same section.

## 6. Explicit actions (not tied to bars)

**Stale lights beat surprise changes.** If playback stops for any reason other than reaching the song's `end` event
(user pressed stop, tech glitch, crash, app closed), BTL sends **nothing**. The band may still be playing the song live,
so the lights must stay on whatever look is currently running. BTL never sends `between_songs` or `stop_all` on its own.

| When | Send |
|---|---|
| Song reaches its `end` event | the `end` event itself (already in the file - nothing special) |
| Song stopped early, glitch, app closing | **nothing** |
| User explicitly presses a "lights panic / blackout" control (optional UI) | `specials.stop_all` |
| User explicitly presses a "between songs" control (optional UI) | `specials.between_songs` |
| (Optional future) manual hit buttons in BTL's UI | `specials.blinder` / `fog_*` |

Never send the same `look` note twice in a row: QLC+ buttons toggle, so a repeat would switch that look **off**.
Rule of thumb: remember the last note sent; if the next look event is identical, skip it.

## 7. Lights must stay optional

BackingTrackLive must work exactly as today when lighting isn't in use:

- No cue file configured / file missing / wrong `format` → no lighting, one log line, playback unaffected.
- MIDI port not found or `bt_midi_open` fails → no lighting, one log line, playback unaffected. Never block on MIDI.
- A send error mid-show → log once, keep playing audio. Audio always wins.
- Suggested settings: `lighting.enabled` (bool), `lighting.cue_file` (path). Port name comes from the cue file.

## 8. How to test the BTL side without the rig

1. loopMIDI running with a port named `BackTrackQLC`.
2. Open `RandomRiotLights2026.qxw` in QLC+ 5 (no lights or DMX needed). The Virtual Console shows one button per cue;
   the active one lights up as BTL plays.
3. Or, with QLC+ closed, use any MIDI monitor on the loopMIDI port and compare against `lighting_cues.json`.

The lighting side's own regression test (`RandomRiot\tools\test_show.ps1`) already proves every note in the cue file
triggers the right look in QLC+, so if BTL sends the file's notes at the file's bars, the show is correct.

## 9. Changing the show

Adding or editing songs, looks, colors or bars happens only on the lighting side: edit `show_design.json`, run
`build_show.ps1`, and BTL picks up the regenerated `lighting_cues.json`. **No BTL code change is ever needed for show
content.** BTL code only changes if this contract's format version changes.

## 10. Open questions for the BTL side

**Answered by BTL, 2026-10-07.**

**1. Does BTL's bar numbering already start at 1 after the count-in, matching §5?**

Yes, exactly. BTL computes a bar's position as `beat = (bar - 1) * sig_num` and the
count-in occupies *negative* frames (`bt_tempo_beat_frame(..., -count_in_beats, ...)`).
So bar 1 is beat 0 is frame 0 is the first downbeat after the count-in, whatever
`count_in_bars` is set to. Nothing is sent during the count-in. There is a test for
this (`test_bar_one_is_the_downbeat`).

Bar positions are always computed from the beat index, never accumulated, which matters
over a three-hour set: accumulating a per-beat delta drifts audibly. Tempo maps are
honoured by the same call, so §5's "use BTL's own bar math" is satisfied for free.

**2. Where should `lighting.cue_file` live in BTL's config, and should `setlist.json` reference it?**

In `setlist.json`, as a path **relative to the set list file**, and BTL will look for
`lighting_cues.json` beside `setlist.json` by default.

The reasoning is an existing BTL rule: a set list folder has to be copyable to the backup
laptop as a self-contained unit, so everything a set list needs lives inside it by a
relative path. An absolute path to `C:\Users\rando\...` would break the moment the
folder moved, and the backup laptop is exactly the case that matters.

So: **copy the generated `lighting_cues.json` into the set list folder** as the last step
of `build_show.ps1`, or point BTL at it and it will be read from wherever the set list
says. The port name still comes from the cue file, per §3.

`lighting.enabled` is not needed as a separate setting: a cue file that is present and
readable means lights, and one that is absent means none. One fewer thing to have set
wrong in a dark room.

**3. Should BTL's UI show the current lighting cue label (nice for rehearsals)?**

Yes - agreed, and it is cheap since `label` is already in the file. BTL will show the
current look's label on the play screen and in the editor, plus how many of the set
list's songs matched a show song, which is the fastest way to catch a title that drifted
between the two files.

---

## 11. Two notes back from the BTL side

**§6's repeat rule is tracked as a (channel, note) pair, not a note alone.** Looks are on
channel 1 and specials on channel 16, so note 37 on channel 16 and note 37 on channel 1
are different buttons. Suppressing on the note alone would skip a legitimate look. If
the lighting side ever puts two different buttons on the same channel *and* note, say so
- but that would be ambiguous for QLC+ too.

**§5's "fire from the playback/transport timing path, not a UI timer" - BTL complies in
substance, with one deliberate difference worth stating.** BTL sends MIDI from its UI
thread, but driven by the audio playhead (a sample counter), never from a wall clock.
Sending MIDI is a syscall, and BTL has a hard rule that its audio callback performs no
syscalls, allocation, locks or I/O - violating it causes dropouts, which during a gig is
worse than a late cue. The UI thread ticks at vsync, so cues land within ~16 ms of the
sample-accurate bar position, inside §5's ±20 ms. If the lighting side ever needs tighter
than that, it needs a different mechanism and we should talk.
