"""Deterministic, bounded low-poly assets for the submitted rail course.
OBJ positions use right-handed authoring (+Z muzzle); Assimp converts to LH.
Textures are tiny authored colour palettes, never sample/demo textures.
"""
from pathlib import Path
import math, struct, uuid
ROOT = Path(__file__).resolve().parents[1]

def save_palette(path, palette):
    # Eight pixels per swatch keep sampling in the center of each band.
    width, height = len(palette)*8, 8
    stride = (width*3+3)//4*4
    row = b''.join(bytes(reversed(color))*8 for color in palette) + bytes(stride-width*3)
    pixels = row*height
    header = struct.pack('<2sIHHI',b'BM',54+len(pixels),0,0,54)
    info = struct.pack('<IiiHHIIiiII',40,width,height,1,24,0,len(pixels),2835,2835,0,0)
    path.write_bytes(header+info+pixels)

class Mesh:
    def __init__(self, name, palette):
        self.name, self.palette, self.faces = name, palette, []
    def face(self, points, colour):
        a,b,c=points[:3]
        u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
        n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
        size=math.sqrt(sum(x*x for x in n))
        if size<1e-8:return
        self.faces.append((points,colour,tuple(x/size for x in n)))
    def save(self, directory):
        directory.mkdir(parents=True,exist_ok=True)
        save_palette(directory/(self.name+'.bmp'), self.palette)
        (directory/(self.name+'.mtl')).write_text('newmtl surface\nKd 1 1 1\nKs 0 0 0\nNs 1\nmap_Kd '+self.name+'.bmp\n',encoding='ascii')
        lines=['# Bounded authored geometry; flat face normals; explicit palette material.','mtllib '+self.name+'.mtl','o '+self.name,'usemtl surface'];index=1
        for points,colour,n in self.faces:
            for x,y,z in points:lines.append(f'v {x:.6f} {y:.6f} {z:.6f}')
            for point in points:lines.append(f'vt {(colour+.5)/len(self.palette):.6f} 0.5')
            for point in points:lines.append('vn %.6f %.6f %.6f'%n)
            for i in range(1,len(points)-1):
                ids=(index,index+i,index+i+1);lines.append('f '+' '.join(f'{j}/{j}/{j}' for j in ids))
            index+=len(points)
        (directory/(self.name+'.obj')).write_text('\n'.join(lines)+'\n',encoding='ascii')
        for ext in ['obj','mtl','bmp']:
            asset=directory/(self.name+'.'+ext);meta=asset.with_suffix(asset.suffix+'.meta')
            if not meta.exists():meta.write_text(f'guid={uuid.uuid4().hex}\nlogicalPath={asset.relative_to(ROOT).as_posix()}\n')

def stone(name, fractured):
    # Corner cuts <= 0.18 of a half extent; centre faces retain collision limits.
    colours=[(119,109,89),(145,131,104),(103,94,78),(164,145,109),(86,81,71),(59,53,44),(231,161,63),(29,26,24)]
    if not fractured:colours=[(int(r*.82),int(g*.88),int(b*.96)) for r,g,b in colours]
    m=Mesh(name,colours)
    outline=[(-.82,-1),(.82,-1),(1,-.82),(1,.82),(.82,1),(-.82,1),(-1,.82),(-1,-.82)]
    rings=[]
    for y,s in [(-1,.88),(-.82,1),(.10,.97),(.78,1),(1,.88)]:
        rings.append([(x*s,y,z*s) for x,z in outline])
    # Reverse winding where needed: these rings travel counter-clockwise from above.
    m.face(rings[0],4);m.face(list(reversed(rings[-1])),1)
    for j in range(len(rings)-1):
        for i in range(8):
            k=(i+1)%8
            m.face([rings[j][i],rings[j+1][i],rings[j+1][k],rings[j][k]],(i+j*3)%5)
    if fractured:
        # Recess-look fracture ribbons on all cardinal faces; stay inside the box.
        # Surface z depends on the ring interpolation, so ribbons cannot float.
        path=[(-.80,-.12),(-.38,.08),(.10,-.04),(.42,.16),(.77,.03)]
        def extent(y):
            for (ya,sa),(yb,sb) in zip([(-1,.88),(-.82,1),(.10,.97),(.78,1)], [(-.82,1),(.10,.97),(.78,1),(1,.88)]):
                if ya<=y<=yb:return sa+(sb-sa)*(y-ya)/(yb-ya)
        for side in range(4):
            def point(x,y,offset):
                z=-min(1,extent(y)+offset)
                return [(x,y,z),(-z,y,x),(-x,y,-z),(z,y,-x)][side]
            for (ya,xa),(yb,xb) in zip(path,path[1:]):
                for width,col in [(.036,7),(.012,6)]:
                    # Winding faces outward; a dark split surrounding a warm seam.
                    offset=.0006 if col==6 else .0003
                    m.face(list(reversed([point(xa-width,ya,offset),point(xa+width,ya,offset),point(xb+width,yb,offset),point(xb-width,yb,offset)])),col)
    m.save(ROOT/'Resources/course_meshes'/name)

def turret():
    m=Mesh('CombatTurret',[(56,69,79),(98,115,122),(160,170,164),(34,41,47),(227,132,45),(255,209,101)])
    def tube(axis,center,levels,colour,segments=12,cap=True):
        rings=[]
        for t,r in levels:
            ring=[]
            for i in range(segments):
                a=2*math.pi*i/segments
                q=(r*math.cos(a),t,r*math.sin(a)) if axis=='y' else (r*math.cos(a),r*math.sin(a),t)
                ring.append(tuple((q[k]+center[k])*.94 for k in range(3)))
            rings.append(ring)
        # Y rings and Z rings have opposite winding.
        def face(p,c):m.face(p if axis=='y' else list(reversed(p)),c)
        for j in range(len(rings)-1):
            for i in range(segments):
                k=(i+1)%segments
                face([rings[j][i],rings[j+1][i],rings[j+1][k],rings[j][k]],colour+(i%2 if colour<2 else 0))
        if cap:face(rings[0],3);face(list(reversed(rings[-1])),colour)
    tube('y',(0,0,0),[(-.50,.73),(-.40,.86),(-.25,.86),(-.17,.66)],0,8)
    base_faces = len(m.faces)
    tube('y',(0,0,0),[(-.17,.40),(.05,.40),(.24,.58),(.44,.48),(.56,.26)],0,8)
    for x in [-.27,.27]:
        tube('z',(x,.17,0),[(-.18,.18),(.12,.20),(.53,.15),(.70,.20),(.88,.20)],1,8,False)
        tube('z',(x,.17,0),[(.875,.175),(.88,.105)],4,8,False)
        # A dark inset bore and warm core make the firing direction explicit.
        tube('z',(x,.17,0),[(.80,.10),(.875,.10)],3,8)
        tube('z',(x,.17,0),[(.878,.057),(.879,.057)],5,8)
    m.save(ROOT/'Resources/enemies/CombatTurret')
    for name,faces in [('CombatTurretBase',m.faces[:base_faces]),('CombatTurretHead',m.faces[base_faces:])]:
        part=Mesh(name,m.palette)
        part.faces=faces
        part.save(ROOT/'Resources/enemies'/name)

if __name__=='__main__':
    stone('RailHazardBlock', True)
    stone('RailHazardSolid', False)
    turret()
