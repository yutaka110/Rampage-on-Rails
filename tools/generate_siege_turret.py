"""Original tall twin-cannon emplacement, inspired by the supplied reference.
Uses the existing deterministic palette OBJ writer. Authoring muzzle is +Z;
Assimp imports it toward runtime -Z. Runtime socket: RailTurretGeometry.h.
"""
from generate_submission_hazards import Mesh, ROOT
import math
PALETTE=[(79,86,90),(112,118,117),(128,132,127),(32,39,43),(174,141,81),(245,181,73),(61,67,71)]

def box(m,center,size,col):
 x,y,z=center;w,h,d=[s/2 for s in size]
 pts=[(x-w,y-h,z-d),(x+w,y-h,z-d),(x+w,y+h,z-d),(x-w,y+h,z-d),
      (x-w,y-h,z+d),(x+w,y-h,z+d),(x+w,y+h,z+d),(x-w,y+h,z+d)]
 for ids in [(3,2,1,0),(4,5,6,7),(0,4,7,3),(2,6,5,1),(0,1,5,4),(7,6,2,3)]:m.face([pts[i] for i in ids],col)

def tower(m):
 outline=[(-.85,-1),(.85,-1),(1,-.85),(1,.85),(.85,1),(-.85,1),(-1,.85),(-1,-.85)]
 rings=[[(x*s,y,z*s) for x,z in outline] for y,s in [(-3.55,1.12),(-2.53,1.10),(-.40,.78),(-.22,.84)]]
 m.face(rings[0],6);m.face(list(reversed(rings[-1])),1)
 for j in range(3):
  for i in range(8):
   k=(i+1)%8;m.face([rings[j][i],rings[j+1][i],rings[j+1][k],rings[j][k]],6 if j==1 else 0)
 # Raised masonry/armour panels expose dark, narrow joints at long range.
 for side in range(4):
  for row in range(5):
   y=-2.32+row*.39;extent=1.10+(y+2.53)*(-.32/2.13)
   for col in range(3):
    c=(col-1)*extent*.55
    def p(a,b):
     z=-(1.10+(b+2.53)*(-.32/2.13))-.016
     return [(a,b,z),(-z,b,a),(-a,b,-z),(z,b,-a)][side]
    ps=[p(c-extent*.255,y-.17),p(c+extent*.255,y-.17),p(c+extent*.255,y+.17),p(c-extent*.255,y+.17)]
    m.face(list(reversed(ps)),1+(row+col)%2)
 box(m,(0,-.18,0),(1.76,.18,1.65),6)

def tube(m,x,y,levels,col,n=12,cap=False,inside=False):
 rings=[[(x+r*math.cos(i*math.tau/n),y+r*math.sin(i*math.tau/n),z) for i in range(n)] for z,r in levels]
 for a,b in zip(rings,rings[1:]):
  for i in range(n):
   k=(i+1)%n;points=[a[k],b[k],b[i],a[i]];m.face(list(reversed(points)) if inside else points,col)
 if cap:m.face(list(reversed(rings[0])),col);m.face(rings[-1],col)

def main():
 base=Mesh('SiegeTurretBase',PALETTE);tower(base)
 head=Mesh('SiegeTurretHead',PALETTE)
 box(head,(0,.23,-.03),(1.58,.78,1.02),0)
 box(head,(0,.66,-.18),(1.25,.18,.80),1)
 box(head,(0,.24,-.59),(1.45,.70,.16),6)
 for side in (-1,1):
  box(head,(side*.73,.47,-.05),(.20,1.02,1.22),1)
  box(head,(side*.80,.75,-.30),(.14,.45,.64),2)
  box(head,(side*.73,.03,-.05),(.26,.14,1.31),6)
  for i in range(7):box(head,(side*.86,.40,-.45+i*.13),(.07,.48,.045),6)
  x=side*.44
  tube(head,x,.32,[(-.50,.25),(.25,.25),(.50,.18),(.90,.18),(1.12,.23),(1.22,.23),(1.65,.16),(1.80,.22),(1.95,.22)],0)
  for z in (.55,.65,.75):tube(head,x,.32,[(z,.205),(z+.045,.205)],1)
  # Open bore: muzzle ring at the exact shared runtime socket, dark recessed cap.
  tube(head,x,.32,[(1.95,.22),(1.95,.12)],2)
  tube(head,x,.32,[(1.74,.12),(1.95,.12)],3,inside=True)
  tube(head,x,.32,[(1.735,.117),(1.74,.117)],3,cap=True)
  box(head,(side*.68,.57,.45),(.07,.09,.24),4)
 combined=Mesh('SiegeTurret',PALETTE);combined.faces=base.faces+head.faces
 for mesh in (base,head,combined):mesh.save(ROOT/'Resources/enemies'/mesh.name)
if __name__=='__main__':main()
