"""Build the canyon arch and its detachable shoulder from a shared rock profile."""
from math import cos, sin, sqrt
from pathlib import Path
from uuid import NAMESPACE_URL, uuid5

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "Resources/course_meshes/RouteNaturalArch"
SHOULDER_OFFSET = (-2.2, -1.5, -.70)
SHOULDER_ORIGIN = tuple(a+b for a,b in zip((7.0, 7.4, 0.0),SHOULDER_OFFSET))

# Outer and inner edges in rail X/Y, followed by front and back depth.
PROFILE = [
    ((-31.0, -15.3), (-11.1, -16.8), 8.9, 10.1),
    ((-26.8, -6.7), (-8.6, -7.7), 6.8, 7.5),
    ((-22.2, -1.1), (-7.3, -.6), 5.0, 6.5),
    ((-17.8, 4.7), (-6.8, 4.0), 4.3, 5.5),
    ((-14.1, 9.9), (-5.7, 6.8), 4.6, 4.6),
    ((-9.8, 13.6), (-4.2, 8.1), 3.6, 4.7),
    ((-5.1, 15.1), (-1.2, 8.8), 3.4, 3.7),
    ((-.4, 15.6), (1.0, 9.2), 2.7, 3.3),
    ((3.8, 13.9), (3.2, 8.8), 3.1, 3.1),
    ((6.25, 11.45), (4.5, 8.10), 2.7, 2.8),
    ((8.55, 9.0), (5.35, 7.05), 2.45, 2.55),
    ((10.50, 7.15), (6.1, 6.1), 2.8, 2.9),
    ((11.20, 5.05), (6.5, 5.85), 2.9, 3.1),
    ((13.9, 2.6), (6.65, 3.0), 4.15, 4.0),
    ((18.1, -.95), (7.15, -.55), 5.55, 6.1),
    ((25.7, -6.7), (8.3, -8.2), 7.0, 7.9),
    ((32.6, -15.1), (11.0, -17.5), 9.3, 9.8),
]
CROWN_CUT, FOOT_CUT = 9, 12
# Fraction across the rock and fraction of front/back depth.
RIM = (
    (.02, -.36), (.16, -.82), (.37, -1), (.62, -.96), (.85, -.76),
    (.99, -.31), (1, .29), (.84, .82), (.56, 1), (.27, .89), (.03, .45),
)


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def unit(v):
    length = sqrt(sum(a*a for a in v))
    return tuple(a / length for a in v)


def center(points):
    return tuple(sum(axis) / len(points) for axis in zip(*points))


def rings_for(profile, relief, first_index=0):
    rings = []
    for index, (outer, inner, front, back) in enumerate(profile, first_index):
        ring = []
        for i, (across, depth) in enumerate(RIM):
            phase = i * 2.39996 + index * 1.71
            x = inner[0] + (outer[0]-inner[0])*across + sin(phase)*relief
            y = inner[1] + (outer[1]-inner[1])*across + cos(phase*1.31)*relief
            z = depth*(front if depth < 0 else back) + sin(phase*.89)*relief
            ring.append((x, y, z))
        rings.append(ring)
    return rings


def skin(rings):
    vertices = [p for ring in rings for p in ring]
    faces = []
    orientation = 0.0
    size = len(RIM)
    for j in range(len(rings)-1):
        inside = center(rings[j] + rings[j+1])
        for i in range(size):
            k = (i+1) % size
            a, b, c, d = j*size+i, j*size+k, (j+1)*size+k, (j+1)*size+i
            triangles = ((a,b,c), (a,c,d)) if (i+j)%2 else ((a,b,d), (b,c,d))
            for face in triangles:
                p, q, r = [vertices[v] for v in face]
                normal = cross(sub(q,p), sub(r,p))
                orientation += sum(a*b for a,b in zip(normal, sub(center([p,q,r]), inside)))
                faces.append(face)
    reverse = orientation < 0
    if reverse:
        faces = [face[::-1] for face in faces]
    normals = [[0.0]*3 for _ in vertices]
    for a,b,c in faces:
        normal = cross(sub(vertices[b],vertices[a]), sub(vertices[c],vertices[a]))
        for v in (a,b,c):
            for axis in range(3):
                normals[v][axis] += normal[axis]
    return vertices, faces, [unit(n) for n in normals], reverse


