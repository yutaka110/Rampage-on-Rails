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

Gameplay scout/basic/leader actors keep the existing 36–70 m forward engagement
band, with stable identity-derived preferred distances of 42–63 m, and remain targetable after firing. Their authored lateral
and vertical hover offsets feed actual actor positions, so aiming/collision follow
the motion. A nominal 0.14–0.18 Hz drift, 3.0–3.8 m lateral and 1.3–1.8 m vertical amplitude with restrained
banking convey lift; rigid hull scale stays constant during charge and recovery.
Readable warning/token admission precedes every repeat shot, with 2.4–2.6 s
cooldowns. Normal forward-hover drones remain targetable beyond their old
pass lifetime until defeated. Section/Wave completion and formation exit
requests never fade or remove a living normal drone. Camera/cart clearance
repositions it and cancels an unsafe shot, without retiring it; scene reset
still clears actors. Completed-Wave checkpoints restore surviving drones.
First attacks get a small admission priority boost so
surviving repeat attackers do not starve new arrivals. Dedicated spire
single-pass encounters and rear chaser behavior retain
their choreography. In cave gameplay, mode 10 uses a cool upper-side GGX key,
weaker warm lower-side bounce and hemisphere ambient. The stronger key and
steel-blue edge reflection separate the hull/shields from brown cave rock while
retaining face shading and mapped roughness. Broad ambient/edge terms use the
geometric normal to avoid turning texture scratches into a glowing outline.
An additional soft frontal GGX source and cool metal ambient reveal the sensor
housing and broad shield faces when the enemy occupies few screen pixels.
Title shading is unchanged.

`EnemyFormationSystem` adds stable identity-derived hover homes after authored
formation cohesion. Independent forward-hover drones use only 5 percent of the
forward cohesion correction and 15 percent of lateral/vertical correction.
Other formations retain full cohesion and their entrance/attack staging.
Six deterministic spacing passes consider nearby hovering drones across wave
boundaries using a screen-oriented ellipsoid. Forward correction is now only
±3 m for separation; the base movement controller holds each actor's own
preferred distance. Private lateral homes span ±5 m around authored positions,
with a 2.5 m outward bias for actors authored left/right of center,
with up to ±8 m of correction; private heights target 4.5–11.5 m above the rail.
Correction permits 6 m down/9 m up, with the existing floor clearance guard.
Rail corridor radius and model clearance additionally bound lateral/height
goals. Actors enter into their own region while still under entrance staging,
so they do not all materialize in a central cluster and slowly disperse.
The body approaches solved positions at at most 2 m/s (0.65 m/s during aiming
or admitted warning). Offsets are applied to the actual runtime actor pose and
removed once in BeginFrame, so targeting, collision and rendering agree and
long sessions cannot accumulate drift. Surviving homes never depend on live
formation indices; death/exit poses retain their last offset. Retry checkpoints
copy the smoothed state. Single-pass guards, turrets, rear chasers and the title
pursuer keep their existing movement paths.

Gameplay screen-presence measurement uses the shield's authored 1.6-unit half
height and local Y scale rather than treating this mesh as a unit-radius sphere.
Gameplay models use a stable 1.30 multiplier shared with this measurement, so
the body is larger without losing distance-dependent perspective. Additional
readability magnification is capped at 1.25 (1.625 combined), with the same idle/engaged threshold (65 percent
of the legacy engaged diameter) to retain perspective and avoid a charging-size
jump. The sphere-only opening scout enlargement is omitted for this mesh.
Collision radius is unchanged.
Forward-hover drones follow individual, multi-frequency flight paths in three
axes, with a checkpointed continuous flight clock. Aiming smoothly slows that
clock to 24 percent; recovery resumes it rather than repeating a side-hop.
Bank/yaw/pitch respond to motion velocity, with restrained limits. Other enemy
archetypes and single-pass choreography retain their existing movement.

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
