"""Original elite weapon audio and independently recoiling Fang barrels."""
from pathlib import Path
import math
import random
import struct
import wave
from generate_elite_pursuers import Mesh, fighter, DEST

ROOT=Path(__file__).resolve().parents[1]
RATE=44100


def audio(name, seconds):
    rng=random.Random(1763+sum(map(ord,name)))
    samples=[];low=0.;phase=0.
    for i in range(round(seconds*RATE)):
        t=i/RATE;p=t/seconds;noise=rng.uniform(-1,1);low=low*.94+noise*.06
        if name=='veil_charge':
            # Integer harmonics make a seamless low-pressure loop.
            value=.22*math.sin(math.tau*80*t)+.07*math.sin(math.tau*160*t)+.03*math.sin(math.tau*320*t)
        else:
            heavy=name=='veil_fire';shock=name=='veil_shock'
            frequency=(130 if not heavy and not shock else 75)*math.exp(-t*(12 if not heavy else 5))+32
            phase+=math.tau*frequency/RATE
            attack=min(1,t/.0025);release=min(1,(seconds-t)/.035)
            body=math.sin(phase)*math.exp(-t*(23 if not heavy and not shock else 8))
            crack=noise*math.exp(-t*(85 if not heavy else 110))
            air=low*math.exp(-t*5)*(1-math.exp(-t*70))
            value=attack*release*(.42*body+.24*crack+(.45 if heavy else .24)*air)
            if heavy:value+=.08*math.sin(math.tau*52*t)*math.exp(-t*4)*attack*release
        samples.append(math.tanh(value*1.15)*.72)
    path=ROOT/'Resources/audio'/f'elite_{name}.wav'
    with wave.open(str(path),'wb') as wav:
        wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(RATE)
        wav.writeframes(b''.join(struct.pack('<h',round(v*32767)) for v in samples))
    print(path.relative_to(ROOT),'peak',round(max(abs(x) for x in samples),3))


def main():
    for state,suffix in [('fitted',''),('exposed','Exposed'),('damaged','Damaged')]:
        source=fighter(True,state);hull=Mesh();barrels=Mesh()
        for face in source.faces:
            (barrels if face[0].endswith('_cannon') else hull).faces.append(face)
        wound=('starboard_cannon_housing',1.02,.22,.92,.27) if state=='exposed' else None
        hull.write(DEST/f'FangFireHull{suffix}.obj',wound)
        barrels.write(DEST/f'FangFireBarrels{suffix}.obj')
    for name,seconds in [('fang_fire',.20),('veil_fire',.72),('veil_shock',.36),('veil_charge',.40)]:audio(name,seconds)


if __name__=='__main__':main()
