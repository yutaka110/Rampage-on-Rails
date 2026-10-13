"""Original stereo rock/sand collapse, aligned to the shader's 0..3.30 s clock.

No recordings or third-party samples. Regenerate with Python and NumPy.
Emission waves match LateCollapseRocks; gravity is used to time their thuds.
"""
from pathlib import Path
import math
import struct
import numpy as np

RATE = 48000
DURATION = 3.60
rng = np.random.default_rng(101426)
count = round(RATE * DURATION)
mix = np.zeros((count, 2), dtype=np.float64)


def noise(length, low, high):
    bins = np.fft.rfftfreq(length, 1 / RATE)
    spectrum = np.fft.rfft(rng.standard_normal(length))
    shaping = (bins / np.maximum(bins + low, 1)) ** 2
    shaping /= 1 + (bins / high) ** 4
    spectrum *= shaping
    result = np.fft.irfft(spectrum, n=length)
    return result / max(np.std(result), 1e-8)


def add(start, signal, gain, pan):
    index = round(start * RATE)
    end = min(count, index + len(signal))
    if index < 0 or index >= end:
        return
    gains = np.sqrt(np.array([1 - pan, 1 + pan]) * 0.5)
    mix[index:end] += signal[:end-index, None] * gains * gain


def crack(start, gain, pan, decay=0.12, weight=1):
    length = round(RATE * (decay * 5 + 0.015))
    t = np.arange(length) / RATE
    body = noise(length, 28, 260 / weight)
    crunch = noise(length, 420, 5200) * np.exp(-t / 0.023)
    # Rough, short resonances rather than a clean ringing/UI tone.
    resonance = np.sin(2*math.pi*(87/weight)*t) * np.exp(-t/0.055) * 0.11
    signal = (body * np.exp(-t/decay) + crunch * 0.40 + resonance)
    signal *= np.minimum(t/0.003, 1) * np.minimum((t[-1]-t)/0.008, 1)
    add(start, signal, gain, pan)


def shader_hash(x, y):
    value = np.mod(np.array([x, y, x], dtype=np.float32) * np.float32(.1031), 1)
    value += np.dot(value, value[[1, 2, 0]] + np.float32(33.33))
    return float(np.mod((value[0] + value[1]) * value[2], 1))


def smooth(a, b, t):
    x = np.clip((t-a)/(b-a), 0, 1)
    return x*x*(3-2*x)


# First fracture, followed by small gritty fragments from the cave shoulder.
crack(.025, .16, -.40, .13, 1.35)
for i in range(28):
    birth = .10 + i*.023 + rng.uniform(-.009, .013)
    crack(birth, rng.uniform(.008, .019), rng.uniform(-.70, .70), .025, .65)

# Broad airborne powder is strongest while it engulfs the lens, with a soft tail.
t = np.arange(count) / RATE
powder = smooth(.06, .55, t) * (1-smooth(2.50, 3.30, t))
flow = noise(count, 180, 3600)
flow *= powder * (.70 + .30 * np.sin(t*9.1 + .4)**2)
add(0, flow, .044, -.17)
add(.017, noise(count, 230, 2900)*powder, .025, .48)
rumble = noise(count, 24, 165) * smooth(.12, .85, t) * (1-smooth(2.15, 3.25, t))
add(0, rumble, .045, 0)

# Sixteen uneven falls use the same waves/depth groups as the rendered rocks.
for i in range(16):
    seed = shader_hash(i+7, 91.4)
    depth_seed = shader_hash(i, 37.1)
    layer = i % 4
    depth = 3.5+1.3*depth_seed if layer == 0 else 9+3*depth_seed if layer == 3 else 6+2*depth_seed
    size = .34+.16*seed if layer == 0 else .23+.18*seed
    height = depth*.3650285+1.25+seed*.75
    speed = 7.6+seed*1.3 if layer == 3 else 9+seed*1.6
    birth = .85+(i//4)*.36+(i%4)*.035+seed*.07
    exit_time = (math.sqrt(speed*speed+19.6*(height+depth*.3650285+size*1.8))-speed)/9.8
    lane = ((i*5)%16 + .18+.64*shader_hash(i, 15.7))/16
    pan = (lane-.5)*1.35
    # Fracture on release; weighty contact as the piece passes below the lens.
    crack(birth+.06, .031 if layer == 0 else .019, pan, .065, 1.1)
    landing = birth+exit_time*.88
    gain = (.065 if layer == 0 else .033) * (1-smooth(3.0, 3.30, landing))
    crack(landing, gain, pan, .12, 1.6 if layer == 0 else 1.15)

# Short diffuse cave reflections share the same clock and decay with the powder.
dry = mix.copy()
for delay, gain in [(0.031,.13), (.067,.10), (.113,.075), (.181,.055), (.277,.028)]:
    shift = round(delay*RATE)
    mix[shift:] += dry[:-shift, ::-1] * gain
mix -= mix.mean(axis=0)
mix *= (smooth(0, .008, t) * (1-smooth(3.35, DURATION, t)))[:, None]
peak = np.max(np.abs(mix))
mix *= .78 / max(peak, 1e-8)
pcm = np.round(mix*32767).astype('<i2').tobytes()

target = Path(__file__).resolve().parents[1] / 'Resources/audio/rail_sand_transition.wav'
target.parent.mkdir(parents=True, exist_ok=True)
# WAVEFORMATEX (18 bytes) loads directly in the engine's RIFF reader.
fmt = struct.pack('<HHIIHHH', 1, 2, RATE, RATE*4, 4, 16, 0)
riff = b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(pcm)) + pcm
target.write_bytes(b'RIFF' + struct.pack('<I', len(riff)+4) + b'WAVE' + riff)
print(f'{target.name}: {DURATION:.2f}s stereo, peak={np.max(np.abs(mix)):.3f}, RMS={np.sqrt(np.mean(mix*mix)):.3f}')
