"""Build the two-part steel barricade for the short course."""
from generate_siege_turret import box
from generate_submission_hazards import Mesh, ROOT


def main():
    panel = Mesh('RouteBarricadePanel', [
        (19, 25, 32), (40, 48, 58), (52, 61, 70), (244, 247, 250),
        (179, 108, 43), (15, 19, 24), (101, 111, 119),
    ])
    box(panel, (0, -.05, 0), (1.94, 1.64, 1.65), 0)
    box(panel, (-.86, 0, 0), (.24, 1.96, 1.92), 1)
    box(panel, (.86, 0, 0), (.24, 1.96, 1.92), 1)
    for z in (-.85, .85):
        box(panel, (0, -.55, z), (1.65, .25, .18), 2)
        # Keep one continuous high-contrast band above the central latch.
        box(panel, (0, .72, z), (1.98, .50, .18), 3)
        for x in (-.84, .84):
            for y in (-.49, .49):
                box(panel, (x, y, z*1.08), (.18, .14, .14), 6)
        box(panel, (0, .02, z*1.08), (.46, .22, .13), 4)
    box(panel, (0, -.87, 0), (1.94, .24, 1.96), 5)
    panel.save(ROOT/'Resources/course_meshes'/panel.name)


if __name__ == '__main__':
    main()
