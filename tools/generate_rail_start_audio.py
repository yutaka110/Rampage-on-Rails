"""Generate original, bounded READY/GO UI cues; no external samples."""
import math
import pathlib
import random
import struct
import wave

ROOT = pathlib.Path(__file__).resolve().parents[1] / "Resources" / "audio"
RATE = 44100


def cue(name, duration, go):
    samples = []
    for index in range(int(RATE * duration)):
        t = index / RATE
        envelope = min(t / 0.006, 1.0) * math.exp(-t * (12 if go else 17))
        if go:
            phase = 2 * math.pi * (660 * t + 120 * t * t)
            tone = 0.34 * math.sin(phase) + 0.20 * math.sin(phase * 1.5)
            tone += 0.12 * math.sin(2 * math.pi * 92 * t) * math.exp(-t * 26)
        else:
            tone = 0.30 * math.sin(2 * math.pi * 440 * t)
            tone += 0.12 * math.sin(2 * math.pi * 660 * t)
        value = tone * envelope * min((duration - t) / 0.03, 1.0)
        samples.append(max(-1, min(1, value)))
    ROOT.mkdir(parents=True, exist_ok=True)
    with wave.open(str(ROOT / name), "wb") as target:
        target.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        target.writeframes(b"".join(struct.pack("<h", round(sample * 32767)) for sample in samples))


def drone_flyby():
    """Original stereo motor + turbulent air, timed from READY at 0.64s.

    The right-hand pass peaks as the drone crosses the cart, then its engine
    loses pitch/brightness and settles toward the front as it brakes.
    """
    duration = 4.21
    rng = random.Random(8041)
    phase = air = 0.0
    samples = bytearray()
    for index in range(round(RATE * duration)):
        t = index / RATE
        approach = min(t / 1.25, 1.0)
        retreat = max(0.0, t - 1.25)
        amplitude = (0.08 + 0.35 * approach ** 1.8) * math.exp(-retreat * 1.5)
        amplitude *= min(t / 0.055, 1.0) * min((duration - t) / 0.32, 1.0)
        frequency = 124 + 43 * approach - 66 * (1 - math.exp(-retreat * 2.0))
        phase += 2 * math.pi * frequency / RATE
        air += 0.17 * (rng.uniform(-1, 1) - air)
        motor = (0.54 * math.sin(phase) + 0.23 * math.sin(phase * 2) +
                 0.12 * math.sin(phase * 3) + 0.055 * math.sin(phase * 7))
        flutter = 0.88 + 0.12 * math.sin(2 * math.pi * 23 * t)
        value = amplitude * (motor * flutter + air * 1.1)
        pan = 0.76 * math.exp(-max(0.0, t - 1.5) * 2.2)
        left = value * math.sqrt((1 - pan) * 0.5)
        right = value * math.sqrt((1 + pan) * 0.5)
        samples.extend(struct.pack("<hh", round(left * 32767), round(right * 32767)))
    ROOT.mkdir(parents=True, exist_ok=True)
    with wave.open(str(ROOT / "rail_drone_flyby.wav"), "wb") as target:
        target.setparams((2, 2, RATE, 0, "NONE", "not compressed"))
        target.writeframes(samples)


if __name__ == "__main__":
    cue("rail_ready.wav", 0.32, False)
    cue("rail_go.wav", 0.44, True)
    drone_flyby()
