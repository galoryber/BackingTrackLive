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

## Licensing, if this is ever sold

Nothing here is planned. It is written down because the shape of the problem
is not obvious and the constraints are unusual, and because the four rules at
the bottom are the ones most likely to be forgotten by anyone reaching for an
off-the-shelf licensing SDK.

**Every released tag stays Apache-2.0 for ever.** That grant is perpetual and
irrevocable per copy, so anyone can fork `v0.1.29` and build it, and no
licensing system changes that. What is still open is future versions: there is
a single copyright holder and no outside contributors, so there is nobody to
ask. Three honest models:

- **Closed from here.** Leave the Apache tags up, make the next version
  proprietary. The public repository stops being the product.
- **Open core.** `libbacktrack` stays Apache - engine, model, JSON,
  resampler, the parts that are pure computation - and the UI, lighting and
  device layers go commercial. The tree is already split along that line.
- **Open source, paid binaries.** Source stays open; what is sold is the
  built, signed, ASIO-capable Windows package. This is the Ardour model, and
  it sells to the same people. Nobody who would pay for this is going to set
  up MSVC, Ninja and the Steinberg SDK to avoid it.

The third needs the least new machinery and fits what is already here.

### Three separate jobs, usually sold as one box

- **Money** - checkout, refunds, and sales tax in dozens of jurisdictions.
  A *merchant of record* (Paddle, Lemon Squeezy, roughly 5% + 50c) becomes
  the legal seller and owes that tax instead of you. Stripe does not: it is
  a processor, and the VAT and state-nexus filings stay yours. For a side
  project that is the worst job to own and the cheapest to hand away.
- **Entitlements** - issuing keys, counting seats, revoking, spotting abuse.
  Keygen, Cryptlex, LicenseSpring.
- **Enforcement** - the code here that decides whether to run. Always ours.

### The architecture, which is a pattern and not a standard

There is no agreed protocol - no OAuth of licensing - but everyone has
converged on the same four steps, and they happen to be exactly what this
program needs:

1. A purchase issues a **license key**, which is only an opaque identifier.
2. The app activates **once, online**, sending the key and a machine
   fingerprint.
3. The server checks the seat count and returns a **signed license file**:
   license id, product, seats, expiry or perpetual, issued-at, fingerprint,
   signed with Ed25519.
4. The app holds the public key and **verifies that file locally, for ever,
   with no network.**

Step 4 is the whole answer to a laptop that never sees wifi at a venue.
Verification is a signature check over a few hundred bytes - no socket, no
wall clock, one file read - which is the same shape as everything in
`libbacktrack` and would be tested the same way, against golden vectors.

Keygen and Cryptlex both implement this; Keygen documents carrying its license
files to air-gapped machines on a USB stick, and defaults those files to a
30-day TTL, which a perpetual license would have to override. Cryptlex's
`LexActivator` has a dependency-free C SDK, which matters here more than it
does to most of their customers. Rolling it instead is a real option: vendor
an Ed25519 implementation the way `third_party/` already vendors single-file
public-domain code, and the client side is a couple of hundred lines with no
vendor who can go out of business and brick every laptop in the field.

### Trials, seats, and the fingerprint trap

A trial should be a real signed license with an expiry in it, issued for an
email address, rather than an anonymous local timestamp. It costs the user one
field, and in exchange a band that asks can be extended by hand and everyone
can be told about a release. Clock-setback is answered by storing the highest
timestamp ever seen and refusing to believe time ran backwards; that is where
the effort should stop.

**A trial has to survive a gig.** The only test a band cares about is a real
show, so expiry blocks *starting a session*, never a running one, and the
warning appears at launch with the days remaining. A trial that runs out at
9:40pm on a Saturday is a story told to other bands for years.

**More than one laptop is a requirement, not a loophole.** There is a backup
machine, and often a rehearsal machine. Five activations with self-service
deactivation costs nothing and avoids a support mail every time somebody
replaces a laptop.

