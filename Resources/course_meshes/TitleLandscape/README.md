# Original title landscape

Original geometry generated from `tools/generate_title_landscape.py`. Runtime shading uses the same PBR material library and texture arrays as gameplay terrain; external material attribution is separate from the original geometry.

- `TitleGround.obj`: one connected closed disk, radius 300 m. The rail/camera corridor (radius 60–92 m) is flat at y=-0.30 m, just below the sleepers. Broad shallow undulation appears away from the track. The outer skirt closes underneath the ground.
- `TitleCliff.obj`: a closed plateau with broad irregular contours and gentle taper, reused at three clearly separated locations.
- `TitleBoulder.obj`: a closed rounded rock used at five sparse locations away from the track and camera.
- `title_sandstone.bmp`: original solid sandstone albedo retained as the OBJ/MTL fallback. Runtime landscape materials use the gameplay dry rock / wet rock / floor sand PBR layers instead.

The generator verifies every edge is shared by exactly two oppositely wound faces, rejects degenerate triangles, and verifies outward-facing shells. Shared vertices and area-weighted normals preserve continuous surfaces at the radial seam.

Regenerate from the repository root: `python tools/generate_title_landscape.py`.
The C++ regression imports these exact OBJ assets and checks ground support around both rails plus camera visibility over a full lap and repeated start transitions.

## Imported surface and title shading checks

The title key light borrows the opening course sun hue at normalized outdoor luminance. Diffuse fill on ground and rails follows a partially desaturated version of that hue, and sky/haze use a warm dusty colour. A mild warm terrain tint keeps Ground054's original maps, grain, normal response and roughness intact. Gameplay material and lighting presets are unchanged.

Title meshes are audited after Assimp import as well: welded-position edges must form closed, consistently oriented solids without duplicate or degenerate triangles. Invalid title meshes are rejected before GPU upload. The generator also checks face normals against the stored smooth normals, preventing isolated winding repairs.

All four landscape meshes (including the departure cave) are submitted once through `Geometry.Terrain` using the gameplay `Terrain.PS.hlsl` and terrain PSO, even while gameplay terrain streaming is disabled. They bind the shared base colour, normal, ORM and height texture arrays, material library, detail cache and environment map. Texture/material layers 0..2 retain the gameplay materials; layer 3 is Ground054 for the title ground only. World-space triplanar projection and slope/height blending keep texture scale consistent across differently scaled meshes. The terrain shader supplies GGX lighting, roughness, AO, mapped normals and environment reflections. `BuildTitleLandscapePbrMaterial` controls title-only detail/contrast using the terrain constant-buffer layout.

Modes 6/7/9 identify authored title OBJ surfaces (mode 9 selects the separate Ground054 slice): their UVs must not be decoded as gameplay terrain's packed contact-AO/variation data. Mode 7 retains the cave interior/depth mask (`U`, `1-V` after Assimp import). `TitleLighting.hlsli` shares the extended camera ABI, moving cart contact shadow and distance haze between the cart and PBR terrain. Title surfaces use the cart shadow instead of stale gameplay cascade data, and add diffuse sky irradiance for the clear-colour title sky alongside the shared specular IBL. Outdoor haze blends into the title sky between 48 and 260 m. Mode 8 previews the opening gameplay cart colour only during the title. The title restores gameplay lighting/background on handoff.

## Title cart suspension

The shared cart mesh has 3.5–5 cm planar chamfers on body, trim and running gear.
All ten components remain closed solids (440 triangles total); the original
bounds and independently solved wheels are preserved. Chamfer UVs carry a U+2
tag against the shared white fallback albedo. Title mode 8 uses this tag for
irregular edge paint wear, sparse filtered scratches, restrained panel variation
and matte sandy lower panels, varying GGX roughness and exposed iron metalness.
Weathering is attached to authored UVs and never uses time or world scrolling.
Gameplay keeps its existing material shader while sharing the refined geometry.

The title cart retains the opening gameplay palette as its colour reference,
with per-face GGX lighting, hemisphere fill and restrained environment reflection.
Mode 8 carries separate paint/iron roughness and metalness. Mode 13 shades the
wheel normals as iron; mode 12 lets rails and sleepers receive the same contact
shadow as the ground. Each wheel has a narrow dense contact core and a softer
footprint that broadens farther below the rail. Ground sky fill is occluded under
the body without flattening the surrounding sand. Track material slots reset to
their gameplay defaults each frame, so title modes do not survive the handoff.

`RailTitleScene` applies local suspension only to the cart body render matrix:
up to 3.8 cm of vertical bounce, 1.5 cm of lateral sway, 0.82 degrees of roll and
0.26 degrees of pitch. Two rail-distance vibration phases and a slower sway
use unwrapped double-precision travel, preserving phase through laps and pauses.
Wheel contacts, wheel rotation, camera framing and attack/dust animation retain
their existing poses. The body motion fades under the departure blackout.

## Foreground speed markers

The title loop has 24 fixed roadside timber stakes on the camera side, spaced
about 18.3 m apart (one passes every 1.5 seconds at the existing 12 m/s speed).
Another 36 low rocks use the existing PBR boulder with irregular spacing,
rotation and scale. Both stay outside the rails, with their bases buried in
the flat ground corridor. All positions cover one complete lap and remain
world-fixed through wrapping and the departure orbit.

`TitleStake.obj` is an original 48-triangle closed, bevelled timber solid with
a slightly uneven top and worn pale band. Its matte wood albedo is original.
Regenerate only these new stake assets with
`python tools/generate_title_roadside_props.py`.

## Title projectile shape

Title shots use a camera-facing 1.8 m circular glowing head with a warm white
centre and orange-red halo. Their directional trail is at most 1.8 m long,
0.52 m wide and dimmer than the head, keeping the round body readable while
showing travel direction. This changes only the title tracer presentation;
shot timing, trajectory and ground impact dust retain their existing behavior.

## Impact energy lighting

Impact light stays at the world-space contact point: a 100 ms hot flash, an
irregular orange ground patch and distance/height-attenuated light through the
existing dust billows fade within 340 ms. Dust density, shape and trajectories
remain unchanged. Ten deterministic ballistic hot fragments cool in 240–348 ms.
All light and sparks use the existing depth-tested additive title tracer pass;
the smoke retains its sorted alpha pass and no new GPU particle pool reset occurs.
