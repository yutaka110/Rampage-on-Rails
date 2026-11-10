"""Original short pulse-cannon shot; no sampled game audio."""
from pathlib import Path
import wave
import numpy as np


def main():
    rate = 48000
    duration = .052
    t = np.arange(int(rate * duration), dtype=np.float64) / rate
    noise = np.random.default_rng(20261020).standard_normal(len(t))
    attack = 1.0 - np.exp(-t * 3000)
    snap = (noise - np.roll(noise, 1)) * np.exp(-t * 330) * .065
    phase = 2 * np.pi * (2650 * t - 24000 * t * t)
    chirp = (np.sin(phase) + .18 * np.sin(phase * 1.88)) * np.exp(-t * 114) * .53
    second_barrel = np.maximum(0, t - .0036)
    echo_phase = 2 * np.pi * (2260 * second_barrel - 18000 * second_barrel * second_barrel)
    chirp += .26 * np.sin(echo_phase) * np.exp(-second_barrel * 126) * (1 - np.exp(-second_barrel * 2450))
    body = np.sin(2 * np.pi * (410 * t - 3600 * t * t)) * np.exp(-t * 136) * .15
    tail = np.clip((duration - t) / .016, 0, 1) ** 2
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
