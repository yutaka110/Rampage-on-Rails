"""Carrier weapons: exposed rotating tubes versus a braced single heavy bore.

Authoring +Z is runtime -Z. Moving meshes use the barrel axis as their
origin; RailTurretGeometry.h supplies the common (0, .32, 0) pivot.
The roadside SiegeTurret assets are generated separately.
"""
import math
from generate_siege_turret import Mesh as PaletteMesh, ROOT, box, tube

# Paint, frame, steel, ceramic bore, safety paint, fasteners, grille,
# tempered steel, hydraulic chrome, armor, rubber gasket, stencil paint.
PALETTE = [(54, 69, 82), (111, 130, 141), (154, 169, 177), (30, 34, 39),
           (177, 130, 54), (75, 91, 103), (41, 51, 61), (103, 104, 117),
           (187, 199, 202), (104, 120, 128), (25, 30, 33), (191, 197, 184)]
HEAVY_PALETTE = [(66, 75, 79), (133, 139, 132), (151, 163, 166), (37, 39, 42),
                 (164, 143, 91), (83, 94, 98), (41, 48, 50), (106, 99, 105),
                 (193, 202, 202), (157, 158, 145), (28, 31, 32), (203, 202, 185)]


class Mesh(PaletteMesh):
    """Mode 16 UVs carry a material band and stable per-face detail coordinates."""
    def __init__(self, name, palette=None):
        super().__init__(name, HEAVY_PALETTE if name.startswith('CarrierHeavy') else PALETTE)

    def save(self, directory):
        super().save(directory)
        lines = ['# Carrier surface UV: U = 4*material + .125 + .75*faceU; V = faceV.',
                 'mtllib '+self.name+'.mtl', 'o '+self.name, 'usemtl surface']
        index = 1
        for points, material, normal in self.faces:
            major = max(range(3), key=lambda axis: abs(normal[axis]))
            axes = (0, 1) if major == 2 else ((1, 2) if major == 0 else (0, 2))
            lo = [min(p[axis] for p in points) for axis in axes]
            span = [max(p[axis] for p in points)-low for axis, low in zip(axes, lo)]
            for x, y, z in points:
                lines.append(f'v {x:.6f} {y:.6f} {z:.6f}')
            for point in points:
                u, v = [(point[axis]-low)/max(length, 1e-6) for axis, low, length in zip(axes, lo, span)]
                lines.append(f'vt {material*4+.125+u*.75:.6f} {v:.6f}')
            for _ in points:
                lines.append('vn %.6f %.6f %.6f' % normal)
            for i in range(1, len(points)-1):
                lines.append('f '+' '.join(f'{j}/{j}/{j}' for j in (index, index+i, index+i+1)))
            index += len(points)
        (directory/(self.name+'.obj')).write_text('\n'.join(lines)+'\n', encoding='ascii')


def bevel_box(mesh, center, size, paint=9, bevel=1):
    x, y, z = center
    w, h, d = [value/2 for value in size]
    cut = min(w, h, d)*.24
    outline = [(-w+cut,-h), (w-cut,-h), (w,-h+cut), (w,h-cut),
               (w-cut,h), (-w+cut,h), (-w,h-cut), (-w,-h+cut)]
    rings = [[(x+a*scale,y+b*scale,z+depth) for a,b in outline]
             for depth,scale in [(-d,.91),(-d+cut,1),(d-cut,1),(d,.91)]]
    mesh.face(list(reversed(rings[0])), paint)
    mesh.face(rings[-1], paint)
    for j in range(3):
        for i in range(8):
            k = (i+1)%8
            mesh.face([rings[j][k],rings[j+1][k],rings[j+1][i],rings[j][i]], bevel if j!=1 else paint)


def bolt(mesh, x, y, front, radius=.035):
    tube(mesh, x, y, [(front-.025,radius),(front,radius)], 5, n=6, cap=True)
    box(mesh, (x,y,front+.001), (radius*.95,radius*.22,.002), 10)


