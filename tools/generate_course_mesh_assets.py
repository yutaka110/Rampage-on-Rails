from __future__ import annotations

import math
import os
import random
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MESH_ROOT = ROOT / "Resources" / "course_meshes"


def normalize(v):
    x, y, z = v
    length = math.sqrt(x * x + y * y + z * z)
    if length <= 1.0e-6:
        return (0.0, 1.0, 0.0)
    return (x / length, y / length, z / length)


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def noise2(x, y, seed=0):
    value = math.sin(x * 12.9898 + y * 78.233 + seed * 37.719) * 43758.5453
    return value - math.floor(value)


class Mesh:
    def __init__(self, name: str, material: str):
        self.name = name
        self.material = material
        self.vertices = []
        self.uvs = []
        self.normals = []
        self.faces = []

    def add_vertex(self, pos, uv, normal):
        self.vertices.append(pos)
        self.uvs.append(uv)
        self.normals.append(normalize(normal))
        return len(self.vertices)

    def add_face(self, a, b, c):
        self.faces.append((a, b, c))

    def add_grid(self, fn, u_count: int, v_count: int, flip=False):
        def add_grid_face(a, b, c):
            # Sharp displacement can make a central-difference vertex normal
            # oppose an individual triangle. The importer then flips just that
            # triangle, leaving a backface-culling hole in an otherwise intact wall.
            pa, pb, pc = (self.vertices[i - 1] for i in (a, b, c))
            face_normal = normalize(cross(sub(pb, pa), sub(pc, pa)))
            average = tuple(sum(self.normals[i - 1][axis] for i in (a, b, c))
                            for axis in range(3))
            if sum(face_normal[i] * average[i] for i in range(3)) < 0.0:
                a, b, c = (self.add_vertex(self.vertices[i - 1], self.uvs[i - 1], face_normal)
                           for i in (a, b, c))
            self.add_face(a, b, c)

        ids = []
        positions = []
        for y in range(v_count + 1):
            row = []
            pos_row = []
            v = y / v_count
            for x in range(u_count + 1):
                u = x / u_count
                pos = fn(u, v)
                row.append(None)
                pos_row.append(pos)
            ids.append(row)
            positions.append(pos_row)

        for y in range(v_count + 1):
            for x in range(u_count + 1):
                xm = max(0, x - 1)
                xp = min(u_count, x + 1)
                ym = max(0, y - 1)
                yp = min(v_count, y + 1)
                du = sub(positions[y][xp], positions[y][xm])
                dv = sub(positions[yp][x], positions[ym][x])
                normal = normalize(cross(dv, du) if flip else cross(du, dv))
                ids[y][x] = self.add_vertex(
                    positions[y][x],
                    (x / u_count, 1.0 - y / v_count),
                    normal,
                )

        for y in range(v_count):
            for x in range(u_count):
                a = ids[y][x]
                b = ids[y][x + 1]
                c = ids[y + 1][x + 1]
                d = ids[y + 1][x]
                if flip:
                    add_grid_face(a, c, b)
                    add_grid_face(a, d, c)
                else:
                    add_grid_face(a, b, c)
                    add_grid_face(a, c, d)

    def write(self, directory: Path, filename: str, mtl_name: str):
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / filename
        with path.open("w", encoding="utf-8", newline="\n") as out:
            out.write(f"mtllib {mtl_name}\n")
            out.write(f"o {self.name}\n")
            for x, y, z in self.vertices:
                out.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
            for u, v in self.uvs:
                out.write(f"vt {u:.6f} {v:.6f}\n")
            for x, y, z in self.normals:
                out.write(f"vn {x:.6f} {y:.6f} {z:.6f}\n")
            out.write(f"usemtl {self.material}\n")
            for a, b, c in self.faces:
                out.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")
        return path


