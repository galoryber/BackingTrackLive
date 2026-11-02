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

## Code signing, and SmartScreen

A fresh download triggers Windows SmartScreen and some antivirus heuristics,
because the executable is unsigned and nobody has downloaded it before.

**More file metadata will not help.** The version block already carries
publisher, product and version, and SmartScreen does not read any of it: its
reputation score comes from a code-signing certificate and from how many
people have run that exact binary without incident. An unsigned file starts at
zero every release, because changing a byte changes the file.

The options, honestly:

- **An EV certificate** buys reputation immediately, costs a few hundred a
  year, and needs a hardware token or an attested cloud key.
- **A standard OV certificate** is cheaper and starts at zero reputation,
  which accrues over downloads - slowly, for a program a handful of people
  run.
- **Nothing**, and tell the band to click through once per release.

Not worth the money while the audience is one band. Worth revisiting if this
is ever handed to people who did not build it, because "click past the warning"
is not something to ask a stranger to do.

## Click voice

The engine already synthesises the click from four parameters - `accent_hz`,
`beat_hz`, `decay_ms` and `gain` - and `bt_engine_set_click_voice()` sets them.
They are **hardcoded defaults with nothing reachable from outside**: not in the
UI, not in `setlist.json`, not a `btplay` flag. `bt_click_voice_defaults()` is
called once from `bt_engine_create()` and never again.

**The defaults stay exactly as they are.** 1600 Hz on the downbeat, 900 Hz on
the other beats, 28 ms decay, 0.7 gain. The band likes them and is unlikely to
change them; this is about having the option, not about a better default.

### Why it is worth having

A click is the worst-behaved signal in the mix, and the numbers say so. At 120
bpm the default click peaks at **-3 dBFS** and its long-term level is about
**-28 dBFS** - a crest factor of **~25 dB**, because the ear integrates
loudness over 100-200 ms and the burst is gone in 28. A loud backing-track
stem sits around 8-10 dB. So the click's peaks have to run roughly 15 dB
hotter than the tracks' to sound level with them in the ears, which makes it
the first thing to trip any ceiling downstream: a preamp, a desk compressor,
or - most often, because it is invisible from the desk - the limiter in an
in-ear belt pack.

None of that is fixable from a fader. Crest factor is the same at every gain
setting. The only two levers are the sound itself and removing the limiting
downstream, and only one of those is ours: **trading peak for duration and
moving the pitch up.** Doubling the decay while halving the gain is the same
perceived loudness about 6 dB lower in peak; moving the beats toward 2-3 kHz,
where the ear is most sensitive and a loud band is not, buys several dB more.

### What it would take

The UI is the easy half: four numbers on the audio screen with an audition
button, since judging a click by reading its parameters is hopeless.

Two things carry the actual risk.

**`bt_engine_set_click_voice()` is not safe to call while playing.** It frees
`click_accent` and `click_beat` and installs new buffers, and `render_click()`
walks exactly those pointers - with no `bt_engine_sync()` in between. It is
safe today only because it is called once, before any stream exists. Live
editing makes it a use-after-free on the audio thread, so it needs the same
sync-before-free discipline as swapping song PCM (rule 1b). The honest
alternative is to apply a changed voice only while stopped, which is less code
and no worse to use.

**The default has to round-trip byte-identical.** Whatever this adds to
`setlist.json` must be absent when the voice is the default, so that every set
list written before it re-saves unchanged - the same rule the lighting channel
followed.

Where it belongs: the set list, not the song. The click sound is a property of
how a band hears itself, not of one arrangement, and nobody wants to set it 41
times.

## Spoken count-in cues

A band records a short spoken lead-in and plays it to the in-ears over the
click: *"Mr Brightside, starts on beat three with the guitar riff — ready,
here we go."*

**This works now, without a recorder built in.** Record the cue in anything,
add it as a stem routed to `inear`, open the align view and press **start on
the count-in**. The stem's first sound lands on the first count-in beat, it
plays over the click, and it stops when the song begins. `btcheck` knows the
difference between that and a mistake, so it reports nothing.

The engine never needed a feature for it: the count-in is not a special
region, the playhead is simply negative there, and a track's offset is
subtracted from it. `tests/test_engine.c` has
`test_a_stem_can_sound_during_the_count_in` to keep that true.

**Recording inside the program** is the part not built, and is deliberately
last. The device layer is output-only, so it needs an input stream, somewhere
to put the samples and a way to write a WAV - none of it hard, all of it new
surface on a program whose argument is that it does less than a DAW: a second
device to choose, a level to watch, a file to name. Worth paying for if the
band ends up wanting a cue on every song. Not worth it for three, which a
phone already covers.

