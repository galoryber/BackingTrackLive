# BackingTrackLive — working notes for Claude

Multi-track backing-track player for live bands. A **player**, not a DAW: fixed
files, fixed routing, fixed set order. Windows is the primary target; macOS and
Linux are supported build targets.

## Build & test

```bash
make check          # configure + build + full test suite. THIS is "green".
make build          # configure + build only
make test           # ctest only (assumes built)
make asan           # build + run tests under ASan/UBSan
make tsan           # build + run tests under ThreadSanitizer
make cov            # coverage report for our own code
make device         # build the device layer (fetches PortAudio) + enumerate
make clean
```

CMake directly, if needed:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

`cmake`/`ninja` are installed via pip into `~/.local/bin` in this dev container;
ensure it is on `PATH`.

## Workflow

Work on a branch, never directly on `main`:

```bash
git checkout -b feat/<thing>
# ... work, with `make check` green ...
git push -u origin feat/<thing>
# wait for CI, then fast-forward main only once it is green
```

`main` is what `release.yml` builds from and what the band laptop downloads.
It should never be red, even for the three minutes it takes CI to notice.

## Testing standards

Coverage floors are enforced in CI (90% line, 65% branch over `src/`). They
are floors, not targets: raise them when the real number moves up, never lower
them to go green.

A new test that passes on the first run has not yet been shown to work. Break
the code it covers and confirm the test fails, in the assertion you expect.
Watch for the mutation that fails to *compile* - `-Werror` will reject it and
you will run a stale binary and see a false pass.

## Non-negotiable rules

These are enforced by tests. Do not work around a failing rtsafe test.

1. **No allocation, locks, file I/O, logging, or syscalls inside
   `bt_engine_render()`** or anything it calls. All memory is acquired during
   load, on the loader thread. `tests/test_rtsafe.c` wraps `malloc`/`free` and
   asserts zero activity across a render.
1a. **Only the loader thread writes `bt_track::pcm`.** Everyone else reads it,
   and only after the loader has published that the song is resident. That is
   the whole ownership rule; violating it is a use-after-free on the audio
   thread.
1b. **Never mutate engine state the audio thread reads.** The engine keeps two
   resolved song slots and publishes the live index with release/acquire;
   `bt_engine_set_song()` writes the other one. Before freeing any PCM a
   render could be walking, call `bt_engine_sync()`. `tests/test_concurrency.c`
   under `make tsan` is what proves this, and it caught the original version
   of this code doing exactly the wrong thing.
2. **The sample counter is the only clock.** Never call `time()`,
   `clock_gettime()`, or any wall-clock source for anything musical — click,
   transport, cues, and (later) DMX all derive from `playhead_frames`.
3. **Beat positions are computed from the beat index, never accumulated.**
   `frame = downbeat + llround(beat * 60.0 / bpm * sr)`. Accumulating a
   per-beat delta drifts audibly over a 3-hour set.
4. **Resample at load, never in the callback.** `bt_resample_planar()`
   runs in `bt_song_load_audio()`; see `docs/resampling.md`.
5. **Paths in `setlist.json` are relative to the setlist file.** The setlist
   folder must be copyable to the backup laptop as a self-contained unit.
6. **Never commit `device.json`.** Bus→channel mapping is machine-local.
   `setlist.json` refers only to logical bus names (`inear`, `foh`, ...).
7. **Never commit the ASIO SDK.** It is not redistributable. See `docs/asio.md`.

## Layout

```
include/backtrack/   public C API (the library's contract)
src/json/            minimal strict JSON parser (a fuzz target)
src/model/           setlist/song/track model, JSON read and write
src/audio/           WAV decode (a fuzz target)
src/engine/          transport, click synthesis, mixer, routing
src/player/          set list player + loader thread: selection, on_end,
                     and the only code that loads or frees stems
src/device/          PortAudio backend. A SEPARATE library: libbacktrack has
                     no device dependency, and nothing in tests/ links this.
tools/btplay.c       CLI: plays a set list through a real device
src/util/            error strings, portable thread/mutex/condvar shim
tools/btrender.c     CLI: setlist.json -> rendered WAV (offline, deterministic)
tools/btcheck.c      CLI: validate a set list, report every problem at once
tests/               unit + golden-render + rtsafe tests
fuzz/                libFuzzer targets for the parsers
examples/setlist/    a runnable example set list
```

## Conventions

- C11. `bt_` prefix on everything public. Opaque structs where practical.
- Errors: return `bt_err` (see `bt_error.h`); never `exit()` in the library.
- Everything in `libbacktrack` is free of platform and device dependencies —
  it is pure computation over buffers, which is what makes it testable on Linux
  against a Windows target.
- Tests are deterministic. Golden renders compare against committed hashes.

## Phase status

- [x] Phase 0 — repo, build, CI, test harness
- [x] Phase 1 — model, JSON, click, mixer, routing, transport (headless)
- [x] Phase 1.5 — resampling, set list player, preload window
- [x] Phase 1.6 — WAV / FLAC / MP3 decode
- [x] Phase 1.7 — set list / device.json writing (lossless, byte-stable)
- [x] Phase 1.8 — background loader thread; loading never blocks the UI
- [x] Phase 1.9 — set list checker (btcheck)
- [~] Phase 2 — PortAudio device layer. Enumeration, open, callback and
      btplay are written and build on all three platforms; ASIO and real
      dropout behaviour need the band laptop and its UMC404HD.

## Shipping

Nothing but CI needs a compiler. Every green `ci` run uploads a runnable
`btrender` per platform (30-day artifacts); a `v*` tag runs `release.yml`,
which tests the Release build and publishes packaged binaries. MSVC links the
CRT statically so a released .exe needs no VC++ redistributable.

All three platforms build warning-clean with `-Werror`. Keep it that way.
- [ ] Phase 3 — cimgui stage UI
- [ ] Phase 4 — MIDI in (footswitch) / MIDI out (patch changes)
- [ ] Phase 5 — DMX via Art-Net / sACN
