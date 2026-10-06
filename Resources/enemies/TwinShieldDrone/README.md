# Twin shield mining drone

Original game mesh reconstructed in code from the user-supplied AI concept image
(`ChatGPT 画像 2026年10月6日 14_12_18.png`). This is a modeled interpretation,
not a claim of exact image-to-3D reconstruction. The unseen rear is authored.
No Tripo/Meshy service, purchased mesh, or external texture is used.

Run `python tools/generate_twin_shield_drone.py` from the repository root.
The generator requires NumPy and writes deterministic geometry and textures.

- `TwinShieldHull.obj`: faceted body, recessed sensor housing, two hollow gun
  barrels, dorsal thrusters, pistons, hinges, vents and fasteners.
- `TwinShieldPanel.obj`: thick shield with actual upper/lower through apertures,
  bright steel bevels, yellow markings and rear reinforcement. Instanced twice.
- `TwinShieldCore.obj`: layered red/orange/amber sensor lens, separate emissive part.
- `TwinShieldDrone.obj`: complete assembled neutral model for inspection/export.
- `TwinShieldDrone_Color.png`: 1024-square original weathered industrial atlas.
- `TwinShieldDrone_NormalRoughness.dds`: linear RGBA8 normal RGB / roughness alpha.
  DDS is intentional: the generic PNG importer requests sRGB textures.

The assembled enemy has 4,764 triangles. Geometry is authored right-handed with
+Z facing the camera and barrel openings; the Assimp loader converts to runtime -Z.
Runtime uses four rigid draws: hull, two shields and sensor. Shields open with
weapon charge and separate slightly on death; sensor emission follows charge.
Old sphere squash, side pods and full-body colour flashes are bypassed; hits use
a brief dedicated material flash. Damage, aiming, health and collision data retain
their existing actor definitions. A shield is visual armour, not a new immunity mechanic.

`drone_scout`, `drone_basic`, `drone_leader` and `drone_chaser` use the new model.
Object material mode 10 adds derivative normal mapping and GGX metallic/roughness
lighting; mode 11 lights the textured sensor independently of scene illumination.

The title scene submits the same four parts through a presentation-only
`TwinShieldDronePose`. It follows 29-35 rail metres behind the cart, weaving and
banking slightly inside the curve so its silhouette stays clear of the logo.
It alternates the two gun muzzles in a three-shot burst every 4.8 seconds. Orange
world-space bolts lead the moving cart and deliberately strike the flat sand
corridor beside the rails. Each impact produces a flash, grit and an expanding
sand-coloured alpha cloud. The live bolt uses a camera-facing world ribbon along
its flight direction, with a cream core, a saturated red-orange glow and a tapered
3.6 m tail. A 0.6 m camera-facing luminous head stays readable when the flight
direction faces the camera; the ribbon is 0.92 m wide before its soft falloff.
`VFX.TitleTracers` draws additive ribbons/heads and a 90 ms contact flash;
the older cylinder effect remains available as an editor preview asset.
Bounded world-space cloud state in `RailTitleScene` lasts 2.15 seconds. Each
impact has eight low spreading puffs, ten rising lobes and 36 deterministic
sand grains. Grains have horizontal drag, gravitational fall and a ground stop.
Alpha billboards sort back to front, retain homogeneous clip depth and draw
through `VFX.TitleGroundDust` into SceneColor with read-only depth before
post-processing. `TitleDust.PS.hlsl` supplies seeded multi-scale density,
irregular edges, internal shading and solid angular grain coverage. The live
title uses the shared GPU particle pool only for muzzle flashes and wheel dust,
so new GPU emitters cannot reset the impact cloud or ballistic grains.
Starting stops new fire; in-flight shots finish and all visuals clear before the
blackout handoff. No gameplay spawn, damage or collision actor is created.
