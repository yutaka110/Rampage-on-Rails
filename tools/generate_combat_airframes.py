"""Original production airframes. Author in runtime coordinates (-Z nose).

Material bands in UV.x select paint, frame, steel, lens, intake, trim,
exhaust and dusty undersides in Object3D's airframe material. No downloaded
geometry or bitmap dependency. Each mechanical component is a closed solid.
"""
from pathlib import Path
import collections
import json
import math

ROOT = Path(__file__).resolve().parents[1]
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def unit(a):
    n=math.sqrt(dot(a,a)); assert n>1e-10
    return tuple(x/n for x in a)
def octagon(x,y,chamfer):
    c=chamfer
    return [(-x+c,-y),(x-c,-y),(x,-y+c),(x,y-c),(x-c,y),(-x+c,y),(-x,y-c),(-x,-y+c)]

class Mesh:
    def __init__(self): self.parts=[]
    def solid(self,name,faces,band,center):
        oriented=[]
        for points in faces:
            n=cross(sub(points[1],points[0]),sub(points[2],points[0]))
            middle=tuple(sum(p[j] for p in points)/len(points) for j in range(3))
            if dot(n,sub(middle,center))<0:points=list(reversed(points))
            oriented.append((points,band))
        self.parts.append((name,oriented))
    def loft(self,name,sections,band):
        # sections: (z, centerX, centerY, halfX, halfY, corner bevel)
        rings=[[(x+cx,y+cy,z) for x,y in octagon(a,b,c)] for z,cx,cy,a,b,c in sections]
        faces=[rings[0],rings[-1]]
        for a,b in zip(rings,rings[1:]):
            for i in range(8):faces.append([a[i],a[(i+1)%8],b[(i+1)%8],b[i]])
        center=tuple(sum(p[j] for r in rings for p in r)/(8*len(rings)) for j in range(3))
        self.solid(name,faces,band,center)
    def box(self,name,c,s,band,bevel=.06):
        x,y,z=c;a,b,d=s;v=min(bevel,a*.35,b*.35,d*.35)
        self.loft(name,[(z-d,x,y,a-v*.5,b-v*.5,v),(z-d+v,x,y,a,b,v),
            (z+d-v,x,y,a,b,v),(z+d,x,y,a-v*.5,b-v*.5,v)],band)
    def tube(self,name,c,z0,z1,outer,inner,band,n=24):
        x,y=c
        def ring(r,z):return [(x+r*math.cos(i*math.tau/n),y+r*math.sin(i*math.tau/n),z) for i in range(n)]
        a,b,ia,ib=ring(outer,z0),ring(outer,z1),ring(inner,z0),ring(inner,z1)
        faces=[]
        for i in range(n):
            j=(i+1)%n
            faces += [([a[i],a[j],b[j],b[i]],band),([ia[j],ia[i],ib[i],ib[j]],4),
                ([a[j],a[i],ia[i],ia[j]],band),([b[i],b[j],ib[j],ib[i]],band)]
        self.parts.append((name,faces))
    def disc(self,name,c,r,depth,band,n=24):
        x,y,z=c
        shape=[(x+r*math.cos(i*math.tau/n),y+r*math.sin(i*math.tau/n)) for i in range(n)]
        a=[(x,y,z-depth) for x,y in shape];b=[(x,y,z+depth) for x,y in shape]
        self.solid(name,[a,b]+[[a[i],a[(i+1)%n],b[(i+1)%n],b[i]] for i in range(n)],band,c)
    def wing(self,name,side,band=0,inset=0):
        poly=[(.36,.18),(1.72,-.91),(1.86,-.57),(.65,.77)]
        if inset:poly=[(.51,.18),(1.60,-.72),(1.67,-.59),(.70,.59)]
        def point(x,z,y):return (side*x,y+.13*(x-.36)+.16*z,z)
        thick=.075 if not inset else .016
        lift=0 if not inset else .076
        a=[point(x,z,lift-thick) for x,z in poly];b=[point(x,z,lift+thick) for x,z in poly]
        center=tuple(sum(p[j] for p in a+b)/8 for j in range(3))
        self.solid(name,[a,b]+[[a[i],a[(i+1)%4],b[(i+1)%4],b[i]] for i in range(4)],band,center)
    def save(self,folder,filename):
        out=ROOT/'Resources/enemies'/folder;out.mkdir(parents=True,exist_ok=True)
        lines=['# Original airframe; Z/winding mirrored for Assimp ConvertToLeftHanded.', 'o '+filename[:-4]]
        next_index=1;total=0;audits=[]
        for name,faces in self.parts:
            lines.append('g '+name);edges=collections.Counter();volume=0;count=0
            for points,band in faces:
                n=unit(cross(sub(points[1],points[0]),sub(points[2],points[0])))
                axis=max(range(3),key=lambda j:abs(n[j]));axes=[j for j in range(3) if j!=axis]
                low=[min(p[j] for p in points) for j in axes];high=[max(p[j] for p in points) for j in axes]
                for p in points:lines.append('v %.7f %.7f %.7f'%(p[0],p[1],-p[2]))
                for p in points:
                    uv=[(p[j]-a)/max(b-a,1e-8) for j,a,b in zip(axes,low,high)]
                    lines.append('vt %.7f %.7f'%(band*4+.125+uv[0]*.75,.125+uv[1]*.75))
                for _ in points:lines.append('vn %.7f %.7f %.7f'%(n[0],n[1],-n[2]))
                for i in range(1,len(points)-1):
                    tri=[points[0],points[i],points[i+1]];volume+=dot(tri[0],cross(tri[1],tri[2]))/6
                    for a,b in zip(tri,tri[1:]+tri[:1]):
                        key=lambda p:tuple(round(x,6) for x in p)
                        edges[tuple(sorted((key(a),key(b))))]+=1
                    ids=[next_index,next_index+i+1,next_index+i]
                    lines.append('f '+' '.join(f'{j}/{j}/{j}' for j in ids));count+=1
                next_index+=len(points)
            assert volume>1e-9,(name,volume)
            assert all(v==2 for v in edges.values()),(name,collections.Counter(edges.values()))
            audits.append(dict(component=name,triangles=count,volume=volume,closed=True));total+=count
        (out/filename).write_text('\n'.join(lines)+'\n',encoding='ascii')
        (out/'airframe-audit.json').write_text(json.dumps(dict(triangles=total,components=audits),indent=2),encoding='utf-8')
        print(folder,':',total,'triangles,',len(audits),'closed components')

