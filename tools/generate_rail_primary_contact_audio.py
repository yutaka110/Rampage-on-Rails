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


def lock_cue(duration, render):
    rng = random.Random(1541)
    samples = bytearray()
    for i in range(round(SAMPLE_RATE * duration)):
        t = i / SAMPLE_RATE
        edge = min(1.0, t/.001, (duration-t)/.018)
        value = render(t, rng.uniform(-1.0, 1.0))
        samples.extend(struct.pack('<h', round(.90 * math.tanh(1.50*value) * edge * 32767)))
    return samples


def lock_confirm(t, _):
    echo = max(0.0, t-.022)
    click = .38 * math.sin(math.tau*1350*t) * math.exp(-115*t)
    chime = .54 * math.sin(math.tau*2030*echo) * math.exp(-90*echo) * min(1.0, echo/.003)
    return click+chime


def lock_launch(t, noise):
    charge = math.tau*(475*t+10200*t*t)
    snap = .21*noise*math.exp(-195*t)
    body = .49*math.sin(charge)*math.exp(-36*t)
    overtone = .22*math.sin(charge*2.07)*math.exp(-53*t)
    return snap+body+overtone


def lock_flight(t, _):
    sweep = math.tau*(1960*t-5600*t*t)
    envelope = min(1.0, t/.016) * math.exp(-23*t)
    return (.42*math.sin(sweep)+.17*math.sin(sweep*.73))*envelope


def lock_contact(t, noise):
    crack = .29*noise*math.exp(-160*t)
    punch = .62*math.sin(math.tau*(196*t-390*t*t))*math.exp(-29*t)
    echo = max(0.0, t-.018)
    ring = (.32*math.sin(math.tau*(1160*echo-3100*echo*echo))
            + .17*math.sin(math.tau*1733*echo))*math.exp(-34*echo)*min(1.0, echo/.005)
    return crack+punch+ring


if __name__ == '__main__':
    for material, plated in [('hull', False), ('plate', True)]:
        write_wave(f'rail_primary_{material}_contact', contact_transient(plated))
        write_wave(f'rail_primary_{material}_resonance', contact_resonance(plated))
    for name, duration, render in [('confirm', .071, lock_confirm), ('launch', .134, lock_launch),
                                    ('flight', .108, lock_flight), ('contact', .188, lock_contact)]:
        write_wave(f'rail_lock_{name}', lock_cue(duration, render))
