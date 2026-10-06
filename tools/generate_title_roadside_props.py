"""Original bevelled timber stake and weathered wood albedo for title scenery."""
from pathlib import Path
import math
import struct
import uuid

from generate_title_landscape import Mesh, OUT, ROOT, cross


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    mesh = Mesh('TitleStake')
    polygon = [(-.09, -.13), (.09, -.13), (.13, -.09), (.13, .09),
               (.09, .13), (-.09, .13), (-.13, .09), (-.13, -.09)]
    rings = []
    for level, height in enumerate([0.0, 1.34, 1.53]):
        ring = []
        for x, z in polygon:
            # The uneven cut and slight taper break the silhouette of a perfect box.
            ring.append(mesh.vertex((x*(1-.025*level), height+(x*.13 if level==2 else 0), z*(1-.02*level))))
        rings.append(ring)
    for low, high in zip(rings, rings[1:]):
        for n in range(8):
            j=(n+1)%8
            mesh.face(low[n], high[n], high[j], low[j])
    bottom=mesh.vertex((0,0,0)); top=mesh.vertex((0,1.53,0))
    for n in range(8):
        j=(n+1)%8
        mesh.face(bottom,rings[0][n],rings[0][j])
        mesh.face(top,rings[-1][j],rings[-1][n])
    # Orient the authored shell once; Mesh validates topology and signed volume.
    volume=sum(sum(mesh.vertices[f[0]][k]*cross(mesh.vertices[f[1]],mesh.vertices[f[2]])[k] for k in range(3)) for f in mesh.faces)
    if volume<0: mesh.faces=[(a,c,b) for a,b,c in mesh.faces]
    mesh.validate()
    lines=['# Original title roadside timber stake; closed bevelled solid.',
           'mtllib title_stake.mtl','o TitleStake','usemtl weathered_timber']
    lines += ['v %.7f %.7f %.7f'%p for p in mesh.vertices]
    uv=[]; normals=[]
    for face in mesh.faces:
        points=[mesh.vertices[i] for i in face]
        n=cross(tuple(points[1][k]-points[0][k] for k in range(3)),tuple(points[2][k]-points[0][k] for k in range(3)))
        length=math.sqrt(sum(v*v for v in n));normals.append(tuple(v/length for v in n))
        us=[math.atan2(p[2],p[0])/(2*math.pi)+.5 for p in points]
        if max(us)-min(us)>.5: us=[u+1 if u<.5 else u for u in us]
        uv += [(u,p[1]/1.55) for u,p in zip(us,points)]
    lines += ['vt %.7f %.7f'%p for p in uv]
    lines += ['vn %.7f %.7f %.7f'%n for n in normals]
    lines += ['f '+' '.join(f'{v+1}/{i*3+j+1}/{i+1}' for j,v in enumerate(face)) for i,face in enumerate(mesh.faces)]
    (OUT/'TitleStake.obj').write_text('\n'.join(lines)+'\n',encoding='ascii')
    width,height=128,256
    pixels=bytearray()
    for y in range(height):
        for x in range(width):
            grain=math.sin(x*.61+math.sin(y*.027)*.9)*.045+math.sin(x*1.87+y*.005)*.023
            wear=math.sin(x*.13+y*.019)*.028
            base=(.38+grain+wear,.265+grain*.75+wear,.155+grain*.45+wear)
            # Worn pale identification band: muted enough to remain scenery.
            if 215<=y<=234 and math.sin(x*.73+y*1.19)>.0:
                base=(.70+grain,.59+grain,.39+grain)
            if y<35: base=tuple(v*(.70+.3*y/35) for v in base)
            rgb=[round(max(0,min(1,v))*255) for v in base]
            pixels.extend(reversed(rgb))
    header=struct.pack('<2sIHHI',b'BM',54+len(pixels),0,0,54)+struct.pack('<IiiHHIIiiII',40,width,height,1,24,0,len(pixels),2835,2835,0,0)
    (OUT/'title_stake.bmp').write_bytes(header+pixels)
    (OUT/'title_stake.mtl').write_text('newmtl weathered_timber\nKd 1 1 1\nKs 0 0 0\nNs 1\nmap_Kd title_stake.bmp\n',encoding='ascii')
    for name in ['TitleStake.obj','title_stake.bmp','title_stake.mtl']:
        asset=OUT/name;meta=asset.with_suffix(asset.suffix+'.meta')
        if not meta.exists():
            logical=asset.relative_to(ROOT).as_posix()
            meta.write_text(f'guid={uuid.uuid5(uuid.NAMESPACE_URL,logical).hex}\nlogicalPath={logical}\n',encoding='ascii')
    print(f'TitleStake: {len(mesh.faces)} triangles; closed shell and outward winding verified')


if __name__=='__main__': main()
