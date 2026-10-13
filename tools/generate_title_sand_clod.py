"""Original irregular cohesive soil lump, centred for cinematic falling poses.

Uses the project's closed-solid validator and OBJ writer. No downloaded mesh.
"""
import math
import uuid
from generate_title_landscape import Mesh, ROOT, OUT, cross


def main():
    mesh = Mesh('TitleSandClod')
    segments = 12
    bottom = mesh.vertex((-0.08, -0.76, 0.06))
    rings = []
    for level, (height, radius) in enumerate(((-0.48, 0.56), (0.0, 0.86), (0.46, 0.63))):
        row = []
        for i in range(segments):
            angle = math.tau * i / segments
            outline = 1 + 0.12 * math.sin(3*angle + level*1.3) + 0.07 * math.cos(5*angle + 0.8)
            y = height + 0.08 * math.sin(2*angle + level*1.1)
            row.append(mesh.vertex((radius*outline*math.cos(angle) + 0.10*height,
                                    y, radius*outline*math.sin(angle))))
        rings.append(row)
    top = mesh.vertex((0.14, 0.77, -0.09))
    for i in range(segments):
        j = (i+1) % segments
        mesh.face(bottom, rings[0][i], rings[0][j])
        mesh.face(top, rings[-1][j], rings[-1][i])
    for lower, upper in zip(rings, rings[1:]):
        for i in range(segments):
            j = (i+1) % segments
            mesh.face(lower[i], upper[i], upper[j], lower[j])
    volume = sum(sum(mesh.vertices[f[0]][i]*cross(mesh.vertices[f[1]], mesh.vertices[f[2]])[i]
                     for i in range(3))/6 for f in mesh.faces)
    if volume < 0:
        mesh.faces = [tuple(reversed(f)) for f in mesh.faces]
    mesh.save()
    asset = OUT / 'TitleSandClod.obj'
    logical = asset.relative_to(ROOT).as_posix()
    asset.with_suffix('.obj.meta').write_text(
        f'guid={uuid.uuid5(uuid.NAMESPACE_URL, logical).hex}\nlogicalPath={logical}\n', encoding='ascii')


if __name__ == '__main__':
    main()