def write_bmp(path: Path, width: int, height: int, sampler):
    path.parent.mkdir(parents=True, exist_ok=True)
    row_stride = ((width * 3 + 3) // 4) * 4
    pixel_data = bytearray()
    for y in range(height - 1, -1, -1):
        row = bytearray()
        for x in range(width):
            r, g, b = sampler(x / max(1, width - 1), y / max(1, height - 1))
            row += bytes((max(0, min(255, int(b))), max(0, min(255, int(g))), max(0, min(255, int(r)))))
        row += b"\x00" * (row_stride - width * 3)
        pixel_data += row

    header_size = 14 + 40
    file_size = header_size + len(pixel_data)
    with path.open("wb") as out:
        out.write(b"BM")
        out.write(struct.pack("<IHHI", file_size, 0, 0, header_size))
        out.write(struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, len(pixel_data), 2835, 2835, 0, 0))
        out.write(pixel_data)


def rock_sampler(base):
    def sample(u, v):
        strata = 0.5 + 0.5 * math.sin(v * 54.0 + math.sin(u * 18.0) * 2.0)
        grain = noise2(u * 31.0, v * 37.0, 3)
        cavity = noise2(u * 92.0, v * 88.0, 9)
        shade = 0.68 + strata * 0.18 + grain * 0.12 - cavity * 0.10
        cool = 0.10 * (1.0 - v)
        return (
            base[0] * shade + 18 * cool,
            base[1] * shade + 26 * cool,
            base[2] * shade + 34 * cool,
        )
    return sample


def write_mtl(directory: Path, name: str, texture: str):
    with (directory / f"{name}.mtl").open("w", encoding="utf-8", newline="\n") as out:
        out.write(f"newmtl {name}\n")
        out.write("Ka 0.18 0.16 0.14\n")
        out.write("Kd 0.78 0.66 0.54\n")
        out.write("Ks 0.10 0.09 0.08\n")
        out.write("Ns 24.0\n")
        out.write(f"map_Kd ../materials/{texture}\n")


def eroded(u, v, strength=0.035, seed=0):
    return (noise2(u * 6.0, v * 9.0, seed) - 0.5) * strength


def smooth_noise(x, y, seed=0):
    ix, iy = math.floor(x), math.floor(y)
    tx, ty = x - ix, y - iy
    tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
    a = noise2(ix, iy, seed) * (1 - tx) + noise2(ix + 1, iy, seed) * tx
    b = noise2(ix, iy + 1, seed) * (1 - tx) + noise2(ix + 1, iy + 1, seed) * tx
    return (a * (1 - ty) + b * ty) * 2 - 1


def add_closed_rock_patch(mesh, surface, u_count, v_count):
    """A solid slab with identical boundary samples on all six surfaces.

    surface(u, v, depth) supplies both faces and their connected perimeter;
    depth runs from -1 to 1. No independently displaced seam or loose legs.
    """
    mesh.add_grid(lambda u, v: surface(u, v, -1), u_count, v_count, flip=True)
    mesh.add_grid(lambda u, v: surface(u, v, 1), u_count, v_count)
    mesh.add_grid(lambda u, v: surface(u, 0, v * 2 - 1), u_count, 8)
    mesh.add_grid(lambda u, v: surface(u, 1, v * 2 - 1), u_count, 8, flip=True)
    mesh.add_grid(lambda u, v: surface(0, v, u * 2 - 1), 8, v_count)
    mesh.add_grid(lambda u, v: surface(1, v, u * 2 - 1), 8, v_count, flip=True)