def export(name, spans, origin=(0,0,0)):
    vertices, normals, faces = [], [], []
    for rings, start, end, cut_start, cut_end in spans:
        source, surface, smooth, reverse = skin(rings)
        size = len(RIM)
        first, last = start*size, (end+1)*size
        base, normal_base = len(vertices), len(normals)
        vertices.extend(source[first:last])
        normals.extend(smooth[first:last])
        for face in surface:
            if min(face) >= first and max(face) < last:
                faces.append([(base+v-first, 1, normal_base+v-first) for v in face])
        for index, sign, fresh in ((start,-1,cut_start), (end,1,cut_end)):
            rim = rings[index]
            midpoint = center(rim)
            outward = unit(sub(midpoint,center(rings[index-sign])))
            cap = len(vertices)
            vertices.append(tuple(a+b*(.22 if fresh else 0) for a,b in zip(midpoint,outward)))
            for i in range(size):
                k = (i+1)%size
                normal = unit(cross(sub(rim[i],vertices[cap]),sub(rim[k],vertices[cap])))
                face = (cap,base+(index-start)*size+i,base+(index-start)*size+k)
                if (sign < 0) != reverse:
                    face = face[::-1]
                    normal = tuple(-x for x in normal)
                n = len(normals)
                normals.append(normal)
                faces.append([(v,2 if fresh else 1,n) for v in face])
    # Course meshes import through Assimp's Z reflection.
    path = OUTPUT / f"{name}.obj"
    lines = [f"o {name}"]
    lines.extend(f"v {x:.5f} {y:.5f} {-z:.5f}" for x,y,z in (sub(p,origin) for p in vertices))
    lines.extend(f"vn {x:.5f} {y:.5f} {-z:.5f}" for x,y,z in normals)
    lines.extend(("vt 0.25 0.5", "vt 0.75 0.5"))
    lines.extend("f "+" ".join(f"{v+1}/{uv}/{n+1}" for v,uv,n in reversed(face)) for face in faces)
    path.write_text("\n".join(lines)+"\n", encoding="ascii")
    logical = path.relative_to(ROOT).as_posix()
    path.with_suffix(".obj.meta").write_text(
        f"guid={uuid5(NAMESPACE_URL, logical).hex}\nlogicalPath={logical}\n", encoding="ascii")


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    arch = rings_for(PROFILE,.22,first_index=-1)
    # Bring the detachable shoulder into the opening while leaving its shape
    # and contact point intact. Blend the adjacent bedrock into the new seams.
    placement = {7:.09, 8:.42, 9:1, 10:1, 11:1, 12:1, 13:.37, 14:.08}
    for index, weight in placement.items():
        arch[index] = [tuple(a+b*weight for a,b in zip(p,SHOULDER_OFFSET)) for p in arch[index]]
    shelf = rings_for([
        ((-21,-2.8),(-15,-2.5),4.8,4.2),
        ((-17,-1.8),(-11,-1.0),4.0,4.8),
        ((-13,-1.7),(-8.1,-.77),3.8,3.7),
        ((-8,-2.7),(-5.1,-.94),3.1,2.6),
        ((-6,-3.5),(-4.9,-2.7),2.2,1.7),
    ],.16)
    export("RouteArchRoots", [
        (arch,0,CROWN_CUT,False,True),
        (arch,FOOT_CUT,len(arch)-1,True,False),
        (shelf,0,len(shelf)-1,False,False),
    ])
    export("RouteArchShoulder", [(arch,CROWN_CUT,FOOT_CUT,True,True)], SHOULDER_ORIGIN)


if __name__ == "__main__":
    main()
