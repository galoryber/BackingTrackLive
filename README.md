# BackingTrackLive

A multi-track backing-track player for live bands.

Not a DAW. Fixed files, fixed routing, fixed set order: load a set list, pick a
song, hit play. The click goes to the in-ears, the stems go to front of house,
and nothing surprising happens at 11pm in a bar.

> **Status: Phase 1.** The engine, model and offline renderer are complete and
> tested. There is no audio device layer and no GUI yet - see
> [Roadmap](#roadmap). You can render a set list to a WAV file today; you
> cannot yet play one through an interface.

## Why

Every tool that does this job is a *host* - a general engine that runs
arbitrary plugins and arbitrary signal graphs, with a set list bolted on top.
A covers band needs a *player*. That is a different shape, not a smaller
version of the same thing, and you cannot get there by turning features off.

## Design

Three decisions carry most of the weight:

**Three threads, with one job each.** The driver calls `bt_player_render()`,
which forwards to the engine and does nothing else. The UI thread calls
`bt_player_tick()`, which decides what end-of-song means and never blocks. A
loader thread owns every decode, every resample and every `free` - and drains
the engine before releasing any buffer, so it cannot pull audio out from under
a render. `tests/test_concurrency.c` runs all three at once under
ThreadSanitizer, which is the only tool that reliably sees this class of bug.

**The engine is headless.** `bt_engine_render()` is a pure function of engine
state - no device, no clock, no threads, no allocation. Every question about
timing, alignment and routing is answered offline, on any platform, with no
audio hardware attached. That is why the whole of Phase 1 ships without a
driver.

**The sample counter is the only clock.** Click, transport, stems and - later -
lighting cues all derive from `playhead_frames`. Nothing consults wall-clock
time. Beat positions are computed from the beat index rather than accumulated,
so a three-hour set does not drift.

**Tracks route to logical buses, never to channel numbers.** `setlist.json`
names `inear` and `foh`; a machine-local `device.json` maps those to physical
channels. The set list folder is therefore portable - copy it to the backup
laptop with a different interface in it and it just works - and it is a text
file you can diff and commit.

Stems are decoded (WAV, FLAC or MP3) and resampled to the device rate once, at
load, so by the time the engine sees a track the format it arrived in has
stopped mattering. Format is detected from content rather than extension - a
stem named `.wav` that is really an MP3 is what happens when somebody re-saves
a file, and it should simply work. There is no tempo detection and no
time-stretching. A song stores its BPM,
time signature and the offset of its first downbeat, taken from wherever you
bought the stems. Collapsing the hardest problem in this domain into three
metadata fields is the single reason this project is tractable.

## Getting a build

You do not need a compiler. CI builds Windows, macOS and Linux binaries on
every push:

- **Latest build** - the Actions tab, open the most recent green `ci` run, and
  download the `btrender-windows-latest` artifact. Kept 30 days.
- **Tagged release** - push a `v*` tag and the `release` workflow publishes
  packaged builds to GitHub Releases.

Windows binaries link the C runtime statically, so the target machine needs no
Visual C++ redistributable - unzip and run.

One caveat worth reading before trusting a public build on stage: released
binaries are **WASAPI-only**, because the ASIO SDK cannot be committed to this
repository. Windows presents a four-output interface such as the UMC404HD to
WASAPI as two separate stereo devices, which means two clocks and a click that
drifts against the tracks. Separate click routing needs ASIO. See
[`docs/asio.md`](docs/asio.md) for how that build gets produced.

## Build from source

Requires CMake 3.16+ and a C11 compiler.

```bash
make check     # configure, build, run the full suite. This is "green".
```

or directly:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Playing through a real device

```bash
make device                  # builds the device layer (fetches PortAudio)
./build-dev/btplay --list-devices
./build-dev/btplay examples/setlist/setlist.json examples/setlist/device.json 0
```

`device.json`'s `device` field selects the interface by case-insensitive
substring - `"UMC404HD"` finds it without anyone transcribing the full name -
and `"default"` means whatever the machine calls default.

The device layer is a **separate library** from `libbacktrack`. The engine has
no platform or device dependency at all, and nothing in `tests/` links the
device code; that is what lets every question about timing, mixing and routing
be answered offline on any machine.

Phase 2 is not finished. Enumeration, opening a stream, the callback bridge,
underrun counting and device-loss detection are written and build on Windows,
macOS and Linux, but they have not yet met a real interface. ASIO is still
behind `-DBT_ENABLE_ASIO=ON` and needs the Steinberg SDK supplied out of band -
see [`docs/asio.md`](docs/asio.md).

## Try it

```bash
python3 examples/setlist/make_demo_tracks.py
cp examples/setlist/device.example.json examples/setlist/device.json
./build/btrender examples/setlist/setlist.json examples/setlist/device.json 0 out.wav
```

`out.wav` is a 4-channel file: the band mix on 1/2, click and cues on 3/4 -
exactly the samples an interface would have been handed.

To render the whole set list as one continuous file, following each song's
`on_end`:

```bash
./build/btrender examples/setlist/setlist.json examples/setlist/device.json --set set.wav
```

## Checking a set list before the gig

```bash
./build/btcheck setlist.json device.json
```

`btrender` and `btplay` stop at the first problem, one song at a time.
`btcheck` decodes every stem once and reports everything wrong in a single
pass, then exits non-zero if anything would actually stop the set playing:

```
Gig Night - 3 song(s), 7 track(s)

  error   song 2 (Real Song) track 1 (Keys): routes to bus "monitor3", which
          device.json does not define
  error   song 2 (Real Song) track 1 (Keys): missing.wav: file could not be read
  warning song 2 (Real Song) track 2 (Vox): silent.wav is entirely silent - wrong file?
  warning song 2 (Real Song) track 3 (Lead): hot.wav peaks at full scale and may
          already be clipped
  warning song 2 (Real Song): no click track - was that intended?
  warning song 3 (Closer): on_end is "next" but this is the last song; it will stop

  set length      0m 08s of audio (longest song 0m 03s)
  preload peak    1.3 MB   (current + next song)
  whole set       1.8 MB   if every song were held at once
```

Errors are things that will fail. Warnings are things that are probably not
what anyone meant - a set list full of warnings still plays. The preload peak
is the largest *adjacent pair* of songs, because that is what the window
actually holds.

## Set list format

```json
{
  "version": 1,
  "name": "Set List #1",
  "songs": [{
    "title": "1985",
    "artist": "Bowling for Soup",
    "tempo": { "bpm": 156.0, "sig": [4, 4], "downbeat_ms": 0 },
    "count_in_bars": 2,
    "on_end": "stop",
    "tracks": [
      { "name": "Click", "type": "click", "bus": "inear" },
      { "name": "Synth", "type": "audio", "bus": "foh",
        "file": "1985/synth.wav", "gain_db": -2.0, "offset_ms": 0 }
    ]
  }]
}
```

- `downbeat_ms` - where beat 1 actually lands. Downloaded stems routinely open
  with silence or a pickup; this is what makes the click line up with the
  music instead of with the file.
- `offset_ms` - per-stem nudge, positive or negative.
- `on_end` - `stop` waits for a human (the singer is talking); `next` runs
  straight into the following song. The seam is one render block wide -
  measured at 5.4 ms at a 512-frame block - so it is gapless to an audience but
  not sample-accurate. A medley that must be musically locked belongs in one
  song file with the segue rendered in.
- `tempo.map` - an array of `{beat, bpm}` for songs that change tempo. The
  format accepts one from day one so that song never forces a migration.
- Stem paths are relative to the set list file, and absolute paths and `..`
  are rejected. A set list folder is meant to be a self-contained unit.

`device.json` is machine-local and gitignored - see
`examples/setlist/device.example.json`.

Both files can be written as well as read (`bt_setlist_save_file`,
`bt_device_cfg_save_file`). Saves go to a temporary alongside the target and
are renamed over it, so an interrupted save cannot leave a half-written set
list where a working one used to be. Numbers are emitted at the shortest
precision that parses back exactly, so a 156.37 BPM stays `156.37` rather than
becoming `156.36999999999999`.

## Testing

The test suite runs headless on Linux, macOS and Windows and needs no audio
hardware.

- **Golden render** - a fixture set list is rendered and hashed. Stems come
  from an integer PRNG and the fixture avoids `libm` entirely, so the hash is a
  fair byte-exact assertion on all three platforms.
- **Block-size invariance** - the same song must render bit-identically at
  every buffer size from 32 to 4096 frames. Any difference means state is
  leaking across a block boundary.
- **Concurrency** - a render thread, a UI thread selecting songs and the
  loader thread loading and freeing, all at once, under TSan with
  `halt_on_error`. Removing the drain before a free fails it immediately.
- **Real-time safety** - `test_rtsafe` wraps the allocator at link time and
  asserts that a render performs **zero** allocations. The most common cause of
  a rig glitching on stage is a `malloc` that crept into the audio callback;
  here that is a build failure rather than a bad night.
- **Tempo** - exact sample positions, no drift across 30,000 beats, correct
  accenting through negative (count-in) beats, and `frame_beat` proven to be
  an exact inverse of `beat_frame`.
- **Decoding** - FLAC is asserted to decode *bit-identically* to a WAV holding
  the same 16-bit samples, so losslessness is verified rather than claimed.
- **Serialisation** - load, save and load again must return the identical
  model, and saving an unchanged set list must produce byte-identical text.
  The second property is what keeps a set list you keep in git from churning
  its diff every time it is opened.
- **Resampling** - the filter is *measured*, not assumed: passband flatness,
  stopband rejection, alias suppression and round-trip residual. See
  [`docs/resampling.md`](docs/resampling.md) for the numbers.
- **Fuzzing** - libFuzzer targets over the JSON parser, the WAV decoder and the
  full set list binding path. `make fuzz` builds them.
- **Sanitizers** - ASan and UBSan on every push.

## Roadmap

| Phase | Content | Status |
|---|---|---|
| 0 | Repo, build, CI, test harness | done |
| 1 | Model, JSON, click, mixer, routing, transport | done |
| 1.5 | Load-time resampling, set list player, preload window | done |
| 1.6 | WAV / FLAC / MP3 decode | done |
| 1.7 | Set list / device.json writing | done |
| 1.8 | Background loader thread | done |
| 1.9 | Set list checker (`btcheck`) | done |
| 2 | PortAudio device layer (WASAPI, then ASIO) | builds; needs hardware |
| 3 | Stage UI (Dear ImGui via cimgui) | |
| 4 | MIDI in (footswitch) and out (patch changes) | |
| 5 | DMX lighting via Art-Net / sACN | |

Known gaps: branch coverage sits at ~70%, concentrated in allocation-failure
paths that nothing currently exercises; and the device layer has never met a
real interface.

See [`docs/asio.md`](docs/asio.md) for why the ASIO SDK is not, and will not
be, committed to this repository.

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