def rock_arch_surface(u, v, depth, seed, tunnel=False, inner_variant=False):
    angle = math.pi * (1 - u)
    sine, cosine = math.sin(angle), math.cos(angle)
    # The aperture and crown share the same full-height supports. The old
    # semicircle stopped above the floor, with disconnected rectangular legs.
    crown = smooth_noise(u * 8, 0, seed) * 0.11 + math.sin(u * 17 + 0.7) * 0.035
    inner_width = (0.94 if tunnel else 0.51) + smooth_noise(u * 5, 2, seed) * 0.035
    outer_width = (1.40 if tunnel else 1.08) + smooth_noise(u * 7, 4, seed) * 0.14
    # Broad shoulders continue into the surrounding cliff. Keep the inner
    # aperture intact: all large changes grow towards the outside of the arch.
    shoulder = abs(cosine) ** 0.65 * (0.80 if tunnel else 1.12)
    shoulder *= (0.70 + 0.60 * u) if inner_variant else (1.20 - 0.45 * u)
    outer_width += shoulder
    width = inner_width * (1 - v) + outer_width * v
    width += abs(cosine) ** 6 * v * 0.20
    notch_center = 0.40 if inner_variant else 0.62
    notch = math.exp(-((u - notch_center) / 0.035) ** 2) * 0.28
    notch += math.exp(-((u - 0.24) / 0.025) ** 2) * 0.14
    outer_height = 1.94 + smooth_noise(u * 5, 7, seed) * 0.22 - notch
    height = ((1.46 if tunnel else 1.16) * (1 - v) + outer_height * v) + crown
    x = cosine * width + sine * (0.07 + v * v * (-0.16 if inner_variant else 0.12))
    y = -1.10 + sine * height
    relief = smooth_noise(u * 9, v * 4, seed + 1) * 0.32
    relief += smooth_noise(u * 24, v * 9, seed + 2) * 0.055
    # Recessed oblique fractures break up the front face at a readable scale.
    fracture = math.exp(-((u - (0.30 + v * 0.075)) / 0.018) ** 2)
    fracture += math.exp(-((u - (0.72 - v * 0.045)) / 0.014) ** 2) * 0.7
    thickness = 0.82 + smooth_noise(u * 6, v * 3, seed + 3) * 0.24
    thickness *= 0.46 + 0.54 * math.sin(math.pi * v) ** 0.65
    thickness *= 1 - fracture * 0.30
    if tunnel:
        # Receding, irregular ribs, rather than a thin open cylindrical sheet.
        relief += math.sin(depth * 12 + u * 3) * 0.035 * (1 - depth * depth)
        thickness *= 1.55
    return (x, y, relief + depth * thickness)


def add_fractured_boulder(mesh, center, radii, seed):
    # Closed low-poly rocks are baked into the arch draw, not extra actors.
    rings, slices = 6, 12
    positions = [(0, -1, 0)]
    for row in range(1, rings):
        a = -math.pi * 0.5 + math.pi * row / rings
        for col in range(slices):
            b = math.tau * col / slices
            positions.append((math.cos(a) * math.cos(b), math.sin(a), math.cos(a) * math.sin(b)))
    positions.append((0, 1, 0))
    transformed = []
    for x, y, z in positions:
        chip = 1 + smooth_noise(x * 3 + z, y * 3, seed) * 0.19
        transformed.append(tuple(center[i] + (x, y, z)[i] * radii[i] * chip for i in range(3)))

    def face(a, b, c):
        pa, pb, pc = (transformed[i] for i in (a, b, c))
        n = normalize(cross(sub(pb, pa), sub(pc, pa)))
        mesh.add_face(*(mesh.add_vertex(p, (p[0], p[1]), n) for p in (pa, pb, pc)))

    for col in range(slices):
        nxt = (col + 1) % slices
        face(0, 1 + col, 1 + nxt)
        for row in range(rings - 2):
            a, b = 1 + row * slices + col, 1 + row * slices + nxt
            c, d = a + slices, b + slices
            face(a, c, b)
            face(b, c, d)
        face(len(positions) - 1, 1 + (rings - 2) * slices + nxt,
             1 + (rings - 2) * slices + col)


def add_arch_cliff_roots(mesh, seed, tunnel):
    for side in (-1, 1):
        # A continuous buried apron climbs from the foot into the side wall.
        # Its top and sides share exactly the same perimeter samples.
        inner = 1.02 if tunnel else 0.74
        width = 2.25 if tunnel else 2.60
        def apron(u, v, depth, side=side):
            x = side * (inner + width * u)
            z = side * (1 - 2 * v) * (1.8 + u * 0.65)
            peak = math.exp(-((u - 0.26) / 0.25) ** 2 - ((v - 0.5) / 0.40) ** 2)
            top = -1.10 + peak * 0.42 + u * u * 0.62
            top += smooth_noise(u * 7, v * 6, seed + (0 if side < 0 else 9)) * 0.055 * math.sin(math.pi * u)
            return x, -1.42 + (top + 1.42) * ((depth + 1) * 0.5), z
        # Broad aprons need fewer samples than the visible arch fractures.
        # Keep Assimp's expanded corner stream below the editor's 65535 cap.
        add_closed_rock_patch(mesh, apron, 24, 16)
        for i in range(6):
            t = i / 5
            x = side * (inner + 0.22 + t * 1.30)
            z = (-1 if i % 2 else 1) * (0.55 + noise2(i, side, seed) * 0.80)
            y = -0.98 + math.sin(t * math.pi) * 0.12
            add_fractured_boulder(mesh, (x, y, z),
                (0.15 + t * 0.06, 0.10 + noise2(i, 2, seed) * 0.08, 0.32 + t * 0.18),
                seed + i * 37 + (0 if side < 0 else 19))


