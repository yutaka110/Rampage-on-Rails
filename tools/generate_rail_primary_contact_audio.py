"""Build short, original impact sounds for the pulse and lock weapons."""
import math
import random
import struct

from generate_rail_brake_audio import write_wave


SAMPLE_RATE = 44100


def contact_transient(plated):
    duration = .038 if plated else .043
    rng = random.Random(703 if plated else 906)
    samples = bytearray()
    fast = slow = 0.0
    for i in range(round(SAMPLE_RATE * duration)):
        t = i / SAMPLE_RATE
        noise = rng.uniform(-1.0, 1.0)
        fast += .54 * (noise - fast)
        slow += .09 * (noise - slow)
        grit = (fast - slow) * (1.26 if plated else 1.06) * math.exp(-245*t)
        frequency = 1130 if plated else 332
        strike = .63 * math.sin(math.tau*(frequency*t - 3100*t*t)) * math.exp(-174*t)
        flex = .26 * math.sin(math.tau*(159*t if not plated else 1783*t)) * math.exp(-142*t)
        edge = min(1.0, t/.00040, (duration-t)/.008)
        samples.extend(struct.pack('<h', round(.94 * math.tanh(1.55*(grit+strike+flex)) * edge * 32767)))
    return samples


def contact_resonance(plated):
    # Inharmonic bending modes model a struck sheet. The glancing shot excites
    # the upper modes again with a quieter, sliding second contact.
    modes = ((887, .31, 49), (1439, .29, 44), (2231, .26, 47), (3467, .17, 67), (4913, .10, 97)) if plated else (
        (232, .40, 36), (371, .28, 43), (639, .21, 48), (1021, .18, 57), (1897, .12, 76))
    duration = .142 if plated else .127
    onset = .004 if plated else .005
    rng = random.Random(773 if plated else 927)
    phases = [rng.uniform(0.0, math.tau) for _ in modes]
    samples = bytearray()
    for i in range(round(SAMPLE_RATE * duration)):
        t = i / SAMPLE_RATE
        ring_time = max(0.0, t-onset)
        ring = sum(amp*math.sin(math.tau*freq*ring_time+phase)*math.exp(-damping*ring_time)
                   for (freq, amp, damping), phase in zip(modes, phases))
        ring *= min(1.0, ring_time/.0038)
        scrape_time = max(0.0, t-(.014 if plated else .009))
        scrape = math.sin(math.tau*((3440 if plated else 1520)*scrape_time - 24700*scrape_time**2))
        scrape *= (.35 if plated else .13) * min(1.0, scrape_time/.002) * math.exp(-89*scrape_time)
        edge = min(1.0, (duration-t)/.018)
        samples.extend(struct.pack('<h', round(.90 * math.tanh(1.38*(ring+scrape)) * edge * 32767)))
    return samples


def lock_contact():
    duration = .164
    rng = random.Random(1541)
    samples = bytearray()
    for i in range(round(44100 * duration)):
        t = i / 44100
        crack = .24 * rng.uniform(-1, 1) * math.exp(-t * 115)
        punch = .54 * math.sin(math.tau * (390*t - 870*t*t)) * math.exp(-t * 39)
        echo = max(0.0, t - .014)
        ring = .23 * math.sin(math.tau * (1870*echo - 5300*echo*echo)) * math.exp(-echo * 44)
        ring *= min(1.0, echo / .004)
        edge = min(1.0, t / .001, (duration - t) / .022)
        samples.extend(struct.pack('<h', round(.88 * math.tanh(1.4 * (crack+punch+ring)) * edge * 32767)))
    return samples


if __name__ == '__main__':
    for material, plated in [('hull', False), ('plate', True)]:
        write_wave(f'rail_primary_{material}_contact', contact_transient(plated))
        write_wave(f'rail_primary_{material}_resonance', contact_resonance(plated))
    write_wave('rail_lock_contact', lock_contact())
