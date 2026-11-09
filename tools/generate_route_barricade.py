"""Build the two-part steel barricade for the short course."""
from generate_siege_turret import box
from generate_submission_hazards import Mesh, ROOT


def main():
    panel = Mesh('RouteBarricadePanel', [
        (46, 52, 55), (75, 83, 86), (106, 109, 104), (172, 126, 52),
        (226, 167, 75), (27, 31, 33), (180, 172, 151),
    ])
    box(panel, (0, -.05, 0), (1.94, 1.64, 1.65), 0)
    box(panel, (-.86, 0, 0), (.24, 1.96, 1.92), 1)
    box(panel, (.86, 0, 0), (.24, 1.96, 1.92), 1)
    for z in (-.85, .85):
        box(panel, (0, -.55, z), (1.65, .25, .18), 2)
        box(panel, (0, .55, z), (1.65, .25, .18), 3)
        for i in range(4):
            x = -.65 + i*.38
            points = [(x, .43, z*1.12), (x+.13, .43, z*1.12),
                      (x+.24, .67, z*1.12), (x+.11, .67, z*1.12)]
            panel.face(points if z>0 else list(reversed(points)), 5)
        for x in (-.84, .84):
            for y in (-.49, .49):
                box(panel, (x, y, z*1.08), (.18, .14, .14), 6)
        box(panel, (0, .02, z*1.08), (.46, .22, .13), 4)
    box(panel, (0, -.87, 0), (1.94, .24, 1.96), 5)
    panel.save(ROOT/'Resources/course_meshes'/panel.name)


if __name__ == '__main__':
    main()
