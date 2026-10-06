"""Author the user-reference drone as reusable OBJ parts, not a flat image.

Right-handed authoring: +Z is the face/muzzle; Assimp converts to runtime -Z.
Only Python + NumPy are needed. No third-party model/texture is downloaded.
"""
from pathlib import Path
import math
import struct
import zlib
import json
import sys
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Resources/enemies/TwinShieldDrone"
SIZE = 1024
TILE = SIZE // 4


def png(path, pixels):
    h, w, channels = pixels.shape
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    raw = b"".join(b"\0" + row.tobytes() for row in pixels.astype(np.uint8))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6 if channels == 4 else 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def dds(path, pixels):
    # Linear RGBA8, including alpha roughness. DDS bypasses the WIC sRGB path.
    h, w, _ = pixels.shape
    header = [124, 0x100F, h, w, w*4, 0, 0] + [0]*11
    header += [32, 0x41, 0, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000]
    header += [0x1000, 0, 0, 0, 0]
    path.write_bytes(b"DDS " + struct.pack("<31I", *header) + pixels.astype(np.uint8).tobytes())


def textures():
    rng = np.random.default_rng(2817)
    colors = [(70,72,71), (86,86,81), (39,42,42), (26,27,26),
              (117,115,104), (131,103,43), (88,71,35), (55,56,53),
              (155,150,130), (32,35,34), (96,79,55), (14,16,15),
              (197,71,18), (255,136,34), (255,213,113), (100,83,59)]
    atlas = np.zeros((SIZE,SIZE,3), np.uint8)
    packed = np.zeros((SIZE,SIZE,4), np.uint8)
    yy, xx = np.mgrid[0:TILE,0:TILE]
    for index, color in enumerate(colors):
        grain = rng.normal(0,3.1,(TILE,TILE))
        broad = 4*np.sin(xx*.031+yy*.012)+2*np.cos(yy*.039)
        c = np.array(color)[None,None,:] + (grain+broad)[:,:,None]
        scratches = rng.random((TILE,TILE)) > .999
        for _ in range(110):
            x,y = rng.integers(12,TILE-12,2)
            length = int(rng.integers(3,24))
            for t in range(length):
                if x+t<TILE-7 and y+t//4<TILE-7:
                    scratches[y+t//4,x+t] = True
        c[scratches] = np.array((135,134,123)) + rng.normal(0,8,(scratches.sum(),1))
        # Two weathered yellow diagonal chevrons on broad shield/body faces.
        if index in (0,1):
            chevron = (np.abs(yy-(.35*TILE + .60*np.abs(xx-TILE*.5)))<13) | (np.abs(yy-(.60*TILE + .60*np.abs(xx-TILE*.5)))<13)
            chips = (rng.random((TILE,TILE))>.08) & (np.sin(xx*.19+yy*.27)>-.7)
            c[chevron & chips] = np.array((145,111,42)) + grain[chevron & chips,None]*1.2
        # Hairline panel joins and a dusty lower edge.
        if index in (0,1,7):
            seams = (np.abs(xx-32)<1)|(np.abs(xx-224)<1)|(np.abs(yy-32)<1)|(np.abs(yy-224)<1)
            c[seams] *= .62
        c += np.maximum(0,yy/TILE-.72)[:,:,None]*np.array((18,13,6))
        if index==11: c=np.broadcast_to(np.array((9,11,10)),(TILE,TILE,3)).copy()
        if index>=12: c=np.broadcast_to(np.array(color),(TILE,TILE,3)).copy()
        rough = np.clip(.65+grain*.003 + (index in (3,9,11))*.15-(index in (4,8))*.20,.30,.88)
        height = grain*.0008
        gy,gx = np.gradient(height)
        n = np.stack((-gx*2,-gy*2,np.ones_like(gx)),axis=2)
        n /= np.linalg.norm(n,axis=2,keepdims=True)
        y,x = divmod(index,4)
        atlas[y*TILE:(y+1)*TILE,x*TILE:(x+1)*TILE] = np.clip(c,0,255).astype(np.uint8)
        packed[y*TILE:(y+1)*TILE,x*TILE:(x+1)*TILE,:3] = np.clip((n*.5+.5)*255,0,255).astype(np.uint8)
        packed[y*TILE:(y+1)*TILE,x*TILE:(x+1)*TILE,3] = (rough*255).astype(np.uint8)
    png(OUT/"TwinShieldDrone_Color.png",atlas)
    dds(OUT/"TwinShieldDrone_NormalRoughness.dds",packed)
    return atlas


def unit(v):
    v = np.array(v,dtype=float)
    return v/max(1e-12,np.linalg.norm(v))


class Mesh:
    def __init__(self,name):
        self.name = name
        self.faces = []

    def face(self,points,tile=0,uv=None):
        points = np.array(points,dtype=float)
        n = unit(np.cross(points[1]-points[0],points[2]-points[0]))
        if np.linalg.norm(n)<.5:
            raise ValueError("degenerate face")
        if uv is None:
            axes = np.argsort(np.abs(n))[:2]
            p = points[:,axes]
            span = np.maximum(np.ptp(p,axis=0),.001)
            uv = .08 + .84*(p-p.min(axis=0))/span
        uv = np.array(uv)
        tx,ty = tile%4,tile//4
        uv[:,0] = (tx+uv[:,0])/4
        uv[:,1] = 1-(ty+uv[:,1])/4
        self.faces.append((points,uv,n,tile))

    def loft(self,outline,levels,tile=0,edge=4):
        rings = [np.array([(x*s,y*s,z) for x,y in outline]) for z,s in levels]
        self.face(rings[0][::-1],2)
        self.face(rings[-1],tile)
        for a,b in zip(rings,rings[1:]):
            for i in range(len(a)):
                j=(i+1)%len(a)
                self.face([a[i],a[j],b[j],b[i]],edge)

    def tube(self,center,levels,tile=4,axis='z',segments=24,cap=True):
        rings=[]
        for t,r in levels:
            ring=[]
            for i in range(segments):
                a=2*math.pi*i/segments
                p=np.array((r*math.cos(a),r*math.sin(a),t))
                if axis=='x': p=p[[2,0,1]]
                if axis=='y': p=p[[1,2,0]]
                ring.append(p+center)
            rings.append(np.array(ring))
        if cap: self.face(rings[0][::-1],tile)
        for a,b in zip(rings,rings[1:]):
            for i in range(segments):
                j=(i+1)%segments
                self.face([a[i],a[j],b[j],b[i]],tile)
        if cap: self.face(rings[-1],tile)

    def frame(self,outer,inner,back,front,tile=0):
        # Actual through aperture: front annulus, back annulus and hole walls.
        a=np.array([(x,y,front) for x,y in outer])
        b=np.array([(x,y,front) for x,y in inner])
        c=np.array([(x,y,back) for x,y in outer])
        d=np.array([(x,y,back) for x,y in inner])
        for i in range(len(a)):
            j=(i+1)%len(a)
            self.face([a[i],a[j],b[j],b[i]],tile)
            self.face([c[j],c[i],d[i],d[j]],2)
            self.face([c[i],c[j],a[j],a[i]],4)
            self.face([b[i],b[j],d[j],d[i]],3)

    def box(self,c,s,tile=0):
        x,y,z=c; a,b,d=s
        self.loft([(x-a,y-b),(x+a,y-b),(x+a,y+b),(x-a,y+b)],[(z-d,1),(z+d,1)],tile,tile)

    def transformed(self,offset=(0,0,0),angle=0):
        m=Mesh(self.name)
        c,s=math.cos(angle),math.sin(angle)
        R=np.array(((c,0,s),(0,1,0),(-s,0,c)))
        for p,uv,n,tile in self.faces:
            m.faces.append((p@R.T+offset,uv,n@R.T,tile))
        return m

    def save(self):
        lines=['# Original mesh reconstructed from user-supplied concept reference.',
               'mtllib TwinShieldDrone.mtl','o '+self.name,'usemtl industrial_armour']
        index=1
        triangles=0
        for points,uv,n,tile in self.faces:
            for p in points: lines.append('v %.7f %.7f %.7f'%tuple(p))
            for q in uv: lines.append('vt %.7f %.7f'%tuple(q))
            for _ in points: lines.append('vn %.7f %.7f %.7f'%tuple(n))
            for i in range(1,len(points)-1):
                ids=(index,index+i,index+i+1)
                lines.append('f '+' '.join(f'{j}/{j}/{j}' for j in ids))
                triangles+=1
            index+=len(points)
        (OUT/(self.name+'.obj')).write_text('\n'.join(lines)+'\n',encoding='ascii')
        return triangles


def octagon(rx,ry,y=0):
    return [(-rx*.62,-ry+y),(rx*.62,-ry+y),(rx,-ry*.56+y),(rx,ry*.54+y),
            (rx*.55,ry+y),(-rx*.55,ry+y),(-rx,ry*.54+y),(-rx,-ry*.56+y)]


def build():
    hull=Mesh('TwinShieldHull')
    hull.loft(octagon(.63,.67),[(-.52,.73),(-.36,.98),(.12,1),(.43,.88),(.53,.74)],1,4)
    # Lower equipment block and two genuinely hollow, stepped gun barrels.
    hull.box((0,-.53,.10),(.40,.12,.26),2)
    for side in (-1,1):
        x=side*.30
        hull.tube((x,-.61,0),[(.08,.13),(.22,.155),(.30,.155),(.63,.115),(.68,.145),(.91,.145)],4,segments=20,cap=False)
        hull.tube((x,-.61,0),[(.91,.145),(.94,.125),(.94,.089),(.74,.089)],3,segments=20,cap=False)
        hull.tube((x,-.61,0),[(.735,.089),(.74,.089)],11,segments=20)
        hull.tube((x,-.61,0),[(.31,.157),(.37,.157)],5,segments=20,cap=False)
        # Dorsal cylindrical thrusters with collars, rear nozzles and vents.
        hull.tube((side*.35,0,-.29),[(.36,.155),(.69,.155),(.76,.13),(.81,.075)],2,axis='y',segments=20)
        hull.tube((side*.35,0,-.29),[(.67,.158),(.70,.158)],5,axis='y',segments=20,cap=False)
        hull.tube((side*.32,.20,0),[(-.70,.14),(-.56,.19),(-.43,.19)],2,segments=20)
        hull.tube((side*.32,.20,0),[(-.705,.10),(-.70,.10)],11,segments=20)
        # Upper piston and lower circular hinge linking the shield mounting.
        hull.tube((0,.12,-.05),[(side*.57,.095),(side*.77,.095),(side*.77,.07),(side*1.04,.07)] if side>0 else [(side*1.04,.07),(side*.77,.07),(side*.77,.095),(side*.57,.095)],4,axis='x',segments=16)
        hull.box((side*.81,-.15,.03),(.10,.20,.13),5)
        hull.tube((side*.78,-.23,0),[(.10,.155),(.17,.155),(.19,.10)],2,segments=24)
        hull.tube((side*.78,-.23,0),[(.193,.075),(.198,.075)],4,segments=20)
        for k in range(4): hull.box((side*.51,-.29+k*.15,-.04),(.055,.024,.10),3)
    # Faceted hood over the sensor plus a recessed octagonal bezel.
    hull.frame(octagon(.44,.40),octagon(.34,.30),.50,.64,2)
    hull.tube((0,0,0),[(.535,.295),(.65,.315),(.70,.315),(.73,.265)],10,segments=40,cap=False)
    hull.tube((0,0,0),[(.731,.268),(.74,.238),(.74,.187),(.72,.18)],4,segments=40,cap=False)
    hull.tube((0,0,0),[(.715,.184),(.72,.184)],11,segments=40)
    for i in range(16):
        a=2*math.pi*i/16
        hull.tube((.284*math.cos(a),.284*math.sin(a),0),[(.704,.012),(.710,.012)],8,segments=6)
    for side in (-1,1):
        hull.box((side*.435,-.035,.55),(.025,.23,.065),4)
    hull.box((0,.65,.04),(.23,.055,.16),2)
    for x in (-.15,-.05,.05,.15): hull.box((x,.709,.04),(.030,.004,.10),11)
    for x,y in ((-.49,.31),(.49,.31),(-.43,-.40),(.43,-.40)):
        hull.tube((x,y,0),[(.45,.022),(.47,.022)],8,segments=6)

    shield=Mesh('TwinShieldPanel')
    shape=[(-.20,-1.06),(.20,-1.06),(.27,-.91),(.27,.91),(.20,1.06),(-.20,1.06),(-.27,.91),(-.27,-.91)]
    shield.loft(shape,[(-.16,.92),(-.12,1),(.10,1),(.15,.90)],0,4)
    for sign in (-1,1):
        outer=[(-.27,sign*.94),(.27,sign*.94),(.23,sign*1.49),(.13,sign*1.60),(-.19,sign*1.60),(-.27,sign*1.40)]
        inner=[(-.13,sign*1.05),(.13,sign*1.05),(.12,sign*1.32),(.075,sign*1.40),(-.09,sign*1.40),(-.13,sign*1.28)]
        if sign<0: outer.reverse();inner.reverse()
        shield.frame(outer,inner,-.13,.10,2)
    for y in (-.83,.83):
        shield.box((0,y,.159),(.20,.018,.015),4)
        for x in (-.17,.17): shield.tube((x,y,0),[(.178,.014),(.183,.014)],8,segments=6)
    for x in (-.18,.18): shield.box((x,0,-.185),(.035,1.12,.04),2)
    shield.box((0,0,-.22),(.14,.28,.085),2)
    core=Mesh('TwinShieldCore')
    core.tube((0,0,0),[(.721,.13),(.75,.115),(.765,.070)],12,segments=40)
    core.tube((0,0,0),[(.766,.069),(.78,.035)],13,segments=32)
    core.tube((0,0,0),[(.780,.035),(.785,.030)],14,segments=24)
    assembled=Mesh('TwinShieldDrone')
    for m in (hull,core,shield.transformed((-1.09,0,0),-.08),shield.transformed((1.09,0,0),.08)):
        assembled.faces.extend(m.faces)
    return hull,shield,core,assembled


def preview(mesh,atlas):
    """Render the actual authored mesh, UVs and atlas for artifact inspection."""
    w,h=768,896
    images=[]
    for camera in (np.array((0,.30,5.7)),np.array((4.3,2.1,6.0))):
        forward=unit(-camera)
        right=unit(np.cross(forward,(0,1,0)))
        up=unit(np.cross(right,forward))
        basis=np.stack((right,up,forward),axis=1)
        image=np.zeros((h,w,3),dtype=np.uint8)
        gradient=np.linspace(221,182,h)[:,None,None]
        image[:]=gradient
        depth=np.full((h,w),np.inf)
        light=unit((-3,5,6))
        for p,uv,n,tile in mesh.faces:
            if np.dot(n,camera-p.mean(axis=0))<=0: continue
            points=(p-camera)@basis
            focal=1000 if camera[0]==0 else 1100
            screen=np.column_stack((w*.5+points[:,0]/points[:,2]*focal,h*.5-points[:,1]/points[:,2]*focal))
            for k in range(1,len(p)-1):
                ids=[0,k,k+1]
                a,b,c=screen[ids]
                xmin=max(0,int(np.floor(min(a[0],b[0],c[0]))));xmax=min(w-1,int(np.ceil(max(a[0],b[0],c[0]))))
                ymin=max(0,int(np.floor(min(a[1],b[1],c[1]))));ymax=min(h-1,int(np.ceil(max(a[1],b[1],c[1]))))
                if xmin>xmax or ymin>ymax: continue
                denominator=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
                if abs(denominator)<1e-8: continue
                yy,xx=np.mgrid[ymin:ymax+1,xmin:xmax+1]
                xx=xx+.5;yy=yy+.5
                wa=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/denominator
                wb=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/denominator
                wc=1-wa-wb
                weights=np.stack((wa,wb,wc),axis=2)/points[ids,2]
                reciprocal=weights.sum(axis=2)
                z=1/np.maximum(reciprocal,.001)
                old=depth[ymin:ymax+1,xmin:xmax+1]
                mask=(wa>=0)&(wb>=0)&(wc>=0)&(z<old)
                if not mask.any(): continue
                q=weights@uv[ids]/reciprocal[:,:,None]
                u=np.clip((q[:,:,0]*SIZE).astype(int),0,SIZE-1)
                v=np.clip(((1-q[:,:,1])*SIZE).astype(int),0,SIZE-1)
                color=atlas[v,u].astype(float)
                shade=.45+.55*max(0,np.dot(n,light))
                view=unit(camera-p.mean(axis=0))
                rim=(1-max(0,np.dot(n,view)))**3
                spec=max(0,np.dot(n,unit(light+view)))**35*40
                if tile<12: color=color*shade+spec+rim*12
                else: color=color*1.15
                color=np.clip(color,0,255).astype(np.uint8)
                region=image[ymin:ymax+1,xmin:xmax+1]
                region[mask]=color[mask];old[mask]=z[mask]
        images.append(image)
    path=OUT/'TwinShieldDrone_Preview.png'
    png(path,np.concatenate(images,axis=1))
    print('Preview:',path)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    atlas=textures()
    (OUT/'TwinShieldDrone.mtl').write_text('newmtl industrial_armour\nKd 1 1 1\nKs .5 .5 .5\nNs 48\nmap_Kd TwinShieldDrone_Color.png\nmap_Bump TwinShieldDrone_NormalRoughness.dds\n',encoding='ascii')
    meshes=build()
    stats={m.name:m.save() for m in meshes}
    assert stats['TwinShieldDrone']<16000
    (OUT/'mesh_stats.json').write_text(json.dumps(stats,indent=2)+'\n')
    print(json.dumps(stats))
    if '--preview' in sys.argv: preview(meshes[-1],atlas)


if __name__=='__main__': main()