def cylinder_y(mesh, center, levels, color, segments=12):
    part = Mesh('temporary', PALETTE)
    tube(part, 0, 0, levels, color, n=segments, cap=True)
    x, y, z = center
    for points, shade, _ in part.faces:
        mesh.face([(x+a, y+c, z-b) for a, b, c in points], shade)


def save(*meshes):
    for mesh in meshes:
        mesh.save(ROOT / 'Resources/enemies' / mesh.name)


def generate_gatling():
    base = Mesh('GatlingTurretBase', PALETTE)
    box(base, (0, -2.40, 0), (1.76, .30, 1.50), 0)
    box(base, (0, -2.20, 0), (1.40, .14, 1.16), 1)
    cylinder_y(base, (0, 0, -.08), [(-2.15, .36), (-.35, .36)], 2)
    cylinder_y(base, (0, 0, -.08), [(-2.08, .39), (-1.94, .39)], 10)
    for side in (-1, 1):
        box(base, (side*.64, -1.30, -.10), (.18, 1.70, .50), 1)
        box(base, (side*.64, -.45, -.10), (.32, .18, .76), 0)
        box(base, (side*.64, -2.05, -.10), (.30, .18, .72), 0)
        for y in (-2.37, -.47):
            bolt(base, side*.64, y, .46, .045)
    cylinder_y(base, (0, 0, 0), [(-.45, .68), (-.26, .68)], 2)
    head = Mesh('GatlingTurretHead', PALETTE)
    bevel_box(head, (0, .20, -.35), (1.26, .65, .92), 0, 1)
    bevel_box(head, (0, .58, -.40), (1.10, .12, .72), 1, 2)
    for side in (-1, 1):
        box(head, (side*.72, .22, -.28), (.22, .76, .96), 6)
        box(head, (side*.82, .22, -.28), (.09, .66, .76), 1)
        for z in (-.56, -.32, -.08):
            box(head, (side*.88, .24, z), (.06, .50, .075), 2)
        box(head, (side*.59, -.12, -.05), (.16, .14, 1.04), 4)
        # Recessed radiator bank behind the three existing thermal cells.
        box(head, (side*.84, .28, .20), (.18, .64, .10), 6)
        for y in (.08, .28, .48):
            box(head, (side*.84, y, .257), (.14, .055, .012), 2)
        for y in (-.08, .61):
            bolt(head, side*.75, y, .215)
    tube(head, 0, .32, [(-.12, .60), (.32, .60), (.40, .54)], 0, n=18)
    tube(head, 0, .32, [(.40, .54), (.40, .49)], 2, n=18)
    for i in range(6):
        a = i*math.tau/6 + math.pi/6
        bolt(head, .565*math.cos(a), .32+.565*math.sin(a), .405, .027)
    rotor = Mesh('GatlingTurretRotor', PALETTE)
    tube(rotor, 0, 0, [(.15, .49), (.52, .49)], 6, n=18, cap=True)
    for i in range(6):
        a = i * math.tau / 6
        x, y = .40*math.cos(a), .40*math.sin(a)
        tube(rotor, x, y, [(.48, .13), (1.83, .13)], 2)
        tube(rotor, x, y, [(1.83, .13), (1.95, .13), (2.06, .165), (2.25, .165)], 7)
        for z in (.65, .85):
            tube(rotor, x, y, [(z, .15), (z+.045, .15)], 1)
        tube(rotor, x, y, [(2.25, .165), (2.25, .135)], 2)
        tube(rotor, x, y, [(2.25, .135), (2.25, .103)], 3)
        tube(rotor, x, y, [(2.03, .103), (2.25, .103)], 3, inside=True)
        tube(rotor, x, y, [(2.025, .10), (2.03, .10)], 3, cap=True)
    tube(rotor, 0, 0, [(1.45, .57), (1.53, .57)], 1, n=18)
    tube(rotor, 0, 0, [(1.53, .57), (1.53, .50)], 2, n=18)
    # Index mark makes acceleration and deceleration readable at play distance.
    box(rotor, (.54, 0, 1.49), (.09, .13, .085), 4)
    for i in range(6):
        a = i*math.tau/6 + math.pi/6
        bolt(rotor, .535*math.cos(a), .535*math.sin(a), 1.535, .025)
    save(base, head, rotor)


