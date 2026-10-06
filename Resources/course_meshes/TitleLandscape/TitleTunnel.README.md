# Title cave mountain

Original procedural geometry created with AI coding assistance on 2026-09-30.
Regenerate from the repository root with `python tools/generate_title_tunnel.py`.
The runtime uses the existing gameplay terrain PBR material library (base colour,
normal, ORM and height arrays); these external textures are not claimed as
original. Their existing attribution and license review still apply.
The OBJ's sandstone MTL is a fallback; `CourseMeshRenderQueue` selects the title
PBR cave material and `Geometry.Terrain` draws it through the gameplay terrain PSO.

The passage extends straight into the mountain, with an irregular opening
roughly 17 units wide and 12 units high. An asymmetric mountain about
100 units wide and 45 units high surrounds it. The far end is sealed by rock
at depth 78, inside the outer wall at depth 80, so bright sky cannot show
through a second opening. The transition completes while the camera is only
4.6 units inside; the rear wall is never approached by the vehicle.

The mesh has 2,405 positions and 4,806 triangles. It is a closed rock solid
with a single entrance, shared seam vertices, validated winding and split
corner normals. OBJ Z is negated for Assimp's left-handed import.
UV U encodes the inner-wall mask and UV V encodes passage depth. Assimp flips V;
the shader restores depth as `1-V`. Rock color is projected from world position
on three axes. Only the interior darkens with depth.

The start action places the mouth 18 rail units ahead and fixes it in world
space. Travel stays at the idle speed of 12 units per second, with no launch
acceleration or speed-dependent zoom. A 1.0-second camera orbit settles at a
fixed distance of 8 and height 5.2. Camera FOV stays at 0.70 radians and the
rolling sound keeps its normal pitch. Only after the camera enters does the
fade hide the world handoff (2.55 seconds total, 4.6 units inside).
Travel is computed from elapsed time so frame rate does not change entry.
At activation, the current Bezier segment is split at the cart. All preceding
curve segments are retained and its outgoing tangent is extended into the
straight approach. The track is rebaked from this connected curve and line.
Scenery uses a separate straight coordinate frame to preserve its world
placement when the motion path retains the curved rear section. The title
ground stays flat out to radius 260 to support the longer departure at every
angle. Presentation never advances the playable course or combat.
