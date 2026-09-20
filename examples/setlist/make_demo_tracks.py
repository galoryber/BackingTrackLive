#!/usr/bin/env python3
"""Generate the demo stems for examples/setlist.

Audio is not committed to the repo, so this regenerates it. Real set lists
point at real stems; this exists only so `btrender` can be run end to end
immediately after cloning.
"""
import math
import os
import shutil
import struct
import wave

SR = 48000


def write_wav(path, channels, seconds, fn, rate=SR):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    n = int(rate * seconds)
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(3)
        w.setframerate(rate)
        frames = bytearray()
        for i in range(n):
            for c in range(channels):
                v = max(-1.0, min(1.0, fn(i, c)))
                s = int(v * 8388607)
                frames += struct.pack("<i", s)[:3]
        w.writeframes(bytes(frames))
    print(f"  {path}  {channels}ch  {seconds:.1f}s  {rate} Hz")


def tone(hz, amp=0.3, decay=None, rate=SR):
    def f(i, _c):
        t = i / rate
        env = 1.0 if decay is None else math.exp(-t / decay)
        return amp * env * math.sin(2 * math.pi * hz * t)
    return f


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    print("writing demo stems:")
    write_wav(os.path.join(here, "tracks/demo-one/synth.wav"), 2, 16.0, tone(440))
    write_wav(os.path.join(here, "tracks/demo-one/bass.wav"),  1, 16.0, tone(110, 0.4))
    write_wav(os.path.join(here, "tracks/demo-one/cues.wav"),  1, 16.0,
              lambda i, c: 0.5 * math.sin(2 * math.pi * 660 * i / SR)
              if (i // SR) % 4 == 0 and (i % SR) < SR // 8 else 0.0)
    # Deliberately 44.1 kHz: a real set list mixes rates, and this makes the
    # example exercise load-time resampling rather than only the easy path.
    write_wav(os.path.join(here, "tracks/demo-two/pad.wav"), 2, 12.0,
              tone(220, 0.25, rate=44100), rate=44100)

    # Stems bought online usually arrive as MP3. Reuse the committed test
    # fixture rather than depending on an encoder being installed: this makes
    # the demo render exercise MP3 decode *and* 44.1k -> 48k resampling.
    src = os.path.join(here, "../../tests/fixtures/tone_mono.mp3")
    dst = os.path.join(here, "tracks/demo-two/lead.mp3")
    shutil.copyfile(src, dst)
    print(f"  {dst}  1ch  0.5s  44100 Hz  (MP3, copied fixture)")


if __name__ == "__main__":
    main()