## Lighting cues over MIDI

The band runs QLC+ on the same laptop, driving a USB DMX adapter. A song
carries cues at bar positions, the player sends MIDI at those bars, and QLC+
owns the fixtures, the scenes and the DMX.

**This replaced the Art-Net / sACN plan.** Speaking DMX ourselves would mean
owning fixture definitions, patching and a 44 Hz output loop, to end up with a
worse version of a tool that already exists and that the band is already
learning. Sending MIDI to QLC+ is a few hundred lines; the rest is somebody
else's problem, correctly.

### Why this is not as hard as it sounds

Cues do not need the real-time path. `bt_player_tick` runs once per UI frame,
vsynced, so about 16 ms at 60 Hz - and lighting does not care: DMX itself
refreshes around 44 Hz, and QLC+ adds its own latency. So firing cues is
ordinary UI-thread work and never touches the rules that govern the audio
callback.

The split follows the one that already works for audio: the cue data and the
"which cues fall between the last playhead and this one" question live in
`libbacktrack`, testable headless on Linux to the sample. Talking to a MIDI
port lives in a separate platform library, as the PortAudio backend does.

### The decision, made

**Absolute events, not a cue list.** Every event says "show look number N" -
a bar, a MIDI channel, a note, and whether it is a look, a hit or an end.
Nothing sends "next".

An earlier design here was a shared "go" note plus a program change per song,
with QLC+ advancing its own list. It is worth recording why that went, because
the reasoning is the useful part: a cue list has a *position*, and a position
can be wrong. Skipping a song, restarting one, or scrubbing while aligning a
stem all leave the desk some number of steps out, and the fix - resend the
program change, then count forward from the top - is a resync path that has to
be correct for every way the transport can move. Absolute events delete the
position, and with it the whole class of bug. Step counting, program changes
and `bt_song_cues_before()` all went with it.

The rules that follow from absoluteness are in `docs/lighting.md`: seeking
sends the most recent *look* and never replays hits, a look already showing is
not re-sent (tracked as a channel and note together, because QLC+'s buttons
toggle), and stopping sends nothing at all.

### Where things live

Which **MIDI port** to open can come from the cue file, which names the port
the show was built against; failing that it comes from the audio settings.

**The cue file is the single source of truth.** `lighting_cues.json` sits
beside `setlist.json`, is read from there, and is *written* there when a bar is
edited - the same relationship a song's tempo has with `setlist.json`. There is
no second copy of the cues and no precedence rule between two sources, both of
which an earlier version had and neither of which survived contact with a real
file.

The format is a contract with the lighting project:
`docs/lighting-contract.md`, `random-riot-lighting/1`, and **neither side
changes it alone.** BTL round-trips keys it does not model, so a file carrying
something new comes back unharmed; anything that genuinely changes the meaning
of the file has to bump the version, where it is refused outright rather than
half-understood.

### Status

**Built, and used.** Verified on a real virtual MIDI port with a monitor on the
other end rather than from the shape of the code: the output layer
(`src/midi`, `btmidi`), the cue file reader and writer
(`src/model/bt_lightshow.c`), the firing, and cue editing on the song screen.

**Not yet proven against a real rig.** Everything above has been checked by
reading the bytes on the wire, and QLC+ will run a show with no fixtures
patched, which covers the notes going out at the right bars. What is untested
is the last hop: fixtures, the DMX adapter, and whether the cue bars are
musically right when there is light in the room. That needs the rig at the
practice space.

The one open item belongs to the other side: **regenerating
`lighting_cues.json` can overwrite a bar the band settled on at rehearsal**,
because nothing in the file marks which bars were hand-adjusted. Only the
generator can fix that, by keeping an existing bar for any cue that is still
there and taking the generated bar only for new ones. §9 of the contract says
so.

## Other product gaps, not CLI-shaped

- **Hands-free control.** A set cannot currently be run without touching the
  laptop, and that is the difference between usable and comfortable on stage.
  Less remains than Phase 4 suggests: MIDI *input* already ships - open, poll
  and `bt_midi_in_find` - because the lighting screen's `listen` button needed
  it. What is missing is binding an incoming note to play, stop or next, and a
  screen to set that binding on.
- **Align view.** Waveform against a click grid, drag to nudge, audition a few
  bars against the click. `bt_peaks` is built and tested for it.
- **Window size.** Not remembered between runs. The last set list, the last
  device and six recents are, in `settings.json` beside the other machine
  config - window geometry is the one piece that is not.
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
