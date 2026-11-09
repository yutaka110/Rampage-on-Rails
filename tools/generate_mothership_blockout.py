"""Carrier hull and fittings. Units match RailMothershipPresentation.h."""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "Resources/enemies/MothershipBlockout"


class Obj:
    def __init__(self):
        self.lines = ["# Original carrier blockout; front +Z; dimensions in game world units."]
        self.count = 0

    def face(self, points, band=None):
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

    def save(self, name):
        (DEST/name).write_text("\n".join(self.lines)+"\n", encoding="utf-8")


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    hull = Obj()
    hull.prism([(-22,-66),(22,-66),(27,32),(16,66),(-16,66),(-27,32)],-8,9,1,5)
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
                hull.box(side*x,-8.55,z,1.8,1.15,6.1,2)
        hull.box(side*58,-7.0,-46,20,4,3,0)
        hull.box(side*58,-7.0,-47.2,15,2.2,1.3,3)
        hull.box(side*47,-5.3,31,16,1.7,12,2)
    hull.box(0,13,-28,25,8,30,5)
    hull.box(0,21,-35,15,8,12,1)
    hull.save("MothershipHull.obj")
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
