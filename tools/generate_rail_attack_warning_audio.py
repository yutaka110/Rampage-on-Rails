"""Generate original, deterministic weapon-warning audio (44.1 kHz mono PCM)."""
from pathlib import Path
import math
import random
import struct
import wave

RATE = 44100
ROOT = Path(__file__).resolve().parents[1] / "Resources" / "audio"


def synth(weapon: int, phase: str) -> bytes:
    duration = {"prepare": (0.24, 0.26, 0.42)[weapon],
                "commit": 0.13, "fire": (0.15, 0.12, 0.30)[weapon], "charge": 0.20}[phase]
    rng = random.Random(1049 + weapon * 71 + sum(map(ord, phase)))
    output = bytearray()
    low_noise = 0.0
    base = (420.0, 560.0, 85.0)[weapon]
    for i in range(int(RATE * duration)):
        t = i / RATE
        p = t / duration
        if phase == "charge":
            # Integer cycles over the loop keep the seam continuous. Runtime
            # pitch follows the actual warning clock; pause freezes playback.
            frequency = (400.0, 600.0, 90.0)[weapon]
            value = .13 * (math.sin(math.tau * frequency * t)
                           + .28 * math.sin(math.tau * frequency * 2 * t))
        else:
            attack = min(1.0, t / 0.008)
            release = min(1.0, (duration - t) / 0.035)
            envelope = attack * release
            low_noise = .92 * low_noise + .08 * rng.uniform(-1, 1)
            if phase == "prepare":
                frequency = base * (1 + .65 * p)
                value = .25 * math.sin(math.tau * base * (t + .325 * t * t / duration))
                value += .06 * math.sin(math.tau * frequency * 2.01 * t)
                if weapon == 1:
                    value *= .25 + .75 * max(0, math.sin(math.tau * 3 * p))
                if weapon == 2:
                    value += .18 * low_noise
                value *= envelope * (0.60 + .40 * p)
            elif phase == "commit":
                value = (.30 * math.sin(math.tau * base * 1.6 * t)
                         + .12 * math.sin(math.tau * base * 2.4 * t))
                value *= envelope * math.exp(-p * 3)
            else:
                frequency = (190, 260, 58)[weapon]
                value = (.35 * math.sin(math.tau * frequency * t)
                         + .24 * low_noise + .08 * rng.uniform(-1, 1))
                value *= envelope * math.exp(-p * (5 if weapon != 2 else 3))
        output += struct.pack("<h", round(max(-.9, min(.9, value)) * 32767))
    return bytes(output)


if __name__ == "__main__":
    ROOT.mkdir(parents=True, exist_ok=True)
    for index, weapon in enumerate(("ordinary", "rapid", "heavy")):
        for phase in ("prepare", "commit", "fire", "charge"):
            path = ROOT / f"attack_{weapon}_{phase}.wav"
            with wave.open(str(path), "wb") as audio:
                audio.setnchannels(1)
                audio.setsampwidth(2)
                audio.setframerate(RATE)
                audio.writeframes(synth(index, phase))
            print(path.relative_to(ROOT.parent.parent))
