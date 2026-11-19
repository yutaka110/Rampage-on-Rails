# Combat airframes

Original hard-surface airframes sharing light grey painted armour, navy frames,
exposed steel and small red/orange optics. Existing actor IDs and gameplay
definitions are retained.

| Mesh ID | Design | Triangles | Closed components |
| --- | --- | ---: | ---: |
| combat_sniper_hull | Heavy single-eye artillery drone | 2,752 | 29 |
| combat_interceptor_hull | Twin-engine forward-swept light fighter | 2,544 | 34 |

Both OBJ files contain the complete aircraft. The render queue deliberately
skips the legacy shared pods/core, expanded outline and sniper height stretch.
Armour is rigid; the existing banking, hover, recoil, charge and hit animation
states still control its pose and lighting.

## Material contract

Object3D shader mode 21 reads `floor(texcoord.x / 4)` as a surface selector.
Within each band, face UVs occupy .125–.875; do not wrap or repack these UVs
without changing the shader. V is flipped by the normal Assimp import.

| Band | Surface |
| ---: | --- |
| 0 | Light grey painted armour |
| 1 | Navy mechanical frame |
| 2 | Bare steel |
| 3 | Red/orange optical emitter |
| 4 | Matte intake/barrel cavity |
| 5 | Muted red unit markings |
| 6 | Small orange exhaust core |
| 7 | Dusty underside |

The optical emitter alone responds to weapon charge. The material receives
the normal combat scorch/emission projection and uses the course's lighting.
Normal shots additionally intersect the imported triangles so protruding
engines and wings receive hits beyond the existing forgiving central sphere.
Dead/untargetable actors are excluded from that additional query.

## Rebuild and review

Run `python tools/generate_combat_airframes.py` from the project root. Geometry
is authored with runtime -Z forward; export mirrors Z and reverses triangle
winding for Assimp's ConvertToLeftHanded import. Audit JSON files are generated
alongside the OBJ files. `python tools/check_combat_airframes.py` validates
winding, closed surfaces, UV bands and visibility into recessed functional parts.

After building and copying resources to the commercial executable directory,
run `python tools/capture_combat_airframes.py`. This captures ordinary moving
gameplay for both production actors: flight, centre hits on each type, and a
wing-tip hit. The opt-in `GE3_AIRFRAME_REVIEW` fixture adds review actors and aim
input only; its default is off and it changes no commercial camera/AI settings.
