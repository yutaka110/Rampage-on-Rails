"""Original short propulsion ignition; deterministic synthesis, no samples."""
from pathlib import Path
import wave
import numpy as np


def main():
    rate, duration = 48000, .34
    t = np.arange(int(rate * duration), dtype=np.float64) / rate
    noise = np.random.default_rng(20261021).standard_normal(len(t))
    low = np.convolve(noise, np.ones(37) / 37, mode='same')
    air = np.convolve(noise, np.ones(5) / 5, mode='same') - low
    # Low mechanical engagement -> rising turbine -> short pressure release.
    phase = 2 * np.pi * (85 * t + 210 * t * t)
    motor = (np.sin(phase) + .22 * np.sin(phase * 2.03)) * np.exp(-t * 9)
    latch = np.sin(2 * np.pi * 430 * t) * np.exp(-t * 80)
    rush = air * (1 - np.exp(-t * 55)) * np.exp(-t * 13)
    envelope = (1 - np.exp(-t * 900)) * np.clip((duration - t) / .08, 0, 1) ** 2
    samples = (.66 * motor + .14 * latch + .65 * rush + .4 * low * np.exp(-t * 12)) * envelope
    samples *= .78 / max(float(np.abs(samples).max()), .001)
    target = Path(__file__).resolve().parents[1] / 'Resources/audio/rail_boost_ignition.wav'
    with wave.open(str(target), 'wb') as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes((samples * 32767).astype('<i2').tobytes())
    print(f'{target.name}: {len(samples)/rate:.3f}s, peak={np.abs(samples).max():.3f}')


if __name__ == '__main__':
    main()
