# Test fixtures

Committed rather than generated at test time: encoding an MP3 would make every
CI runner depend on an encoder being installed, and codec tests want a
byte-stable input anyway.

All three hold the same signal - 0.5 s at 44100 Hz, 1 kHz in the left channel
and 2.5 kHz in the right, at half scale (`tone_mono.mp3` is the left channel
only).

`tone_stereo.wav` and `tone_stereo.flac` contain **identical 16-bit samples**,
which is what lets `test_flac_is_lossless` assert a bit-exact match rather than
a tolerance.

Generated with `soundfile` from an int16 source:

```python
import numpy as np, soundfile as sf
SR, N = 44100, 22050
t = np.arange(N, dtype=np.float64) / SR
left  = np.rint(0.5 * np.sin(2*np.pi*1000.0*t) * 32767.0).astype(np.int16)
right = np.rint(0.5 * np.sin(2*np.pi*2500.0*t) * 32767.0).astype(np.int16)
stereo = np.stack([left, right], axis=1)
sf.write("tone_stereo.wav",  stereo, SR, subtype="PCM_16")
sf.write("tone_stereo.flac", stereo, SR, subtype="PCM_16")
sf.write("tone_mono.mp3",    left,   SR)
```

Starting from integers matters. Writing float samples and letting libsndfile
quantise produced WAV and FLAC files differing by one LSB on half the samples,
because its float-to-int rounding is not identical across writers - which looks
exactly like a decoder bug and is not one.
