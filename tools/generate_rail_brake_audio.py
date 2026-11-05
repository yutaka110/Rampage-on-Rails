"""Generate original short steel brake scrapes for the vehicle audio bridge."""
from pathlib import Path
import math
import random
import struct


def main():
    rate = 44100
    duration = .18
    rng = random.Random(8519)
    samples = bytearray()
    filtered = 0.0
    for index in range(round(rate * duration)):
        time = index / rate
        phase = time / duration
        noise = rng.uniform(-1, 1)
        filtered += .14 * (noise - filtered)
        grit = noise - filtered
        friction = math.sin(math.tau * (1100 * time - 700 * time * time))
        metal = math.sin(math.tau * 1830 * time) * math.sin(math.tau * 47 * time)
        envelope = min(1, time / .008) * min(1, (duration - time) / .045)
        value = (.23 * grit + .11 * friction + .07 * metal) * envelope * (1 - phase * .30)
        samples.extend(struct.pack('<h', round(value * 32767)))
    path = Path(__file__).resolve().parents[1] / 'Resources/audio/rail_brake_scrape.wav'
    # The engine's PCM loader expects the WAVEFORMATEX cbSize field.
    audio_format = struct.pack('<HHIIHHH', 1, 1, rate, rate * 2, 2, 16, 0)
    chunks = b'fmt ' + struct.pack('<I', len(audio_format)) + audio_format
    chunks += b'data' + struct.pack('<I', len(samples)) + samples
    path.write_bytes(b'RIFF' + struct.pack('<I', len(chunks) + 4) + b'WAVE' + chunks)


if __name__ == '__main__':
    main()
