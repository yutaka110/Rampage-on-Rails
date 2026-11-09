"""Build the two player-cart impact one-shots."""
import math
import random
import struct

from generate_rail_brake_audio import write_wave


def impact(heavy):
    duration = .26 if heavy else .128
    rng = random.Random(8181 if heavy else 2423)
    pcm = bytearray()
    previous = 0.0
    for i in range(round(44100 * duration)):
        t = i / 44100
        noise = rng.uniform(-1, 1)
        crack = .28 * (noise - .60 * previous) * math.exp(-t * (105 if heavy else 140))
        previous = noise
        frequency = 92 if heavy else 281
        body = (.70 if heavy else .48) * math.sin(math.tau * (frequency * t - 105 * t * t)) * math.exp(-t * (24 if heavy else 53))
        plate = (.22 * math.sin(math.tau * 1067 * t) + .11 * math.sin(math.tau * 1787 * t)) * math.exp(-t * (27 if heavy else 58))
        tail = max(0.0, t - .047)
        rattle = .17 * math.sin(math.tau * 398 * tail) * math.exp(-tail * 28) if heavy else 0
        edge = min(1.0, t / .0015, (duration - t) / .032)
        pcm.extend(struct.pack('<h', round(.89 * math.tanh(1.3 * (crack + body + plate + rattle)) * edge * 32767)))
    return pcm


if __name__ == '__main__':
    write_wave('rail_cart_projectile_hit', impact(False))
    write_wave('rail_cart_heavy_hit', impact(True))
