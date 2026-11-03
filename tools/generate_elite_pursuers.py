"""Build the original Fang and Veil pursuit meshes as Wavefront assets."""

from math import cos, sin, tau, sqrt
from pathlib import Path
import struct

DEST = Path(__file__).resolve().parents[1] / "Resources/enemies/ElitePursuers"
PALETTE = {
    "gunmetal": (.18, .24, .30),
    "armour": (.32, .40, .47),
    "edge": (.45, .53, .58),
    "joint": (.095, .125, .15),
    "glass": (.055, .105, .14),
    "copper": (.68, .34, .16),
    "ivory": (.83, .83, .74),
    "sensor": (.19, .72, .80),
    "nozzle": (.24, .42, .56),
}


class Mesh:
    def __init__(self):
        self.faces = []

    def loft(self, name, rings, material, bands=None):
        """Join equally sized convex cross-sections and cap both ends."""
        points = [p for ring in rings for p in ring]
        center = tuple(sum(p[i] for p in points) / len(points) for i in range(3))

        def face(vertices, surface, inside):
            a, b, c = vertices[:3]
            u, v = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
            normal = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
            size = sqrt(sum(n*n for n in normal))
            if size < 1e-8:
                raise ValueError(f"Degenerate face in {name}")
            sign = 1 if sum(normal[i]*(a[i]-inside[i]) for i in range(3)) > 0 else -1
            if sign < 0:
                vertices = vertices[::-1]
            self.faces.append((name, surface, vertices, tuple(sign*n/size for n in normal)))

        face(rings[0], material, center)
        face(rings[-1], material, center)
        for k, (a, b) in enumerate(zip(rings, rings[1:])):
            surface = bands.get(k, material) if bands else material
            inside = tuple(sum(p[i] for p in a+b) / (len(a)+len(b)) for i in range(3))
            for i in range(len(a)):
                j = (i+1) % len(a)
                face([a[i], a[j], b[j], b[i]], surface, inside)

    def write(self, path, wound_surface=None):
        lines = ["# Elite pursuit fighter. Nose +Z, up +Y.", "mtllib ElitePursuers.mtl", f"o {path.stem}"]
        index = normal_index = 1
        next_uv = len(PALETTE) + 1
        for i in range(len(PALETTE)):
            lines.append(f"vt {(i+.5)/len(PALETTE):.7f} 0.5")
        texture_index = {name:i+1 for i,name in enumerate(PALETTE)}
        current = None
        for name, material, points, normal in self.faces:
            if (name, material) != current:
                lines += [f"g {name}", f"usemtl {material}"]
                current = name, material
            lines.extend("v %.5f %.5f %.5f" % p for p in points)
            lines.append("vn %.6f %.6f %.6f" % normal)
            uv_indices = [texture_index[material]] * len(points)
            if wound_surface:
                housing, center_x, center_y, shoulder_z, radius = wound_surface
                if name == housing and min(point[2] for point in points) >= shoulder_z:
                    # The shader uses the U band above one for the exposed housing.
                    uv_indices = range(next_uv, next_uv + len(points))
                    for x, y, _ in points:
                        lines.append(f"vt {1.5+(x-center_x)/(2*radius):.7f} "
                                     f"{.5+(y-center_y)/(2*radius):.7f}")
                    next_uv += len(points)
            lines.append("f " + " ".join(f"{index+j}/{uv}/{normal_index}" for j, uv in enumerate(uv_indices)))
            index += len(points)
            normal_index += 1
        path.write_text("\n".join(lines)+"\n", encoding="ascii")


def section(z, x, y, w, h):
    outline = [(-.72,-1),(.72,-1),(1,-.55),(1,.55),(.72,1),(-.72,1),(-1,.55),(-1,-.55)]
    return [(x+u*w, y+v*h, z) for u,v in outline]


def wing_section(x, y, front, back, thick):
    outline = [(-.67,-1),(.67,-1),(1,-.4),(1,.4),(.67,1),(-.67,1),(-1,.4),(-1,-.4)]
    return [(x, y+v*thick, (front+back)/2+u*(front-back)/2) for u,v in outline]


def tube(z, x, y, radius):
    return [(x+cos(tau*i/8)*radius, y+sin(tau*i/8)*radius, z) for i in range(8)]


