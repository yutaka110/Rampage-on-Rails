"""Generate original short steel brake scrapes for the vehicle audio bridge."""
from pathlib import Path
import math
import random
import struct


def write_wave(name, samples, rate=44100):
    path = Path(__file__).resolve().parents[1] / 'Resources/audio' / f'{name}.wav'
    audio_format = struct.pack('<HHIIHHH', 1, 1, rate, rate * 2, 2, 16, 0)
    chunks = b'fmt ' + struct.pack('<I', len(audio_format)) + audio_format
    chunks += b'data' + struct.pack('<I', len(samples)) + samples
    path.write_bytes(b'RIFF' + struct.pack('<I', len(chunks) + 4) + b'WAVE' + chunks)


def manual_brake():
    rng = random.Random(9032)
    for name, duration in [('bite', .17), ('loop', 1.0), ('release', .19), ('round_pass', .30)]:
        count = round(44100 * duration)
        noise = [rng.uniform(-1, 1) for _ in range(count)]
        samples = bytearray()
        for i in range(count):
            t = i / 44100
            u = t / duration
            grain = noise[i] - .52 * noise[(i - 4) % count]
            grit = .55 * grain + .45 * noise[(i - 15) % count]
            if name == 'loop':
                # Integer-cycle layers and circular noise taps preserve the texture at the seam.
                steel = math.sin(math.tau * 727 * t + .32 * math.sin(math.tau * 9 * t))
                rail = math.sin(math.tau * 1193 * t) * math.sin(math.tau * 23 * t)
                value = (.18 * grit + .095 * steel + .052 * rail) * (.83 + .17 * math.cos(math.tau * 7 * t))
            elif name == 'bite':
                value = (.30 * math.sin(math.tau * (191 * t - 212 * t * t)) * math.exp(-t * 27) +
                         (.27 * grit + .15 * math.sin(math.tau * 1256 * t)) * math.exp(-t * 42))
            elif name == 'release':
                value = (.19 * math.sin(math.tau * 340 * t) + .15 * grit) * math.exp(-t * 32)
            else:
                drop = math.sin(math.tau * (690 * t - 1190 * t * t))
                wind = math.exp(-((u - .34) / .27) ** 2)
                value = wind * (.37 * grit + .15 * drop)
            if name != 'loop':
                value *= min(1, t / .004) * min(1, (duration - t) / .042)
            samples.extend(struct.pack('<h', round(max(-.95, min(.95, value)) * 32767)))
        write_wave(f'rail_brake_{name}', samples)


def siege_fire():
    rng = random.Random(6149)
    samples = bytearray()
    previous_noise = 0.0
    for i in range(round(44100 * .39)):
        t = i / 44100
        noise = rng.uniform(-1, 1)
        crack = (.29 * (noise - .58 * previous_noise) +
                 .25 * math.sin(math.tau * (1130 * t - 2250 * t * t))) * math.exp(-t * 72)
        previous_noise = noise
        pressure = (.50 * math.sin(math.tau * (91 * t - 73 * t * t)) +
                    .24 * math.sin(math.tau * (224 * t - 250 * t * t))) * math.exp(-t * 16)
        latch_time = max(0, t - .083)
        latch = math.sin(math.tau * 401 * latch_time) * math.exp(-latch_time * 39) * .13
        value = .96 * math.tanh(1.24 * (crack + pressure + latch)) * min(1, t / .0017)
        samples.extend(struct.pack('<h', round(value * 32767)))
    write_wave('rail_siege_turret_fire', samples)


def siege_latch():
    rng = random.Random(6173)
    samples = bytearray()
    previous = 0.0
    for i in range(round(44100 * .092)):
        t = i / 44100
        noise = rng.uniform(-1, 1)
        click = .26 * (noise - .68 * previous) * math.exp(-t * 210)
        previous = noise
        stop = .44 * math.sin(math.tau * 505 * t) * math.exp(-t * 96)
        ring = .19 * math.sin(math.tau * 1437 * t) * math.exp(-t * 61)
        samples.extend(struct.pack('<h', round((click + stop + ring) * min(1, t / .001) * 32767)))
    write_wave('rail_siege_turret_latch', samples)


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
    write_wave('rail_brake_scrape', samples)
    manual_brake()
    siege_fire()
    siege_latch()


if __name__ == '__main__':
    main()
