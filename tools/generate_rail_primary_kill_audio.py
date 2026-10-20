"""Original internal core blast followed by a 60ms delayed armor fracture."""
from pathlib import Path
import wave
import numpy as np


def main():
    rate = 48000
    duration = .24
    t = np.arange(int(rate * duration), dtype=np.float64) / rate
    noise = np.random.default_rng(20261021).standard_normal(len(t))
    high = noise - np.concatenate(([0.0], noise[:-1]))
    attack = 1 - np.exp(-t * 3600)
    snap = high * np.exp(-t * 190) * 0.17
    metal = sum(weight * np.sin(2 * np.pi * frequency * t + phase)
                for frequency, weight, phase in [(1670, .22, 0), (2843, .15, .4), (4529, .07, 1.2)])
    metal *= np.exp(-t * 35) * .65
    body = np.sin(2 * np.pi * (110 * t - 130 * t * t)) * np.exp(-t * 26) * .58
    # A short, band-limited pressure rush gives the rupture mass without a
    # long explosion tail that would mask continuous fire and later kills.
    low_noise = np.convolve(noise, np.ones(23) / 23, mode='same')
    blast = low_noise * (1 - np.exp(-t * 800)) * np.exp(-t * 24) * .85
    edge = np.sin(2 * np.pi * (2350 * t - 5200 * t * t)) * np.exp(-t * 60) * .08
    fracture_t = np.maximum(t - .060, 0)
    fracture = (high * .11 + np.sin(2 * np.pi * 3200 * fracture_t) * .13)
    fracture *= (t >= .060) * (1 - np.exp(-fracture_t * 4000)) * np.exp(-fracture_t * 100)
    tail = np.clip((duration - t) / .055, 0, 1) ** 2
    samples = (snap + metal + body + blast + edge + fracture) * attack * tail
    samples *= .80 / max(float(np.abs(samples).max()), .001)
    target = Path(__file__).resolve().parents[1] / 'Resources/audio/rail_primary_kill.wav'
    with wave.open(str(target), 'wb') as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes((samples * 32767).astype('<i2').tobytes())
    print(f'{target.name}: {len(samples)/rate:.3f}s')


if __name__ == '__main__':
    main()
