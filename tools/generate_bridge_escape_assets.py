"""Original closed steel meshes and deterministic layered foley for bridge escape."""
from pathlib import Path
from math import sin,cos,pi,exp
import random,struct,wave
from generate_elite_pursuers import Mesh,section,DEST

def box(m,name,x,y,z,w,h,l,material):
    m.loft(name,[section(z-l/2,x,y,w,h),section(z+l/2,x,y,w,h)],material)

def bridge_deck(length):
    m=Mesh();box(m,'deck',0,-.70,0,5.5,.35,length,'armour')
    for i in range(int(length/1.25)):
        box(m,'rail_tie',0,-.20,-length/2+.625+i*1.25,4.4,.07,.22,'joint')
    for side in (-1,1):
        box(m,'rail_head',side*1.9,-.04,0,.13,.13,length,'edge')
        box(m,'truss_chord',side*4.8,-2.35,0,.18,.22,length,'gunmetal')
        box(m,'hand_rail',side*5.1,1.12,0,.075,.075,length,'ivory')
        for i in range(int(length/5)):
            z=-length/2+2.5+i*5
            box(m,'guard_post',side*5.1,.35,z,.065,.75,.065,'ivory')
            box(m,'truss_post',side*4.8,-1.4,z,.16,.82,.16,'armour')
    return m

def main():
    # Imported OBJ Z is reversed by the renderer. The ramp extends forward -Z.
    m=Mesh()
    m.loft('deck',[section(-float(i),0,6*(i/40)**2-.50,5.5,.45) for i in range(41)],'armour')
    for side in (-1,1):
        m.loft('running_rail',[section(-float(i),side*1.9,6*(i/40)**2-.05,.12,.14) for i in range(41)],'edge')
    for i in range(40):
        a,b=float(i),float(i+1)
        ya,yb=6*(a/40)**2,6*(b/40)**2
        if i%3==0:box(m,'cross_tie',0,ya-.22,-a,4.4,.12,.28,'joint')
        if i%5==0:
            for side in (-1,1):box(m,'safety_mark',side*5.0,ya+.02,-a,.25,.035,.6,'ivory')
    m.write(DEST/'BridgeEscapeRamp.obj')
    m=Mesh()
    box(m,'steel_back',0,0,0,.82,.88,.40,'joint')
    box(m,'release_housing',0,0,-.1,.64,.72,.40,'armour')
    box(m,'release_lens',0,0,.36,.40,.40,.12,'sensor')
    for side in (-1,1):
        box(m,'ivory_grips',side*.63,0,.30,.12,.7,.14,'ivory')
        box(m,'amber_lock',0,side*.67,.31,.42,.1,.12,'copper')
    m.write(DEST/'BridgeEscapeClamp.obj')
    m=Mesh();box(m,'flange_top',0,.36,0,2.8,.09,1.1,'edge');box(m,'web',0,0,0,2.8,.3,.16,'armour');box(m,'flange_base',0,-.36,0,2.8,.09,1.1,'edge')
    m.write(DEST/'BridgeEscapeDebris.obj')
    bridge_deck(50).write(DEST/'BridgeEscapeSpan.obj')
    bridge_deck(5).write(DEST/'BridgeEscapeLeaf.obj')
    m=Mesh();box(m,'tension_member',0,0,0,1,1,1,'copper');m.write(DEST/'BridgeEscapeLink.obj')
    m=Mesh();box(m,'anchor_foot',.6,-.65,0,1.65,.38,1.8,'gunmetal')
    box(m,'pin_pylon',0,2.8,0,.40,3.35,.45,'armour')
    box(m,'pin_crown',0,6.45,0,.80,.17,.60,'ivory')
    for side in (-1,1):box(m,'anchor_bolt',side*.72,-.15,.60,.11,.10,.12,'copper')
    m.write(DEST/'BridgeEscapePylon.obj')
    audio=DEST.parents[1]/'audio';audio.mkdir(exist_ok=True)
    rng=random.Random(63051)
    for name,duration in [('bridge_escape_break',1.55),('bridge_escape_release',.72),('bridge_escape_land',.8),
                          ('bridge_escape_charge',1.9),('bridge_escape_cannon',1.6),('bridge_escape_tear',1.8)]:
        samples=[];lo=0
        for i in range(int(44100*duration)):
            t=i/44100;noise=rng.uniform(-1,1);lo=lo*.96+noise*.04
            if name.endswith('charge'):
                q=min(1,t/1.7);env=min(1,t/.12)*(.12+.65*q*q)
                value=env*(.23*sin(2*pi*(70*t+90*t*t))+.11*sin(2*pi*(390*t+300*t*t))+.14*lo)
                if t>1.56:value*=max(0,(1.7-t)/.14)
            elif name.endswith('cannon'):
                value=.38*noise*exp(-t*38)+.48*sin(2*pi*(58*t-8*t*t))*exp(-t*5)+.7*lo*exp(-t*3)
                value+=.12*sin(2*pi*1750*t)*exp(-t*27)
            elif name.endswith('tear'):
                value=.6*lo*exp(-t*2.8)+.13*sin(2*pi*(170*t-18*t*t))*exp(-t*4)
                for hit in (0,.16,.34,.58):
                    if t>=hit:value+=(.17*sin(2*pi*742*(t-hit))+.12*noise)*exp(-(t-hit)*22)
            elif name.endswith('break'):
                value=.6*lo*exp(-t*2)+.3*noise*exp(-t*12)+.17*sin(2*pi*(55*t-6*t*t))*exp(-t*4)
                for hit in (.06,.19,.31):
                    if t>=hit:value+=.21*sin(2*pi*810*(t-hit))*exp(-(t-hit)*36)
            elif name.endswith('release'):
                value=.3*noise*exp(-t*24)+.19*(sin(2*pi*640*t)+.4*sin(2*pi*1630*t))*exp(-t*8)+.3*lo*exp(-t*4)
            else:value=.65*lo*exp(-t*9)+.25*sin(2*pi*72*t)*exp(-t*12)+.17*noise*exp(-t*25)
            value*=min(1,(duration-t)/.07)
            samples.append(struct.pack('<h',int(max(-.95,min(.95,value))*32767)))
        with wave.open(str(audio/(name+'.wav')),'wb') as out:
            out.setparams((1,2,44100,0,'NONE','not compressed'));out.writeframes(b''.join(samples))

if __name__=='__main__':main()
