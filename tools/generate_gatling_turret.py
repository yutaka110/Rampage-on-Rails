"""Carrier rapid battery: fixed armour and a separate six-barrel rotor.

Uses the project's palette OBJ writer; authoring +Z becomes runtime -Z.
Rotor origin is the barrel axis. Shared runtime pivot is (0, .32, 0).
"""
from generate_siege_turret import Mesh, ROOT, PALETTE, box, tube
import math


def main():
    head = Mesh('GatlingTurretHead', PALETTE)
    box(head, (0, .23, -.16), (1.62, .90, 1.12), 0)
    box(head, (0, .76, -.24), (1.40, .18, .94), 1)
    for side in (-1, 1):
        box(head, (side * .79, .32, -.02), (.22, 1.06, 1.36), 1)
        box(head, (side * .91, .29, -.10), (.12, .65, .94), 6)
        for z in (-.42, -.21, 0, .21):
            box(head, (side * .98, .36, z), (.06, .46, .065), 4)
    tube(head, 0, .32, [(-.05, .72), (.38, .72), (.56, .62)], 0, n=18)
    tube(head, 0, .32, [(.56, .62), (.56, .55)], 2, n=18)
    rotor = Mesh('GatlingTurretRotor', PALETTE)
    tube(rotor, 0, 0, [(.25, .53), (.66, .53)], 6, n=18, cap=True)
    for i in range(6):
        a = i * math.tau / 6
        x, y = .40 * math.cos(a), .40 * math.sin(a)
        tube(rotor, x, y, [(.56, .15), (1.82, .15), (1.94, .18), (2.25, .18)], 0)
        for z in (.78, 1.0, 1.22):
            tube(rotor, x, y, [(z, .17), (z + .06, .17)], 1)
        tube(rotor, x, y, [(2.25, .18), (2.25, .095)], 2)
        tube(rotor, x, y, [(2.02, .095), (2.25, .095)], 3, inside=True)
        tube(rotor, x, y, [(2.015, .09), (2.02, .09)], 3, cap=True)
    for z in (1.40, 1.76):
        tube(rotor, 0, 0, [(z, .60), (z + .11, .60)], 1, n=18)
        tube(rotor, 0, 0, [(z + .11, .60), (z + .11, .53)], 2, n=18)
    for mesh in (head, rotor):
        mesh.save(ROOT / 'Resources/enemies' / mesh.name)


if __name__ == '__main__':
    main()
