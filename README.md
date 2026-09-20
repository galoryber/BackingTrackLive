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

There is no tempo detection and no time-stretching. A song stores its BPM,
time signature and the offset of its first downbeat, taken from wherever you
bought the stems. Collapsing the hardest problem in this domain into three
metadata fields is the single reason this project is tractable.

## Build

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

## Try it

```bash
python3 examples/setlist/make_demo_tracks.py
cp examples/setlist/device.example.json examples/setlist/device.json
./build/btrender examples/setlist/setlist.json examples/setlist/device.json 0 out.wav
```

`out.wav` is a 4-channel file: the band mix on 1/2, click and cues on 3/4 -
exactly the samples an interface would have been handed.

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
  straight into the following song.
- `tempo.map` - an array of `{beat, bpm}` for songs that change tempo. The
  format accepts one from day one so that song never forces a migration.
- Stem paths are relative to the set list file, and absolute paths and `..`
  are rejected. A set list folder is meant to be a self-contained unit.

`device.json` is machine-local and gitignored - see
`examples/setlist/device.example.json`.

## Testing

The test suite runs headless on Linux, macOS and Windows and needs no audio
hardware.

- **Golden render** - a fixture set list is rendered and hashed. Stems come
  from an integer PRNG and the fixture avoids `libm` entirely, so the hash is a
  fair byte-exact assertion on all three platforms.
- **Block-size invariance** - the same song must render bit-identically at
  every buffer size from 32 to 4096 frames. Any difference means state is
  leaking across a block boundary.
- **Real-time safety** - `test_rtsafe` wraps the allocator at link time and
  asserts that a render performs **zero** allocations. The most common cause of
  a rig glitching on stage is a `malloc` that crept into the audio callback;
  here that is a build failure rather than a bad night.
- **Tempo** - exact sample positions, no drift across 30,000 beats, correct
  accenting through negative (count-in) beats, and `frame_beat` proven to be
  an exact inverse of `beat_frame`.
- **Fuzzing** - libFuzzer targets over the JSON parser, the WAV decoder and the
  full set list binding path. `make fuzz` builds them.
- **Sanitizers** - ASan and UBSan on every push.

## Roadmap

| Phase | Content | Status |
|---|---|---|
| 0 | Repo, build, CI, test harness | done |
| 1 | Model, JSON, click, mixer, routing, transport | done |
| 2 | PortAudio device layer (WASAPI, then ASIO), load-time resampling | next |
| 3 | Stage UI (Dear ImGui via cimgui) | |
| 4 | MIDI in (footswitch) and out (patch changes) | |
| 5 | DMX lighting via Art-Net / sACN | |

Known gaps in Phase 1, in priority order: stems must already be at the device
sample rate (`BT_ERR_RATE` rather than a silent quality compromise - Phase 2
adds libsamplerate); MP3 and FLAC are not decoded yet; there is no preload
scheduler for the next song.

See [`docs/asio.md`](docs/asio.md) for why the ASIO SDK is not, and will not
be, committed to this repository.

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
