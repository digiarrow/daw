#!/usr/bin/env python3
"""Synthesize an original four-second stereo tone for the Phase 0 demo.

The sound is generated here. It is not a recording or a sample from another product.
At 120 BPM each note starts on a beat, so the click lines up with the onsets.
"""

import math
import struct
import wave
from pathlib import Path

SAMPLE_RATE = 48000
SECONDS = 4.0
BPM = 120.0
FRAME_COUNT = int(SAMPLE_RATE * SECONDS)
BEAT_SECONDS = 60.0 / BPM

# Two original arpeggios, one note per beat.
NOTES = [
    (0, 440.00),
    (1, 523.25),
    (2, 659.25),
    (3, 880.00),
    (4, 392.00),
    (5, 493.88),
    (6, 587.33),
    (7, 783.99),
]


def main() -> None:
    left = [0.0] * FRAME_COUNT
    right = [0.0] * FRAME_COUNT

    for beat_index, frequency in NOTES:
        start = int(round(beat_index * BEAT_SECONDS * SAMPLE_RATE))
        duration = int(0.42 * SAMPLE_RATE)
        for offset in range(duration):
            frame = start + offset
            if frame >= FRAME_COUNT:
                break
            time = offset / SAMPLE_RATE
            envelope = math.exp(-time * 7.0)
            click = math.exp(-time * 90.0)
            body = math.sin(2.0 * math.pi * frequency * time)
            harmonic = 0.22 * math.sin(2.0 * math.pi * frequency * 2.0 * time)
            sample = (body + harmonic) * 0.55 * envelope + click * 0.18
            left[frame] += sample * 0.92
            right[frame] += sample * 0.78

    peak = max(max(abs(sample) for sample in left), max(abs(sample) for sample in right), 1.0e-9)
    scale = 0.55 / peak
    destination = Path(__file__).resolve().parents[1] / "examples" / "demo-tone.wav"
    destination.parent.mkdir(parents=True, exist_ok=True)

    with wave.open(str(destination), "w") as handle:
        handle.setnchannels(2)
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for index in range(FRAME_COUNT):
            for sample in (left[index] * scale, right[index] * scale):
                clipped = max(-1.0, min(1.0, sample))
                frames += struct.pack("<h", int(round(clipped * 32767.0)))
        handle.writeframes(frames)

    print(f"wrote {destination} ({FRAME_COUNT} frames, peak scaled to 0.55)")


if __name__ == "__main__":
    main()
