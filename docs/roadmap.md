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

| today | UI equivalent needed | notes |
|---|---|---|
| `btplay --list-devices`, then hand-write `device.json` | **Routing editor**: pick device and API from the enumerated list, map buses to channels | Smaller than it looks. `bt_device_*` already enumerates with API names and channel counts; `bt_device_cfg_save_file` already writes the file losslessly. This is a screen over two things that exist. |
| `btcheck setlist.json device.json` | **Validation panel** in edit mode: run it, list issues, click one to jump to the song or track it belongs to | `bt_setlist_validate` already returns structured issues with song and track indices, precisely so a UI can do this. The CLI is one consumer of it. |
| `btrender out.wav`, `btrender --set out.wav` | **Export**: render this song, or the whole set, to a file | Useful for sending a reference mix to the band, or checking alignment somewhere other than the stage. |
| `btrender --make-demo` | **First-run experience**: no set list yet, so offer to make one | Right now, launching with no `--setlist` silently opens an unsaveable demo. That is a developer's answer to an empty state. |
| editing `setlist.json` by hand for anything the editor cannot do yet | **Tempo map editing** (songs that change tempo), **count-in per song**, **reordering tracks** | The format supports all of it; the editor does not expose all of it. |
| reading stderr | **Surface errors in the UI**: a stem that will not decode, a bus that does not exist, a save that failed | `bt_err` is already structured and `bt_strerror` already says something useful. The UI mostly needs to show it. |

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
