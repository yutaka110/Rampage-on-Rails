"""Generate the original, deterministic short drone core fracture sound."""
from pathlib import Path
import wave
import numpy as np


def main():
    rate = 48000
    t = np.arange(int(rate * 0.14), dtype=np.float64) / rate
    rng = np.random.default_rng(20261020)
    noise = rng.standard_normal(len(t))
    high = noise - np.concatenate(([0.0], noise[:-1]))
    attack = 1.0 - np.exp(-t * 2400.0)
    crack = high * np.exp(-t * 135.0) * 0.38
    metal = sum(np.sin(2 * np.pi * frequency * t + phase) * weight
                for frequency, phase, weight in [(1730, 0.0, 0.24), (2697, 0.7, 0.17), (4331, 1.8, 0.09)])
    metal *= np.exp(-t * 48.0)
    body = np.sin(2 * np.pi * (180 * t - 200 * t * t)) * np.exp(-t * 55.0) * 0.25
    tail = np.clip((0.14 - t) / 0.025, 0.0, 1.0) ** 2
    samples = (crack + metal + body) * attack * tail
    samples *= 0.78 / max(float(np.abs(samples).max()), 0.001)
    samples = np.clip(samples, -1, 1)
    target = Path(__file__).resolve().parents[1] / 'Resources/audio/drone_core_break.wav'
    with wave.open(str(target), 'wb') as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes((samples * 32767).astype('<i2').tobytes())
    print(f'{target}: {len(samples) / rate:.2f}s, peak={float(np.abs(samples).max()):.2f}')


if __name__ == '__main__':
    main()
