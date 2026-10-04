# Roadmap

Two different things live here, kept apart on purpose.

**CLI/UI parity** is the list of jobs you can only do today by typing a
command or editing JSON. Each one is fine for a technical user and is the
wrong first experience for anyone else, so each is a gap between "works for
the band" and "shippable".

**Not planned** is the list of things this deliberately will not do. It is
load-bearing: the reason this project exists is that every tool in the space
is a host with a set list bolted on, and the way it stays useful is by
refusing to become one.

---

## CLI / UI parity

Everything below is now reachable without a terminal. The CLI tools remain,
because they script and they are what CI runs, but nothing requires them.

| job | where it is in the UI |
|---|---|
| open or create a set list | start screen, with recents; remembered between runs |
| pick a device, map buses | edit → **audio** |
| validate the set (`btcheck`) | edit → **check**, and an issue clicks through to the song it is about |
| render to WAV (`btrender`) | edit → **check** → export |
| make a demo set (`--make-demo`) | start screen |
| list devices (`--list-devices`) | edit → **audio** |
| play (`btplay`) | the whole point of the thing |

Still only in `setlist.json` by hand: **tempo maps**, for a song whose tempo
changes partway through. The file format supports them — `"tempo": {"map":
[{"beat": 0, "bpm": 96}, {"beat": 8, "bpm": 132}]}` — and the engine plays
them correctly, including the rule that beat positions are computed from the
beat index rather than accumulated, so a map does not drift. What is missing
is a way to edit one.

Not urgent, and deliberately so: the band does not currently play anything
that changes tempo mid-song, having avoided those arrangements because they
were awkward to rehearse. The work becomes worth doing when a song they want
needs it, which is a better trigger than a guess about when it might.

What it would take, when that happens: a list of (beat, bpm) rows in the song
editor, and the align view drawing the grid from the map rather than a single
BPM — which it already does, since it asks `bt_tempo_beat_frame` for every
line rather than multiplying one tempo out.

## Spoken count-in cues

A band records a short spoken lead-in and plays it to the in-ears over the
click: *"Mr Brightside, starts on beat three with the guitar riff — ready,
here we go."* Wanted, and not scheduled.

**Most of it already works.** The engine does not treat the count-in as a
special region: the playhead is simply negative there, and a track's offset is
subtracted from it, so a stem nudged back far enough sounds during the
count-in and stops when the song begins. Route it to `inear` and the audience
hears nothing. `tests/test_engine.c` has
`test_a_stem_can_sound_during_the_count_in` to keep that true, because nothing
else in the suite renders a stem before song frame zero.

So the file-based version of this feature is: record the cue in anything,
add it as a stem on `inear`, and nudge it to start on the first count-in beat.
Today that means working out the nudge by hand — two bars of 4/4 at 148 BPM is
−3243 ms — which is arithmetic nobody should be doing.

**What is actually missing**, in the order it is worth doing:

1. **A nudge that understands the count-in.** The align view draws bars and
   beats either side of zero already; what it lacks is a way to say "start
   here" and have the offset computed. A button reading *start on the first
   count-in beat* is the whole feature, and it is small.

2. **`btcheck` should stop calling it a mistake.** The warning *"offset shifts
   the stem entirely before the song starts"* fires on exactly this, correctly
   by its own logic and wrongly for the intent. It needs to know that landing
   inside the count-in is deliberate.

3. **Recording in the program**, which is the only genuinely new engineering.
   The device layer is output-only; this needs an input stream, somewhere to
   put the samples, and a way to write a WAV. None of it is hard and all of it
   is new surface — a second device to choose, a level to watch, a file to
   name — on a program whose whole argument is that it does less than a DAW.

The honest recommendation is 1 and 2 first. They are small, they make the
feature usable with any recorder, and they would show whether the band
actually wants cues on every song or on three of them — which is the thing
worth knowing before adding a recorder to a player.

## Other product gaps, not CLI-shaped

- **Hands-free control.** A set cannot currently be run without touching the
  laptop. MIDI in for a footswitch is Phase 4 and is the difference between
  usable and comfortable on stage.
- **Align view.** Waveform against a click grid, drag to nudge, audition a few
  bars against the click. `bt_peaks` is built and tested for it.
- **Remembering things.** Window size, last set list opened, last device. None
  of it persists.
- **ASIO.** Not blocking — the UMC404HD exposes a four-channel endpoint under
  WASAPI — but worth having for exclusive device access and lower latency.
- **A backup-laptop story.** The set list folder is deliberately copyable, but
  nothing helps you keep two machines in step.

## Not planned

Each of these is a thing a DAW does, and doing it would make this the kind of
tool it exists to avoid being.

- **Plugin hosting.** No VST, no AU. The moment this hosts plugins it is a
  host with a set list bolted on, which is the thing every competitor already
  is.
- **Recording.** It plays files. It does not capture anything.
- **Time-stretching or pitch-shifting.** Tempo is metadata that must match
  what is already in the stems. Changing it live means stretching every stem,
  which is a different product and a licensing problem.
- **Mixing beyond gain and mute.** No EQ, no compression, no sends. Stems
  arrive mixed; the desk does the rest.
- **Aggregating multiple audio devices.** Two devices means two clocks, and
  two clocks means drift. One interface, one clock.
- **Beat detection.** The BPM and first downbeat are written down when the
  stems are bought. Guessing them is a hard problem whose failures are
  invisible until a gig.

---

If these should be GitHub issues rather than a file, say so — the text is
ready to paste, and a file in the repository is the version that survives
without anyone remembering to file them.
