"""Generate the layered sandstone mesa used in the canyon vistas."""

from math import cos, hypot, sin, tau
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[1]
    asset = root / "Resources/course_meshes/CanyonMesa/CanyonMesa.obj"
    asset.parent.mkdir(parents=True, exist_ok=True)

    sides = 24
    strata = ((-1.0, 1.05), (-.61, .92), (-.31, .72), (-.18, .71),
              (-.11, .62), (.39, .53), (.52, .56), (.59, .52),
              (.89, .47), (.98, .43))
    rings = []
    for level, (y, radius) in enumerate(strata):
        ring = []
        for index in range(sides):
            angle = tau * index / sides
            cleft = .09 * sin(3 * angle + .31) + .07 * cos(5 * angle - .17)
            r = radius * (1 + cleft + .027 * sin(index * 2.27 + level * .64))
            ring.append((r * cos(angle) + .08 * y,
                         y + .025 * sin(angle * 3 + level * .51),
                         r * sin(angle) - .06 * y))
        rings.append(ring)

    lines = ["# Terraced sandstone mesa, runtime placement supplies world scale.", "o CanyonMesa"]
    count = 0

    def triangle(a, b, c):
        nonlocal count
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        normal = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                  u[0] * v[1] - u[1] * v[0])
        length = hypot(*normal)
        normal = tuple(value / length for value in normal)
        for x, y, z in (a, b, c):
            lines.extend((f"v {x:.6f} {y:.6f} {z:.6f}",
                          f"vt {.5 + x * .27 + z * .16:.6f} {.5 + y * .43:.6f}",
                          "vn %.6f %.6f %.6f" % normal))
        lines.append("f " + " ".join(f"{i}/{i}/{i}" for i in range(count + 1, count + 4)))
        count += 3

    for lower, upper in zip(rings, rings[1:]):
        for index in range(sides):
            following = (index + 1) % sides
            triangle(lower[index], upper[index], upper[following])
            triangle(lower[index], upper[following], lower[following])
    for index in range(1, sides - 1):
        triangle(rings[0][0], rings[0][index], rings[0][index + 1])
        triangle(rings[-1][0], rings[-1][index + 1], rings[-1][index])
    asset.write_text("\n".join(lines) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
