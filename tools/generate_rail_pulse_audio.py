"""Original short pulse-cannon shot; no sampled game audio."""
from pathlib import Path
import wave
import numpy as np


def main():
    rate = 48000
    t = np.arange(int(rate * 0.065), dtype=np.float64) / rate
    noise = np.random.default_rng(20261020).standard_normal(len(t))
    attack = 1.0 - np.exp(-t * 3000)
    snap = (noise - np.roll(noise, 1)) * np.exp(-t * 240) * 0.10
    chirp = np.sin(2 * np.pi * (1200 * t - 6800 * t * t)) * np.exp(-t * 72) * 0.48
    body = np.sin(2 * np.pi * (190 * t - 650 * t * t)) * np.exp(-t * 88) * 0.28
    tail = np.clip((0.065 - t) / 0.018, 0, 1) ** 2
    samples = (snap + chirp + body) * attack * tail
    samples *= 0.72 / max(float(np.abs(samples).max()), 0.001)
    target = Path(__file__).resolve().parents[1] / 'Resources/audio/rail_pulse_fire.wav'
    with wave.open(str(target), 'wb') as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes((samples * 32767).astype('<i2').tobytes())
    print(f'{target.name}: {len(samples) / rate:.3f}s')


if __name__ == '__main__':
    main()
