"""Render the exported OBJ geometry for rig inspection, using the local renderer."""
from pathlib import Path
from types import SimpleNamespace
import math
import struct
import zlib
import numpy as np
import generate_veil_interceptor as veil
import generate_twin_shield_drone as renderer

OUT=veil.ROOT/'generated/veil-model-review'


def rotation(x=0,y=0):
    cx,sx,cy,sy=math.cos(x),math.sin(x),math.cos(y),math.sin(y)
    return np.array(((cy,0,sy),(0,1,0),(-sy,0,cy)))@np.array(((1,0,0),(0,cx,-sx),(0,sx,cx)))


def load(state):
    faces=[]
    for name,pivot in veil.PIVOTS.items():
        if state=='damaged' and name in ('VeilSensor','VeilSensorCover'):
            continue
        vertices=[];normals=[];material='coal'
        rot=np.eye(3);offset=np.array(pivot,dtype=float)
        if state=='charge':
            if name=='VeilArmourTop':rot=rotation(x=-veil.SHROUD_ANGLE);offset[2]-=veil.SHROUD_RETRACT
            if name=='VeilArmourBottom':rot=rotation(x=veil.SHROUD_ANGLE);offset[2]-=veil.SHROUD_RETRACT
            if name=='VeilSensorCover':rot=rotation(x=-1.45)
            if name=='VeilWingLeft':rot=rotation(y=-.12)
            if name=='VeilWingRight':rot=rotation(y=.12)
        if state=='exploded':
            if name=='VeilWingLeft':offset[0]-=1.2
            if name=='VeilWingRight':offset[0]+=1.2
            if name=='VeilArmourTop':offset[1]+=1.0
            if name=='VeilArmourBottom':offset[1]-=1.0
            if name in ('VeilCannon','VeilCannonGlow'):offset[2]+=1.0
            if name in ('VeilSensor','VeilSensorCover'):offset[0]-=.8;offset[1]+=.5
            offset*=.60
            rot*=.60
        for line in (veil.OUT/(name+'.obj')).read_text().splitlines():
            p=line.split()
            if not p:continue
            if p[0]=='v':vertices.append([float(v) for v in p[1:]])
            elif p[0]=='vn':normals.append([float(v) for v in p[1:]])
            elif p[0]=='usemtl':material=p[1]
            elif p[0]=='f':
                indices=[tuple(int(v)-1 for v in word.split('/')) for word in p[1:]]
                points=np.array([vertices[i[0]] for i in indices])@rot.T+offset
                if state!='exploded':points*=.84
                n=np.array(normals[indices[0][2]])@rot.T
                n=n/max(np.linalg.norm(n),.001)
                tile=list(veil.COLORS).index(material)
                if name=='VeilCannonGlow':tile=12 if state=='charge' else 13
                if name=='VeilEngineGlow':tile=13
                if name=='VeilSensor' and state=='charge':tile=14
                uv=np.array([((tile%4+.5)/4,1-(tile//4+.5)/4)]*len(points))
                faces.append((points,uv,n,tile))
    return SimpleNamespace(faces=faces)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    atlas=np.zeros((1024,1024,3),dtype=np.uint8)
    colors=list(veil.COLORS.values())+[(.45,.78,1),(1,.04,.07),(1,.35,.10),(0,0,0)]
    for i,color in enumerate(colors):
        atlas[(i//4)*256:(i//4+1)*256,(i%4)*256:(i%4+1)*256]=np.array(color)*255
    renderer.OUT=OUT
    for state in ('rest','charge','exploded'):
        renderer.preview(load(state),atlas)
        (OUT/'TwinShieldDrone_Preview.png').replace(OUT/f'veil-{state}-mesh.png')
    before=OUT/'before-heavy-redesign/assets'
    if (before/'VeilHull.obj').exists():
        current=veil.OUT
        try:
            veil.OUT=before;renderer.OUT=before.parent
            old_atlas=atlas.copy();old_colors={};name=None
            for line in (before/'VeilInterceptor.mtl').read_text().splitlines():
                p=line.split()
                if p and p[0]=='newmtl':name=p[1]
                elif p and p[0]=='Kd':old_colors[name]=[float(x) for x in p[1:4]]
            for i,name in enumerate(veil.COLORS):
                old_atlas[(i//4)*256:(i//4+1)*256,(i%4)*256:(i%4+1)*256]=np.array(old_colors[name])*255
            renderer.preview(load('rest'),old_atlas)
        finally:
            veil.OUT=current;renderer.OUT=OUT
        def read_rgb(path):
            b=path.read_bytes();w,h=struct.unpack_from('>II',b,16);offset=8;data=b''
            while offset<len(b):
                length=struct.unpack_from('>I',b,offset)[0];kind=b[offset+4:offset+8]
                if kind==b'IDAT':data+=b[offset+8:offset+8+length]
                offset+=length+12
            rows=np.frombuffer(zlib.decompress(data),np.uint8).reshape(h,w*3+1)
            assert (rows[:,0]==0).all()
            return rows[:,1:].reshape(h,w,3).copy()
        old=read_rgb(before.parent/'TwinShieldDrone_Preview.png')[:,768:]
        new=read_rgb(OUT/'veil-rest-mesh.png')[:,768:]
        comparison=np.concatenate((old,new),axis=1)
        comparison[:,766:770]=70
        renderer.png(OUT/'veil-style-comparison.png',comparison)
        poses=np.concatenate((read_rgb(OUT/'veil-rest-mesh.png')[:,:768],read_rgb(OUT/'veil-charge-mesh.png')[:,:768]),axis=1)
        poses[:,766:770]=70
        renderer.png(OUT/'veil-deployment-comparison.png',poses)


if __name__=='__main__':main()