What to fingerprint is where this goes wrong. On Windows, `MachineGuid` dies
on a reimage, the volume serial dies on a reformat, and the MAC address
changes with a dock, a USB ethernet adapter or a VPN - and this audience
plugs things in constantly. Generate a UUID on first run instead and keep it
in `%APPDATA%\BackingTrackLive\`: it survives docks and dies on a reimage,
which is the right way round. A mismatch should prompt a re-activation and
never a lockout.

That file belongs beside `device.json` by rule 6 - machine config is
invisible, user content is visible - which also keeps it **out of the set list
folder**, so handing a USB stick to the backup laptop keeps working exactly as
it does now.

### The threat model is key sharing, not cracking

With public source a check is one revert away, and even a closed native binary
falls to one patched conditional jump. Obfuscators and packers buy hours
against anyone determined, cost real engineering, and introduce crash modes on
users' machines that cannot be debugged from here - a bad trade for any
program, and an absurd one for a program running a show.

The realistic failure is one person buying a key and passing it to four other
bands, and that is a detection problem the entitlement layer already solves:
forty activations on one key is visible in a dashboard and can be revoked or
upsold. No obfuscation would have helped. Sign the license, verify it
properly, count seats, watch for outliers, accept the rest - this is a small,
chatty, word-of-mouth audience where a reputation for hostile DRM costs more
than the leakage.

### Rules this program would have to keep

1. **A show never fails because of licensing.** The check runs once at
   launch, on the UI thread. Nothing license-related goes anywhere near the
   audio callback; rule 1 forbids syscalls in render, and a network call there
   is unthinkable.
2. **No periodic phone-home.** The laptop has no wifi at a venue. A perpetual
   license verifies offline indefinitely, and anything subscription-shaped
   re-checks opportunistically with a grace period measured in months.
3. **Lapsed means read-only, not dead.** The set list still plays. Edits and
   updates are what stop.
4. **Degrade, never block.** A corrupt license file, a missing one, an
   unreadable `%APPDATA%` - the failure mode is a warning on screen, not a
   program that will not start an hour before doors.

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

## Starting maximized, and a fullscreen button

**Both built on 2026-11-03.** It starts maximized, and the set list carries a
fullscreen button in its top-right corner.

The hazard this entry was written to flag turned out to be unverifiable here
rather than absent: `toggle_fullscreen()` saved the window rect and style on
the way in and put them back on the way out, and starting maximized means the
saved style carries `WS_MAXIMIZE` with a monitor-sized rect - a window
claiming to be maximized at coordinates somebody wrote down. Testing it needs
an interactive desktop, and the test VM is reached over SSH in session 0,
where the program exits rather than opening a window at all.

So the dependency was removed instead of tested: exiting fullscreen from a
maximized window now hands the job to `ShowWindow(SW_MAXIMIZE)` rather than
replaying a saved rect, which has no edge case to get wrong. **The F11 round
trip is still unconfirmed on real hardware** - it wants ten seconds at the gig
laptop.

**The fullscreen button needs no new machinery**, which was the open question
in asking for it. Play mode is already mouse-aware: the set list hit-tests
rows against `ImGui::GetIO().MousePos` and reports the click back to the host
through `bt_ui_result::click`. So the button is a fourth value on
`bt_ui_click`, a rect to hit-test, and the host calling
`toggle_fullscreen(hwnd)` - which already exists and already takes the HWND.

Two details it does need:

- **The screen has to know which state it is in**, so the icon can mean
  "leave" while fullscreen and "enter" while not. That is a `bool fullscreen`
  on `bt_ui_state`, set by the host from `g_fullscreen`.
- **Not in the footer.** The hints and the interface badge already compete for
  that line, and the hints drop one at a time until what is left fits beside
  the badge. A third claimant makes a tight line tighter, and the top corner
  is empty.

The icon can be a glyph rather than drawn geometry: ImGui is pinned at
v1.92.9b, whose font system rasterises glyphs on demand, so the atlas is not
confined to the Latin range that `load_font()` asks for. That is also why the
segue arrow (U+2193) renders at all.

## Other product gaps, not CLI-shaped

- **Hands-free control.** A set cannot currently be run without touching the
  laptop, and that is the difference between usable and comfortable on stage.
  Less remains than Phase 4 suggests: MIDI *input* already ships - open, poll
  and `bt_midi_in_find` - because the lighting screen's `listen` button needed
  it. What is missing is binding an incoming note to play, stop or next, and a
  screen to set that binding on.
- **Narrow windows.** Below about 1280px wide, rows laid out at fixed widths
  run past the edge: measured at 1024x700, align overruns by 316px, audio by
  150, the song screen's cue row by 63 and the set list by 20. Harmless on the
  gig laptop, which runs maximized on a large screen, and a reflow of four
  screens to fix - so it is written down rather than half-done.
- **Window size.** Not remembered between runs. The last set list, the last
  device and six recents are, in `settings.json` beside the other machine
  config - window geometry is the one piece that is not. Starting maximized
  (above) makes most of this moot, since the size nobody wants to re-set is
  the full screen.
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
