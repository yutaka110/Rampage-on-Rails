"""Original low-poly carrier blockout. Units match RailMothershipPresentation.h."""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "Resources/enemies/MothershipBlockout"


class Obj:
    def __init__(self):
        self.lines = ["# Original carrier blockout; front +Z; dimensions in game world units."]
        self.count = 0

    def face(self, points):
        a, b, c = points[:3]
        u = [b[i]-a[i] for i in range(3)]
        v = [c[i]-a[i] for i in range(3)]
        n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
        length = math.sqrt(sum(x*x for x in n))
        n = [x/length for x in n]
        # Expanded faces keep hard edges and consistent explicit normals.
        for p in points:
            self.lines += ["v %.5f %.5f %.5f" % p, "vt 0.5 0.5", "vn %.5f %.5f %.5f" % tuple(n)]
        base = self.count+1
        for i in range(1, len(points)-1):
            self.lines.append("f " + " ".join(f"{j}/{j}/{j}" for j in (base,base+i,base+i+1)))
        self.count += len(points)

    def prism(self, outline, bottom, top):
        # Outline is counterclockwise in X/Z; top normal is +Y.
        self.face([(x,top,z) for x,z in reversed(outline)])
        self.face([(x,bottom,z) for x,z in outline])
        for i, (x,z) in enumerate(outline):
            xx,zz = outline[(i+1)%len(outline)]
            self.face([(x,bottom,z),(x,top,z),(xx,top,zz),(xx,bottom,zz)])

    def box(self, x,y,z,w,h,d):
        self.prism([(x-w/2,z-d/2),(x+w/2,z-d/2),(x+w/2,z+d/2),(x-w/2,z+d/2)],y-h/2,y+h/2)

    def save(self, name):
        (DEST/name).write_text("\n".join(self.lines)+"\n", encoding="utf-8")


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    hull = Obj()
    hull.prism([(-22,-66),(22,-66),(27,32),(16,66),(-16,66),(-27,32)],-9,9)
    for side in (-1,1):
        # Broad twin launch sponsons, connected by a thick transverse beam.
        points=[(side*x,z) for x,z in [(30,-48),(70,-48),(81,-30),(74,35),(53,52),(32,35)]]
        if side<0: points.reverse()
        hull.prism(points,-8,6)
        hull.box(side*31,1,-10,24,12,44)
        hull.box(side*58,8,-23,23,8,38)
    hull.box(0,13,-28,25,8,30)
    hull.box(0,21,-35,15,8,12)
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
