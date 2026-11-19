"""Validate exported winding, material bands and recessed functional surfaces."""
from pathlib import Path
from collections import Counter
import math
from generate_combat_airframes import sub, cross, dot, unit

ROOT = Path(__file__).resolve().parents[1]

def load(folder, filename):
    vertices, uvs, normals, faces = [], [], [], []
    part = ''
    for line in (ROOT / 'Resources/enemies' / folder / filename).read_text().splitlines():
        a = line.split()
        if not a:
            continue
        if a[0] == 'g': part = a[1]
        if a[0] == 'v': vertices.append(tuple(map(float, a[1:])))
        if a[0] == 'vt': uvs.append(tuple(map(float, a[1:])))
        if a[0] == 'vn': normals.append(tuple(map(float, a[1:])))
        if a[0] == 'f':
            corners = [tuple(int(i)-1 for i in x.split('/')) for x in a[1:]]
            points = [vertices[c[0]] for c in corners]
            n = unit(cross(sub(points[1], points[0]), sub(points[2], points[0])))
            assert dot(n, normals[corners[0][2]]) > .99, (part, 'winding')
            bands = {int(uvs[c[1]][0] // 4) for c in corners}
            assert len(bands) == 1 and bands <= set(range(8)), (part, bands)
            # Restore runtime Z. The importer's handedness conversion flips
            # winding as well, so reorder the triangle before ray tests.
            points = [(p[0], p[1], -p[2]) for p in points]
            faces.append((part, [points[0], points[2], points[1]], bands.pop()))
    assert all(math.isfinite(x) for p in vertices for x in p)
    for part in {f[0] for f in faces}:
        edges = Counter()
        for _, points, _ in (f for f in faces if f[0] == part):
            for a, b in zip(points, points[1:] + points[:1]):
                edges[tuple(sorted((a, b)))] += 1
        assert all(v == 2 for v in edges.values()), (part, 'open/nonmanifold')
    print(folder, 'PASS', len(faces), 'triangles')
    return faces

def front_contact(faces, x, y):
    origin, direction = (x, y, -3), (0, 0, 1)
    hits = []
    for name, (a, b, c), band in faces:
        e1, e2 = sub(b, a), sub(c, a)
        h = cross(direction, e2)
        determinant = dot(e1, h)
        if abs(determinant) < 1e-8: continue
        s = sub(origin, a)
        u = dot(s, h) / determinant
        q = cross(s, e1)
        v = dot(direction, q) / determinant
        t = dot(e2, q) / determinant
        if u >= -1e-6 and v >= -1e-6 and u + v <= 1.000001 and t > 0:
            hits.append((t, name, band))
    return min(hits) if hits else None

sniper = load('CombatSniper', 'CombatSniperHull.obj')
interceptor = load('CombatInterceptor', 'CombatInterceptorHull.obj')
assert front_contact(sniper, 0, -.02)[1:] == ('optic_pupil', 3), 'optic occluded'
assert front_contact(sniper, .32, -.02)[1:] == ('muzzle_armour', 0), 'missing muzzle ring'
assert front_contact(sniper, 1.14, .25)[1:] == ('armour_1', 0)
assert front_contact(interceptor, 1.20, .10)[1] == 'intake_vane_1_0', 'intake hidden'
assert front_contact(interceptor, .19, -.24)[1:] == ('gun_lens_1', 3), 'gun lens hidden'
print('Recessed optics, cannon ring, armour, intake and gun lenses PASS')
