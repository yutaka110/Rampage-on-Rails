"""Build the canyon arch and its detachable overhead slab from a shared profile."""
from math import cos, sin, sqrt
from pathlib import Path
from uuid import NAMESPACE_URL, uuid5

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "Resources/course_meshes/RouteNaturalArch"
SLAB_ORIGIN = (0, 20, 0)

# Outer and inner edges in rail X/Y, followed by front and back depth.
PROFILE = [
    ((-34.0, -15.3), (-18.0, -16.8), 8.9, 10.1),
    ((-29.8, -6.7), (-15.8, -7.7), 6.8, 7.5),
    ((-25.4, -.9), (-14.6, -.6), 5.6, 6.5),
    ((-22.8, 5.3), (-13.3, 5.0), 5.3, 5.5),
    ((-19.4, 11.9), (-12.2, 10.2), 4.6, 5.0),
    ((-15.4, 18.3), (-11.4, 13.0), 3.9, 4.7),
    ((-10.4, 22.6), (-10.5, 14.5), 3.4, 3.4),
    ((-5.4, 24.6), (-5.0, 15.9), 3.5, 3.3),
    ((-.4, 25.0), (0.0, 16.6), 3.35, 3.4),
    ((4.6, 24.4), (5.1, 15.7), 3.3, 3.4),
    ((10.2, 22.4), (10.3, 14.4), 3.3, 3.35),
    ((15.1, 19.1), (11.5, 13.0), 3.8, 4.6),
    ((19.4, 13.6), (12.6, 10.3), 4.3, 5.1),
    ((23.0, 6.5), (13.8, 5.5), 5.05, 5.8),
    ((25.0, -.95), (15.2, -.55), 5.65, 6.3),
    ((29.6, -6.7), (16.1, -8.2), 7.4, 8.3),
    ((34.1, -15.1), (18.0, -17.5), 9.3, 9.8),
]
LEFT_SEAM, RIGHT_SEAM = 6, 10
SHELF_ORIGIN = (8.5, 17, 0)
SHELF_PROFILE = [
    ((2.0, 16.4), (2.9, 14.2), 2.7, 2.6),
    ((5.2, 19.8), (5.3, 13.6), 4.4, 4.2),
    ((9.7, 21.0), (9.1, 13.1), 4.6, 5.0),
    ((15.6, 19.3), (14.1, 13.4), 3.8, 4.9),
    ((23.9, 25.2), (18.4, 12.4), 6.8, 7.7),
    ((33.6, 26.0), (28.8, 15.6), 7.9, 11.2),
    ((47.2, 34.5), (38.2, 7.5), 13.9, 16.7),
    ((64.0, 37.0), (53.0, -3.0), 18.6, 20.4),
    ((83.0, 37.9), (72.0, -16.0), 21.8, 23.2),
]
SHELF_SEAM = 3
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
    export("RouteArchRoots", [
        (arch,0,LEFT_SEAM,False,True),
        (arch,RIGHT_SEAM,len(arch)-1,True,False),
    ])
    export("RouteArchShoulder", [(arch,LEFT_SEAM,RIGHT_SEAM,True,True)], SLAB_ORIGIN)
    shelf = rings_for(SHELF_PROFILE, .35)
    export("RouteShelfRoot", [(shelf, SHELF_SEAM, len(shelf)-1, True, False)])
    export("RouteShelfTip", [(shelf, 0, SHELF_SEAM, False, True)], SHELF_ORIGIN)


if __name__ == "__main__":
    main()
