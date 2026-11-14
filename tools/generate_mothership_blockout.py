"""Carrier hull and fittings. Units match RailMothershipPresentation.h."""
from pathlib import Path
import math
from generate_submission_hazards import save_palette

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "Resources/enemies/MothershipBlockout"
HULL_PALETTE = [(52, 64, 72), (135, 151, 156), (163, 178, 180), (22, 29, 34),
                (139, 113, 62), (92, 110, 118), (39, 50, 58), (118, 134, 146),
                (177, 190, 197), (95, 113, 125), (27, 35, 42), (164, 177, 169)]


class Obj:
    def __init__(self, runtime_coordinates=False):
        self.lines = ["# Original carrier blockout; front +Z; dimensions in game world units."]
        self.count = 0
        self.runtime_coordinates = runtime_coordinates

    def face(self, points, band=None):
        if self.runtime_coordinates:
            # Assimp reflects OBJ Z. Author the hull on the same axes as the
            # runtime sockets and correct winding before exporting the face.
            points = [(x,y,-z) for x,y,z in reversed(points)]
        a, b, c = points[:3]
        u = [b[i]-a[i] for i in range(3)]
        v = [c[i]-a[i] for i in range(3)]
        n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
        length = math.sqrt(sum(x*x for x in n))
        n = [x/length for x in n]
        axes = [i for i in range(3) if i != max(range(3), key=lambda i: abs(n[i]))]
        low = [min(p[i] for p in points) for i in axes]
        span = [max(p[i] for p in points)-low[j] for j,i in enumerate(axes)]
        for p in points:
            uv = "vt 0.5 0.5"
            if band is not None:
                u,v = [(p[i]-low[j])/max(span[j],1e-6) for j,i in enumerate(axes)]
                uv = "vt %.6f %.6f" % (band*4+.125+.75*u,.06+.88*v)
            self.lines += ["v %.5f %.5f %.5f" % p, uv, "vn %.5f %.5f %.5f" % tuple(n)]
        base = self.count+1
        for i in range(1, len(points)-1):
            self.lines.append("f " + " ".join(f"{j}/{j}/{j}" for j in (base,base+i,base+i+1)))
        self.count += len(points)

    def prism(self, outline, bottom, top, band=None, underside=None):
        # Outline is counterclockwise in X/Z; top normal is +Y.
        self.face([(x,top,z) for x,z in reversed(outline)],band)
        self.face([(x,bottom,z) for x,z in outline],band if underside is None else underside)
        for i, (x,z) in enumerate(outline):
            xx,zz = outline[(i+1)%len(outline)]
            self.face([(x,bottom,z),(x,top,z),(xx,top,zz),(xx,bottom,zz)],band)

    def box(self, x,y,z,w,h,d,band=None):
        self.prism([(x-w/2,z-d/2),(x+w/2,z-d/2),(x+w/2,z+d/2),(x-w/2,z+d/2)],y-h/2,y+h/2,band)

    def plating(self, x, y, z, width, height, depth, band, bevel=5):
        # Recessed seams between chamfered panels facing the chase camera.
        w, h = width / 2, height / 2
        cut = min(w, h) * .24
        outline = [(-w+cut,-h), (w-cut,-h), (w,-h+cut), (w,h-cut),
                   (w-cut,h), (-w+cut,h), (-w,h-cut), (-w,-h+cut)]
        rear = [(x+a, y+b, z+depth) for a,b in outline]
        front = [(x+a*.89, y+b*.76, z) for a,b in outline]
        self.face(list(reversed(front)), band)
        for i in range(len(outline)):
            j = (i+1) % len(outline)
            self.face([front[i], front[j], rear[j], rear[i]], bevel)

    def save(self, name):
        (DEST/name).write_text("\n".join(self.lines)+"\n", encoding="utf-8")


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    hull = Obj(runtime_coordinates=True)
    hull.prism([(-17,-67),(17,-67),(25,-54),(27,32),(16,66),(-16,66),(-27,32),(-25,-54)],-8,9,9,5)
    hull.prism([(-13,-56),(13,-56),(18,30),(9,57),(-9,57),(-18,30)],-11,-7.7,0)
    hull.prism([(-5,-54),(5,-54),(7,36),(3,56),(-3,56),(-7,36)],-13,-10.7,2)
    for side in (-1,1):
        # Broad twin launch sponsons, connected by a thick transverse beam.
        points=[(side*x,z) for x,z in [(30,-48),(70,-48),(81,-30),(74,35),(53,52),(32,35)]]
        if side<0: points.reverse()
        hull.prism(points,-4.8,6,1,5)
        hull.box(side*31,1,-10,24,12,44,0)
        hull.box(side*58,8,-23,23,8,38,5)
        # A deep open launch bay between lower armour cheeks. Keep the
        # original centreline and animated launch guides on their sockets.
        hull.box(side*43,-5.05,0,19,.6,29,3)
        for cheek in (31,55):
            hull.prism([(side*cheek-3,-17),(side*cheek+3,-17),(side*cheek+4,11),
                (side*cheek+2,17),(side*cheek-2,17),(side*cheek-4,11)],-9.8,-4.5,1,2)
        for z in (-27,-38):
            hull.box(side*55,-6.2,z,26,3.2,8,0)
            hull.box(side*55,-8.0,z,20,.8,6.1,3)
            for x in (47,51,55,59,63):
                hull.box(side*x,-8.55,z,1.8,1.15,5.70,2)
        hull.box(side*58,-7.0,-46,20,4,3,0)
        hull.box(side*58,-7.0,-47.2,15,2.2,1.3,3)
        hull.box(side*47,-5.3,31,16,1.7,12,2)
        # Alloy plating over dark launch cavities, with substantial vents at
        # the outer roots. Their spacing survives the normal gameplay lens.
        for x, y, z, w, h, band in [(48,2.8,-49.8,25,5.1,1), (57,-1.3,-49.4,22,2.9,9),
                                     (65,8,-43.0,15,4.0,1), (37,-3,-48.8,11,2.0,5)]:
            hull.plating(side*x,y,z,w,h,2.0,band)
        hull.plating(side*65,-1.7,-50.2,10,2.4,2.1,3)
        for x in (62,64,66,68):
            hull.box(side*x,-1.7,-50.25,.50,1.22,.60,5)
        for x in (34,45,56,67):
            hull.box(side*x,4.0,-50.0,.52,.76,.9,11)
    hull.box(0,13,-27.5,25,8,29,5)
    hull.plating(0,13,-43.9,26.3,7.9,2.8,9,7)
    hull.box(0,21,-34.8,15,8,11.6,1)
    hull.plating(0,21,-41.9,15.8,6.5,2.0,1)
    # Recessed weapon bays. The shoulders overlap the wing roots, and the
    # open rear/underside clears the traversing heads and moving barrels.
    shoulders = [
        ([(9.5,-71),(28.5,-71),(47.5,-47),(47.5,-36),(28,-32),(9.5,-48)], -9.4,-2.8,1,6),
        ([(25.2,-71.2),(28.7,-69.8),(37.3,-52),(34.8,-48),(27,-58),(25.2,-65)], -13.4,-7.7,0,6),
        ([(10.9,-69.3),(14.4,-71.2),(14.4,-58.5),(12,-54),(9.4,-59)], -12.6,-7.8,6,6),
        ([(14.4,-63),(25.2,-63),(26.6,-53),(13.4,-53)], -13.1,-8.6,6,3),
    ]
    for side in (-1,1):
        for outline,bottom,top,band,underside in shoulders:
            points=[(side*x,z) for x,z in outline]
            hull.prism(points if side>0 else list(reversed(points)),bottom,top,band,underside)
        hull.plating(side*19,4.7,-65.0,8.3,5.3,9.0,9)
        hull.plating(side*14,-3.0,-68.0,5.3,5.0,2.4,9)
        hull.plating(side*30,-4.8,-67.0,6.2,2.9,7.0,1)
    # Stepped central plates reveal a narrow seam and a continuous dark bed.
    hull.plating(0,5.3,-68.3,29,6.4,2.1,0)
    hull.plating(0,5.65,-70.1,13.2,6.0,2.2,1,7)
    for side in (-1,1):
        hull.plating(side*10.65,5.25,-69.65,7.4,5.4,1.9,2)
    hull.plating(0,-2.0,-69.2,24,6.8,2.8,0)
    hull.plating(0,-2.0,-70.1,15,3.4,1.4,3)
    for x in (-5.4,-3.6,-1.8,0,1.8,3.6,5.4):
        hull.box(x,-2.0,-70.15,.46,2.0,.62,5)
    # Recessed carriers for the two permanent heavy-weapon markers.
    for x in (16.1,23.9):
        hull.plating(x,-5.1,-71.7,2.55,3.45,1.0,10)
    hull.save("MothershipHull.obj")
    save_palette(DEST / "CarrierHullSurface.bmp", HULL_PALETTE)
    unit = Obj()
    unit.box(0,0,0,1,1,1)
    unit.save("MothershipBox.obj")
    # Exposed rear reactor. Radius is normalized to its gameplay target sphere.
    reactor = Obj()
    for i in range(8):
        a, b = (i+.5)*math.tau/8, (i+1.5)*math.tau/8
        def pt(r, z, theta):
            return (r*math.cos(theta), r*math.sin(theta), z)
        for points in [
            [pt(1,.20,a),pt(1,.20,b),pt(1,-.28,b),pt(1,-.28,a)],
            [pt(.62,-.28,a),pt(1,-.28,a),pt(1,-.28,b),pt(.62,-.28,b)],
            [pt(.62,-.28,b),pt(.62,.15,b),pt(.62,.15,a),pt(.62,-.28,a)],
            [(0,0,.15),pt(.62,.15,a),pt(.62,.15,b)],
        ]:
            reactor.face(list(reversed(points)))
    reactor.save("MothershipReactor.obj")


if __name__ == "__main__":
    main()