def sniper():
    m=Mesh()
    m.loft('pressure_hull',[(-.45,0,0,.48,.30,.12),(-.25,0,0,.64,.40,.15),(.58,0,0,.55,.35,.13),(.79,0,0,.37,.26,.10)],1)
    for side in [-1,1]:
        m.box(f'armour_{side}',(side*.83,.09,.12),(.48,.46,.60),0,.13)
        m.box(f'cheek_frame_{side}',(side*.78,-.30,-.22),(.37,.13,.36),2,.06)
        m.box(f'unit_stripe_{side}',(side*.97,.11,-.486),(.085,.26,.025),5,.018)
        for i in range(3):m.box(f'vent_{side}_{i}',(side*.80,-.16+i*.105,-.50),(.14,.024,.035),4,.012)
        m.tube(f'hover_motor_{side}',(side*.73,-.38),.06,.91,.235,.17,2)
        m.disc(f'hover_hot_{side}',(side*.73,-.38,.88),.12,.015,6)
        m.box(f'upper_fin_{side}',(side*1.04,.58,.33),(.22,.07,.31),1,.04)
    m.box('gun_mount',(0,-.03,-.39),(.46,.35,.16),2,.07)
    m.tube('barrel_jacket',(0,-.02),-1.16,-.40,.355,.255,2,32)
    m.tube('muzzle_armour',(0,-.02),-1.22,-1.12,.40,.26,0,32)
    m.tube('muzzle_inner',(0,-.02),-1.14,-.47,.25,.205,4,32)
    m.disc('single_optic_back',(0,-.02,-.72),.204,.022,4)
    m.disc('single_optic',(0,-.02,-.75),.135,.014,3)
    m.disc('optic_pupil',(0,-.02,-.768),.054,.009,3)
    m.box('rangefinder',(0,.46,-.19),(.28,.13,.32),0,.055)
    m.box('rangefinder_glass',(0,.47,-.518),(.14,.034,.017),4,.015)
    m.box('underside',(0,-.43,.28),(.42,.11,.41),7,.055)
    return m

def interceptor():
    m=Mesh()
    m.loft('central_fuselage',[(-1.03,0,-.01,.11,.095,.04),(-.42,0,0,.39,.24,.10),(.38,0,0,.44,.27,.11),(.79,0,0,.25,.20,.07)],0)
    m.box('dorsal_spine',(0,.23,.14),(.17,.10,.38),1,.05)
    m.box('optical_slit',(0,.185,-.58),(.17,.035,.045),3,.015)
    for side in [-1,1]:
        m.wing(f'forward_wing_{side}',side)
        m.wing(f'wing_inlay_{side}',side,1,1)
        m.box(f'engine_pylon_{side}',(side*.78,.05,.22),(.43,.11,.29),2,.065)
        m.loft(f'engine_shell_{side}',[(-.58,side*1.20,.10,.255,.245,.085),(-.40,side*1.20,.10,.32,.29,.10),
            (.62,side*1.20,.10,.30,.27,.085),(.88,side*1.20,.10,.23,.21,.07)],0)
        m.tube(f'intake_lip_{side}',(side*1.20,.10),-.67,-.56,.235,.175,2)
        m.disc(f'intake_shadow_{side}',(side*1.20,.10,-.545),.172,.013,4)
        for i in [-1,0,1]:m.box(f'intake_vane_{side}_{i}',(side*1.20+i*.085,.10,-.57),(.015,.125,.013),1,.009)
        m.tube(f'exhaust_rim_{side}',(side*1.20,.10),.83,.98,.22,.16,2)
        m.disc(f'exhaust_hot_{side}',(side*1.20,.10,.96),.14,.014,6)
        m.box(f'engine_stripe_{side}',(side*1.20,.395,.24),(.08,.014,.28),5,.012)
        m.tube(f'paired_gun_{side}',(side*.19,-.24),-1.10,-.67,.067,.039,2,12)
        m.disc(f'gun_lens_{side}',(side*.19,-.24,-.68),.036,.008,3,12)
        m.box(f'wingtip_marker_{side}',(side*1.72,.12,-.76),(.055,.028,.085),5,.018)
    m.box('ventral_mechanics',(0,-.255,.26),(.25,.065,.33),7,.035)
    return m

if __name__=='__main__':
    sniper().save('CombatSniper','CombatSniperHull.obj')
    interceptor().save('CombatInterceptor','CombatInterceptorHull.obj')