def make_organic_arch():
    mesh = Mesh("OrganicArchLarge_HD", "organic_rock")
    add_closed_rock_patch(mesh, lambda u, v, d: rock_arch_surface(u, v, d, 11), 96, 24)
    add_arch_cliff_roots(mesh, 111, False)
    return mesh


def make_rib_tunnel(inner_variant=False):
    mesh = Mesh("RibTunnelInner_HD" if inner_variant else "RibTunnelWall_HD", "rib_rock")
    seed = 57 if inner_variant else 21
    add_closed_rock_patch(mesh,
        lambda u, v, d: rock_arch_surface(u, v, d, seed, True, inner_variant), 96, 24)
    add_arch_cliff_roots(mesh, seed + 100, True)
    return mesh


def make_root_spire():
    mesh = Mesh("RootSpireColumn_HD", "root_rock")
    rings, slices = 86, 56
    ids = []
    for i in range(rings + 1):
        t = i / rings
        y = -1.0 + 2.0 * t
        twist = t * 2.4
        radius = 0.30 * (1.0 - t) + 0.14 * t + 0.035 * math.sin(t * 31.0)
        row = []
        for j in range(slices):
            a = (j / slices) * math.tau + twist
            lobe = 1.0 + 0.18 * math.sin(a * 3.0 + t * 9.0) + 0.08 * math.sin(a * 7.0)
            r = radius * lobe
            x = math.cos(a) * r
            z = math.sin(a) * r
            n = normalize((math.cos(a), 0.12 * math.sin(t * math.pi), math.sin(a)))
            row.append(mesh.add_vertex((x, y, z), (j / slices, 1.0 - t), n))
        ids.append(row)
    # Match the smooth normal to the actual taper and twist, including the
    # periodic seam. Radial normals alone hide the vertical stone profile.
    for i in range(rings + 1):
        for j in range(slices):
            dy = sub(mesh.vertices[ids[min(i + 1, rings)][j] - 1],
                     mesh.vertices[ids[max(i - 1, 0)][j] - 1])
            da = sub(mesh.vertices[ids[i][(j + 1) % slices] - 1],
                     mesh.vertices[ids[i][(j - 1) % slices] - 1])
            mesh.normals[ids[i][j] - 1] = normalize(cross(dy, da))
    for i in range(rings):
        for j in range(slices):
            a = ids[i][j]
            b = ids[i][(j + 1) % slices]
            c = ids[i + 1][(j + 1) % slices]
            d = ids[i + 1][j]
            mesh.add_face(a, c, b)
            mesh.add_face(a, d, c)
    for root_index, angle in enumerate((0.35, 2.45, 4.35)):
        mesh.add_grid(
            lambda u, v, a=angle: (
                math.cos(a + u * 0.45) * (0.20 + v * 0.78),
                -0.92 + u * 0.55,
                math.sin(a + u * 0.45) * (0.20 + v * 0.78),
            ),
            36,
            5,
        )
    return mesh


def make_curved_wall():
    mesh = Mesh("CurvedCanyonWall_HD", "organic_rock")
    def surface(u, v, depth):
        skyline = 0.80 + smooth_noise(u * 7, 0, 31) * 0.20
        x = (u * 2 - 1) * (1 + math.sin(v * 5) * 0.07)
        y = -1.18 + (skyline + 1.18) * v
        z = 0.24 * math.cos((u - 0.5) * math.pi)
        z += smooth_noise(u * 8, v * 10, 32) * 0.07
        return x, y, z + depth * (0.16 + 0.08 * (1 - v))
    add_closed_rock_patch(mesh, surface, 80, 36)
    return mesh


