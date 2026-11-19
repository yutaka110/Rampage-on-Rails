"""Build an original, watertight rock and eight matching closed fracture chunks."""
from pathlib import Path
import math,itertools,json
out=Path('Resources/course_meshes/SideRock')
out.mkdir(parents=True,exist_ok=True)
def add(a,b):return tuple(x+y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def mul(a,t):return tuple(x*t for x in a)
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def unit(a):return mul(a,1/math.sqrt(dot(a,a)))
def unique(points):
 r=[]
 for p in points:
  if all(dot(sub(p,q),sub(p,q))>1e-14 for q in r):r.append(p)
 return r
def clip(faces,n,d):
 kept=[];cut=[]
 for poly,inside in faces:
  result=[]
  for a,b in zip(poly,poly[1:]+poly[:1]):
   da,db=dot(n,a)-d,dot(n,b)-d
   if da<=1e-8:result.append(a)
   if (da<0 and db>0) or (da>0 and db<0):
    p=add(a,mul(sub(b,a),da/(da-db)));result.append(p);cut.append(p)
  result=unique(result)
  if len(result)>=3:kept.append((result,inside))
 cut=unique(cut)
 if len(cut)>=3:
  c=mul(tuple(map(sum,zip(*cut))),1/len(cut))
  u=unit(cross(n,(0,1,0) if abs(n[1])<.9 else (1,0,0)));v=cross(n,u)
  cut.sort(key=lambda p:math.atan2(dot(sub(p,c),v),dot(sub(p,c),u)))
  kept.append((cut,True))
 return kept
phi=(1+math.sqrt(5))/2
verts=list(set([(0,a,b*phi) for a in [-1,1] for b in [-1,1]]+[(a,b*phi,0) for a in [-1,1] for b in [-1,1]]+[(b*phi,0,a) for a in [-1,1] for b in [-1,1]]))
verts=sorted(map(unit,verts));faces=[]
for ids in itertools.combinations(range(12),3):
 a,b,c=[verts[i] for i in ids];n=unit(cross(sub(b,a),sub(c,a)))
 values=[dot(sub(p,a),n) for p in verts]
 if min(values)<-1e-7 and max(values)>1e-7:continue
 if dot(n,a)<0:b,c=c,b
 ab=unit(add(a,b));bc=unit(add(b,c));ca=unit(add(c,a))
 for tri in [(a,ab,ca),(ab,b,bc),(ca,bc,c),(ab,bc,ca)]:
  points=[]
  for p in tri:
   r=1+.055*math.sin(p[0]*11+p[1]*7+p[2]*5)
   points.append((p[0]*r+.07*p[1],p[1]*r,p[2]*r+.06*p[0]))
  faces.append((points,False))
faces=clip(faces,(0,-1,0),.77)
points=unique([p for f,_ in faces for p in f]);lo=[min(p[i] for p in points) for i in range(3)];hi=[max(p[i] for p in points) for i in range(3)]
faces=[([tuple((p[i]-(lo[i]+hi[i])/2)/((hi[i]-lo[i])/2) for i in range(3)) for p in poly],inside) for poly,inside in faces]
def volume(fs):
 return sum(dot(poly[0],cross(poly[i],poly[i+1]))/6 for poly,_ in fs for i in range(1,len(poly)-1))
def write(name,fs,pivot=(0,0,0)):
 lines=['# Original SideRock geometry. Engine coordinates; OBJ Z is mirrored for Assimp.'];index=0
 for poly,inside in fs:
  n=unit(cross(sub(poly[1],poly[0]),sub(poly[2],poly[0])))
  for i in range(1,len(poly)-1):
   tri=[poly[0],poly[i],poly[i+1]]
   for p in tri:
    q=sub(p,pivot);lines.append('v %.7f %.7f %.7f'%(q[0],q[1],-q[2]))
   for p in tri:
    axis=max(range(3),key=lambda j:abs(n[j]));u,v=[p[j]*.5+.5 for j in range(3) if j!=axis]
    lines.append('vt %.7f %.7f'%(u,v+(3 if inside else 0)))
   for _ in tri:lines.append('vn %.7f %.7f %.7f'%(n[0],n[1],-n[2]))
   ids=[index+1,index+3,index+2];lines.append('f '+' '.join(f'{j}/{j}/{j}' for j in ids));index+=3
 (out/name).write_text('\n'.join(lines)+'\n')
write('SideRock.obj',faces)
planes=[(unit((1,.16,-.12)),.03),(unit((-.13,1,.19)),-.07),(unit((.18,-.11,1)),.08)]
centers=[];volumes=[]
for i,signs in enumerate(itertools.product([-1,1],repeat=3)):
 fs=faces
 for (n,d),sign in zip(planes,signs):fs=clip(fs,mul(n,sign),d*sign)
 points=unique([p for f,_ in fs for p in f]);pivot=mul(tuple(map(sum,zip(*points))),1/len(points));centers.append(pivot);volumes.append(volume(fs))
 assert volumes[-1]>0
 write(f'SideRockFragment{i}.obj',fs,pivot)
assert abs(sum(volumes)-volume(faces))<1e-6
h='#pragma once\n#include "utils/math/Vector.h"\n#include <array>\nnamespace RailSideRockGeometry {\ninline constexpr std::array<Vector3,8> Pivots{{\n'
h+=''.join('    Vector3{%.7ff,%.7ff,%.7ff},\n'%p for p in centers)
h+='}};\n}\n'
Path('application/course/RailSideRockGeometry.h').write_text(h)
(out/'geometry-audit.json').write_text(json.dumps(dict(original_volume=volume(faces),fragment_volumes=volumes,pivots=centers,outer_faces=len(faces)),indent=2))
print('Generated original rock and 8 matching closed chunks; volume conserved:',volume(faces))