def fighter(fang, subsystem_state="fitted"):
    mesh = Mesh()
    nose = 2.18 if fang else 1.58
    mesh.loft("fuselage", [section(z,0,y,w,h) for z,y,w,h in [
        (-2.05,.02,.42,.24),(-1.75,.02,.70,.39),(-.57,.03,.78,.43),
        (.59,.02,.68,.38),(nose-.34,-.04,.35 if fang else .49,.21),(nose,-.05,.23 if fang else .37,.14)
    ]], "gunmetal", {1:"armour", 3:"armour"})
    mesh.loft("canopy", [section(z,0,y,w,h) for z,y,w,h in [
        (-.72,.39,.23,.09),(-.32,.53,.35,.14),(.52,.50,.30,.11),(1.04,.32,.15,.045)
    ]], "glass")
    mesh.loft("visor", [section(z,0,.015,w,.042) for z,w in [(nose-.015,.17 if fang else .27),(nose+.025,.16 if fang else .26)]], "sensor")
    if fang:
        mesh.loft("copper_spine", [section(z,0,y,.065,.026) for z,y in [
            (.99,.31),(1.35,.264),(1.80,.19),(2.06,.133)
        ]], "copper")
    if not fang:
        mesh.loft("sensor_socket", [
            tube(.40,-1.10,.23,.16), tube(.81,-1.10,.23,.18)
        ], "joint")
        if subsystem_state != "damaged":
            accent = "joint" if subsystem_state == "exposed" else "glass"
            mesh.loft("sensor_pod", [
                tube(z,-1.10,.38,r) for z,r in [(.37,.12),(.48,.24),(.83,.23),(.97,.12)]
            ], "armour", {1:accent, 2:accent})
    for side in (-1,1):
        label = "port" if side < 0 else "starboard"
        if fang:
            stations = [(.58,-.04,1.01,-1.38,.18),(1.10,.18,1.25,-1.51,.16),
                        (1.67,.47,1.66,-1.14,.12),(2.15,.76,2.13,-.49,.075),
                        (2.57,1.02,2.47,1.74,.031)]
            bands = {0:"joint",1:"armour",3:"edge"}
        else:
            stations = [(.58,.03,.86,-1.65,.21),(1.18,.13,.93,-1.84,.17),
                        (1.83,.17,.55,-1.73,.15),(2.06,.13,.42,-1.54,.12),
                        (2.40,-.20,.19,-1.41,.085),(2.56,-.34,.04,-1.20,.055)]
            bands = {0:"joint",1:"armour",3:"ivory",4:"edge"}
        mesh.loft(label+"_wing", [wing_section(side*x,y,f,b,h) for x,y,f,b,h in stations], "gunmetal", bands)
        x = side*.54
        mesh.loft(label+"_engine", [tube(z,x,-.36,r) for z,r in [
            (-2.18,.26),(-1.99,.31),(-1.19,.29),(-.83,.20)
        ]], "joint", {1:"armour"})
        mesh.loft(label+"_exhaust", [tube(z,x,-.36,r) for z,r in [
            (-2.205,.18),(-2.145,.22)
        ]], "nozzle")
        if fang and side > 0 and subsystem_state == "damaged":
            mesh.loft(label+"_cannon_socket", [
                tube(.43,1.02,.16,.14), tube(.64,1.02,.16,.15)
            ], "joint", {0:"copper"})
        else:
            mesh.loft(label+"_cannon", [tube(z,side*.73,-.18,r) for z,r in [
                (.30,.115),(.73,.105),(nose-.28,.075),(nose-.15,.091)
            ]], "joint", {2:"edge"})
            if fang and side > 0:
                accent = "joint" if subsystem_state == "exposed" else "copper"
                mesh.loft(label+"_cannon_housing", [
                    tube(z,1.02,.22,r) for z,r in [(.47,.15),(.61,.26),(.92,.26),(1.08,.15)]
                ], "armour", {1:accent})
        for z in (-1.18,-.88,-.58):
            mesh.loft(label+"_hinge", [tube(depth,side*.94,.16,.083) for depth in (z,z+.14)], "edge")
    return mesh


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    materials = []
    for name, color in PALETTE.items():
        materials.extend([f"newmtl {name}", "Ka 0.08 0.08 0.08", "Kd %.3f %.3f %.3f" % color,
                          "Ks 0.18 0.21 0.24", "Ns 45", "illum 2", ""])
    (DEST/"ElitePursuers.mtl").write_text("\n".join(materials), encoding="ascii")
    row = b"".join(bytes(round(x*255) for x in color[::-1])*16 for color in PALETTE.values())
    pixels = row*16
    bitmap = struct.pack("<2sIHHI", b"BM", 54+len(pixels), 0, 0, 54)
    bitmap += struct.pack("<IiiHHIIiiII", 40, len(PALETTE)*16, 16, 1, 24, 0, len(pixels), 0, 0, 0, 0)
    (DEST/"ElitePursuers.bmp").write_bytes(bitmap+pixels)
    for name, fang in [("Fang", True), ("Veil", False)]:
        fighter(fang).write(DEST/f"{name}.obj")
    panel = Mesh()
    panel.loft("shell", [section(z,0,0,w,h) for z,w,h in [
        (-.30,.25,.12),(-.08,.34,.22),(.14,.28,.18),(.24,.19,.11)
    ]], "joint", {1:"armour",2:"sensor"})
    panel.loft("lock_plate", [section(z,0,0,w,h) for z,w,h in [
        (.23,.15,.084),(.29,.13,.070),(.32,.087,.050)
    ]], "sensor", {1:"ivory"})
    panel.write(DEST/"RearBoostPanel.obj")
    fighter(True, "exposed").write(DEST/"FangExposed.obj",
                                  ("starboard_cannon_housing", 1.02, .22, .92, .27))
    fighter(True, "damaged").write(DEST/"FangDamaged.obj")
    fighter(False, "exposed").write(DEST/"VeilExposed.obj",
                                   ("sensor_pod", -1.10, .38, .83, .24))
    fighter(False, "damaged").write(DEST/"VeilDamaged.obj")


if __name__ == "__main__":
    main()
