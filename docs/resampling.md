# Load-time resampling

Stems arrive at whatever rate they were sold at. A set list mixing 44.1 kHz and
48 kHz files is the normal case, so conversion has to be automatic and it has
to be inaudible.

## Where it happens

Once per stem, in `bt_song_load_audio()`, on the loader thread. Never in the
audio callback — see rule 4 in `CLAUDE.md`. By the time the engine sees a
track, every stem is already at the device rate and the mixer does nothing but
add and multiply.

## Why in-tree instead of libsamplerate

libsamplerate is excellent and solves a harder problem than this one: it is
built to convert at arbitrary, *changing* ratios in real time. None of that
applies at load time. Audio rates are always rational (44100/48000 reduces to
147/160), so the filter is a precomputed polyphase bank with no transcendental
evaluated per output sample, and the implementation is ~200 lines with no
submodule, no vendored build system, and nothing extra to get right on three
platforms.

The trade is that the quality claim has to be *demonstrated* rather than
borrowed. `tests/test_resample.c` measures the filter rather than inspecting
the code: passband flatness, stopband rejection, alias suppression, DC gain and
round-trip residual.

## Design

- Windowed-sinc, 32 taps per side at unity ratio, Blackman-Harris window
  (~−92 dB sidelobes; chosen over Kaiser because it needs three cosines rather
  than a Bessel function, and the extra control Kaiser offers is not needed
  here).
- Cutoff is `0.5` of the input rate when upsampling, and drops to the **output**
  Nyquist when downsampling — otherwise the discarded band folds back as
  aliasing.
- Exact rational phase accounting: output frame `j` sits at input position
  `j·down/up`, split into an integer sample and a phase index. No floating-point
  position accumulates, so alignment is exact however long the file is — the
  same discipline as beat positions.
- Each phase kernel is normalised to unity DC gain, which makes the DC test
  exact instead of approximate.
- Outside the input is treated as silence. Correct for a finite signal; the
  first and last few milliseconds are filter ramp.

## Measured response

Produced by the filter in `src/audio/bt_resample.c`. Reproduce with the tests.

**44.1 kHz → 48 kHz** (the conversion that will actually happen most):

| Input | Response |
|---|---|
| 100 Hz – 19 kHz | 0.000 dB |
| 20 kHz | −0.013 dB |
| 21 kHz | −0.732 dB |
| 21.5 kHz | −2.329 dB |

Transparent across the entire audible band; the roll-off sits at the 22.05 kHz
input Nyquist, where there is no musical content and no hearing.

**48 kHz → 24 kHz** (a deliberately aggressive case, Nyquist 12 kHz):

| Input | Output peak | Would alias to |
|---|---|---|
| 1–10 kHz | 0.0 dB | — |
| 11.5 kHz | −1.0 dB | — |
| 13 kHz | −46.3 dB | 11 kHz |
| 15 kHz | −112.7 dB | 9 kHz |
| 18 kHz | −133.7 dB | 6 kHz |
| 22 kHz | −145.4 dB | 2 kHz |

The transition band is narrow — content in the ~12–13 kHz sliver is only
attenuated ~46 dB. That band only exists for heavy downsampling, which is not
a real workflow here; for 44.1↔48 the equivalent region sits above 21 kHz. If
a genuine use for aggressive downsampling appears, lower the cutoff slightly
(trading a little passband for a wider transition) rather than adding taps.

## Cost

A few seconds per song at load for a full set of stems, paid once, on a
background thread, before the song is needed. The preload scheduler (current +
next song resident) is what keeps that off the critical path.