def make_vista_hole_wall():
    mesh = Mesh("VistaHoleWall_HD", "vista_rock")
    # A closed weathered buttress, with an irregular skylight bored through
    # its thickness. A polar rectangle keeps the four outer sides connected.
    def surface(u, v, depth):
        a = u * math.tau
        ca, sa = math.cos(a), math.sin(a)
        inner = 0.35 + smooth_noise(ca * 3 + 4, sa * 3 + 4, 41) * 0.055
        outer = 1 / max(abs(ca) / 1.3, abs(sa))
        outer *= 1 + smooth_noise(ca * 5 + 8, sa * 5 + 8, 42) * 0.12
        r = inner * (1 - v) + outer * v
        x, y = ca * r, sa * r - 0.18
        if y < -0.18:
            y = -0.18 + (y + 0.18) * 1.65  # bury the foot beneath the canyon floor
        z = smooth_noise(x * 4, y * 4, 43) * 0.10 + depth * (0.6 + v * 0.18)
        return x, y, z
    # Periodic u perimeter: no end caps, whose overlapping copies would flicker.
    mesh.add_grid(lambda u, v: surface(u, v, -1), 128, 24)
    mesh.add_grid(lambda u, v: surface(u, v, 1), 128, 24, flip=True)
    mesh.add_grid(lambda u, v: surface(u, 0, v * 2 - 1), 128, 8, flip=True)
    mesh.add_grid(lambda u, v: surface(u, 1, v * 2 - 1), 128, 8)
    return mesh


def make_broken_bridge():
    mesh = Mesh("SpireBrokenBridgeArc_HD", "root_rock")
    mesh.add_grid(
        lambda u, v: (
            -1.0 + 2.0 * u,
            -0.10 + 0.20 * v,
            0.25 * math.sin(u * math.pi * 0.85) + 0.10 * math.sin(u * math.pi * 3.0) + eroded(u, v, 0.035, 51),
        ),
        90,
        10,
    )
    mesh.add_grid(
        lambda u, v: (
            -1.0 + 2.0 * u,
            -0.26 + 0.08 * v,
            0.25 * math.sin(u * math.pi * 0.85) + 0.10 * math.sin(u * math.pi * 3.0) - 0.18,
        ),
        90,
        4,
        flip=True,
    )
    return mesh


def main():
    material_dir = MESH_ROOT / "materials"
    write_bmp(material_dir / "organic_rock_albedo.bmp", 256, 256, rock_sampler((142, 104, 78)))
    write_bmp(material_dir / "rib_rock_albedo.bmp", 256, 256, rock_sampler((118, 95, 86)))
    write_bmp(material_dir / "root_rock_albedo.bmp", 256, 256, rock_sampler((112, 80, 60)))
    write_bmp(material_dir / "vista_rock_albedo.bmp", 256, 256, rock_sampler((154, 139, 132)))

    specs = [
        ("OrganicArchLarge", "OrganicArchLarge.obj", "organic_rock", "organic_rock_albedo.bmp", make_organic_arch()),
        ("RibTunnelWall", "RibTunnelWall.obj", "rib_rock", "rib_rock_albedo.bmp", make_rib_tunnel()),
        ("RibTunnelInner", "RibTunnelInner.obj", "rib_rock", "rib_rock_albedo.bmp", make_rib_tunnel(True)),
        ("RootSpireColumn", "RootSpireColumn.obj", "root_rock", "root_rock_albedo.bmp", make_root_spire()),
        ("CurvedCanyonWall", "CurvedCanyonWall.obj", "organic_rock", "organic_rock_albedo.bmp", make_curved_wall()),
        ("VistaHoleWall", "VistaHoleWall.obj", "vista_rock", "vista_rock_albedo.bmp", make_vista_hole_wall()),
        ("SpireBrokenBridgeArc", "SpireBrokenBridgeArc.obj", "root_rock", "root_rock_albedo.bmp", make_broken_bridge()),
    ]

    for folder, filename, material, texture, mesh in specs:
        directory = MESH_ROOT / folder
        directory.mkdir(parents=True, exist_ok=True)
        write_mtl(directory, material, texture)
        path = mesh.write(directory, filename, f"{material}.mtl")
        print(f"{path.relative_to(ROOT)}: {len(mesh.vertices)} verts, {len(mesh.faces)} tris")


if __name__ == "__main__":
    random.seed(1234)
    main()
