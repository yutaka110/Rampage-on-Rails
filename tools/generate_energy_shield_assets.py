"""Original deterministic emitter geometry and short shield audio cues."""
from pathlib import Path
import math,random,struct,wave
from generate_submission_hazards import Mesh,ROOT
from generate_siege_turret import box

def main():
    mesh=Mesh('EnergyShieldEmitter',[(48,54,55),(87,96,97),(24,30,32),(85,74,49),(60,118,128)])
    box(mesh,(0,0,0),(.28,.67,.32),0)
    box(mesh,(0,0,.17),(.18,.47,.06),2)
    box(mesh,(0,.32,.025),(.33,.10,.36),1)
    box(mesh,(0,-.32,.025),(.33,.10,.36),1)
    box(mesh,(0,0,.205),(.10,.36,.025),4)
    box(mesh,(0,0,-.15),(.53,.17,.20),3)
    mesh.save(ROOT/'Resources/course_meshes/EnergyShieldEmitter')
    rng=random.Random(7721);rate=24000
    for name,duration in [('hit',.16),('break',.42)]:
        data=[]
        for n in range(int(rate*duration)):
            t=n/rate;p=t/duration
            attack=min(1,t/.003)
            if name=='hit':
                value=(.55*math.sin(2*math.pi*(920*t-950*t*t))+.22*math.sin(2*math.pi*1770*t)+rng.uniform(-.12,.12))*math.exp(-p*7)
            else:
                value=(rng.uniform(-.75,.75)*math.exp(-p*6)+.28*math.sin(2*math.pi*(610*t-500*t*t))*math.exp(-p*4))*(1-p)
            data.append(struct.pack('<h',int(max(-1,min(1,value*attack))*.65*32767)))
        with wave.open(str(ROOT/f'Resources/audio/rail_energy_shield_{name}.wav'),'wb') as out:
            out.setnchannels(1);out.setsampwidth(2);out.setframerate(rate);out.writeframes(b''.join(data))

if __name__=='__main__':main()
