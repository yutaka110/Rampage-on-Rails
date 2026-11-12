"""Render the rockfall and carrier combat sounds as original mono PCM."""

import math
import random
import struct

from generate_rail_brake_audio import write_wave


RATE = 44100


def pulse(time, attack, decay):
    return min(1.0, time / attack) * math.exp(-time * decay) if time >= 0 else 0.0


def tone(frequency, time, bend=0.0):
    return math.sin(math.tau * (frequency * time + bend * time * time))


def rock_crack(t, low, grit, edge):
    return (1.75 * edge + .45 * tone(202, t, -350)) * pulse(t, .001, 36) + (
        1.5 * grit + .34 * tone(144, t - .061, -190)
    ) * pulse(t - .061, .002, 31) + grit * pulse(t - .145, .006, 19)


def rock_land(t, low, grit, edge):
    body = (.54 * tone(82, t, -32) + .26 * tone(127, t, -57)) * pulse(t, .004, 9)
    roll = 2.5 * low * pulse(t - .018, .030, 3.9)
    gravel = sum(grit * gain * pulse(t - delay, .004, decay) for delay, gain, decay in (
        (.035, 1.20, 18), (.108, .90, 20), (.202, .64, 17), (.315, .48, 22), (.438, .36, 16)
    ))
    return body + roll + gravel + .64 * edge * pulse(t, .003, 36)


def intercept(t, low, grit, edge):
    snap = (1.4 * edge + .18 * tone(255, t, -720)) * pulse(t, .001, 65)
    # A fast upward sweep and a distinct second, shorter release.
    rise = (.44 * tone(880, t, 6900) + .18 * tone(1570, t, 10400)) * pulse(t, .005, 20)
    release = t - .076
    ring = (.33 * tone(2470, release, -3600) + .21 * tone(1740, release, -1900) +
            .38 * grit) * pulse(release, .003, 26)
    return snap + rise + ring


def battery_hit(t, low, grit, edge):
    strike = (.48 * tone(174, t, -222) + .92 * edge) * pulse(t, .001, 51)
    plate = sum(gain * tone(freq, t, -freq * .11) * pulse(t, .002, decay)
                for freq, gain, decay in ((396, .40, 11), (681, .31, 13),
                                          (1103, .24, 18), (1712, .19, 24)))
    mechanism = (.33 * tone(216, t - .083, -285) + .95 * grit) * pulse(t - .083, .003, 25)
    return strike + plate + mechanism


def render(name, duration, seed, source):
    rng = random.Random(seed)
    low = mid = high = 0.0
    samples = bytearray()
    for index in range(round(duration * RATE)):
        t = index / RATE
        noise = rng.uniform(-1, 1)
        low += .023 * (noise - low)
        mid += .17 * (noise - mid)
        high += .72 * (noise - high)
        value = source(t, low, mid - low, high - mid)
        fade = min(1.0, (duration - t) / .035)
        samples.extend(struct.pack("<h", round(29000 * math.tanh(1.35 * value) * fade)))
    write_wave(name, samples, RATE)


if __name__ == "__main__":
    render("rail_arch_crack", .32, 281, rock_crack)
    render("rail_arch_land", .84, 718, rock_land)
    render("rail_carrier_intercept", .28, 546, intercept)
    render("rail_carrier_battery_hit", .40, 994, battery_hit)