def generate_heavy():
    base = Mesh('CarrierHeavyTurretBase', PALETTE)
    bevel_box(base, (0, -2.38, 0), (2.50, .34, 1.94), 9, 1)
    box(base, (0, -2.15, -.10), (2.20, .14, 1.68), 1)
    for side in (-1, 1):
        bevel_box(base, (side*.86, -1.35, -.05), (.48, 1.46, 1.20), 9, 1)
        box(base, (side*.88, -1.31, .58), (.44, 1.35, .10), 10)
        bevel_box(base, (side*.88, -1.31, .64), (.39, 1.24, .04), 9, 1)
        for y in (-1.78, -1.28, -.78):
            box(base, (side*.88, y, .65), (.40, .10, .06), 6)
        for y in (-1.84, -.79):
            for dx in (-.12, .12):
                bolt(base, side*.88+dx, y, .686, .04)
    box(base, (0, -.51, -.02), (2.12, .30, 1.64), 6)
    cylinder_y(base, (0, 0, 0), [(-.36, .88), (-.21, .88)], 2, 16)
    head = Mesh('CarrierHeavyTurretHead', PALETTE)
    bevel_box(head, (0, .11, -.35), (1.68, .82, 1.40), 9, 1)
    bevel_box(head, (0, .63, -.38), (1.48, .20, 1.18), 9, 1)
    for side in (-1, 1):
        bevel_box(head, (side*.87, .18, -.02), (.36, 1.04, 1.66), 9, 1)
        box(head, (side*1.02, .12, -.02), (.12, .76, 1.32), 0)
        box(head, (side*.92, -.34, .02), (.38, .16, 1.82), 6)
        box(head, (side*.85, .45, .62), (.30, .34, .23), 4)
        tube(head, side*.76, -.09, [(-.12, .13), (.80, .13)], 0, cap=True)
        tube(head, side*.76, -.09, [(.67, .145), (.80, .145)], 10)
        for y in (-.20, .58):
            for dx in (-.10, .10):
                bolt(head, side*.87+dx, y, .822, .032)
    barrel = Mesh('CarrierHeavyTurretBarrel', PALETTE)
    tube(barrel, 0, 0, [(-.40, .46), (.48, .46), (.66, .52), (1.52, .52)], 2, n=16)
    tube(barrel, 0, 0, [(1.52, .52), (1.70, .66), (1.98, .66), (2.10, .61)], 9, n=16)
    tube(barrel, 0, 0, [(1.38, .525), (1.49, .525)], 7, n=16)
    tube(barrel, 0, 0, [(.50, .56), (.68, .56)], 1, n=16)
    tube(barrel, 0, 0, [(2.10, .61), (2.10, .57)], 2, n=16)
    tube(barrel, 0, 0, [(2.10, .57), (2.10, .43)], 3, n=16)
    tube(barrel, 0, 0, [(1.72, .43), (2.10, .43)], 3, n=16, inside=True)
    tube(barrel, 0, 0, [(1.71, .42), (1.72, .42)], 3, n=16, cap=True)
    for side in (-1, 1):
        tube(barrel, side*.76, -.41, [(.18, .075), (1.25, .075)], 8, cap=True)
        box(barrel, (side*.62, -.38, 1.24), (.32, .18, .16), 6)
    vent = Mesh('CarrierHeavyTurretVent', PALETTE)
    bevel_box(vent, (0, 0, 0), (.36, .10, .84), 9, 1)
    box(vent, (0, .057, .05), (.24, .018, .60), 0)
    box(vent, (0, .070, .26), (.22, .012, .065), 4)
    for z in (-.22, -.08, .06):
        box(vent, (0, .070, z), (.22, .012, .045), 6)
    save(base, head, barrel, vent)


if __name__ == '__main__':
    generate_gatling()
    generate_heavy()
