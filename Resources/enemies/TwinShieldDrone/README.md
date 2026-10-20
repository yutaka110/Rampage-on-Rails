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
their choreography. In cave gameplay, mode 10 retains neutral rock-bounce fill
and opening light, with a stronger oblique upper-side GGX key to distinguish
front, side and bevel faces at gameplay distance. Small authored contact
occlusion volumes shade shield inner faces/mounts, hull joints, the sensor
recess and downward faces. They use each part's world-to-model transform packed
in the existing material slots, so shading follows banking and detached parts.
Each shield's mounting shadow weakens with charge spread and fades after its
own detachment time; this adds no terrain raycasts, textures or draw calls.
Contact visibility is bounded to retain the readable ambient floor. Core
emission, hit flashes, outdoor and title shading retain their existing paths.

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

Ordinary primary-shot kills use `RailPrimaryKillPresentation`: a white core
ruptures into six staggered, irregular orange-red lobes over the first 0.17 s.
The struck shield releases at 55–67 ms while the blast is still expanding.
A single restrained glint replaces the former large crossed slash. Directed
fragments, delayed alpha-blended soot and the existing falling wreck carry the
aftermath. Released material retains its world emission position rather than
following the falling sensor. Smoke finishes within 0.72 s; all state shares
the gameplay clock and clears on retry/title return. Six fire lobes and five
smoke puffs per kill, with at most sixteen live kills, bound the extra work.
`TitleTracer.PS.hlsl` UV30 supplies the original turbulent hot-to-cool burst;
the sorted, depth-tested `TitleDust` pass supplies smoke. The generated 240 ms
core blast sound includes the metal break at 60 ms. Lock-on kill presentation,
damage, collision and the later ground-impact explosion remain independent.

Ordinary hostile shots now share the drone's orange core palette: a round
plasma mass with broad flowing channels, a ragged white-hot seam, broken bright
edge arcs, a red density edge and a three-radius orange tail. The former smooth
Lambert sphere and circular white highlight are omitted to avoid a plastic-ball
appearance at gameplay size. Direct/predictive, assault and turret visual bindings use the orb
style; their existing projectile IDs and physical trajectories are unchanged.
`EnemyProjectileVfxRenderer` exposes a native sphere backend only when the host
provides both world shader passes. `AppRunLoop` draws the solid silhouette in
the sorted alpha/depth-tested `TitleDust` pass (UV2000) and the compact glow/
tail in `TitleTracer` (UV32), with flowing edge light at UV34. The 64-step internal flow uses gameplay time;
the body radius does not pulse. The existing pixel readability policy and
shoot-down authority still determine visibility and interception separately.
Native orbs skip duplicate particle instances and debug circles/diamonds/
brackets. Hosts without this backend retain the visible primitive fallback.
The title and READY salvo use the same sphere/tail shaders, and world effects
remain visible when the gameplay HUD is hidden. Homing missiles and boss arcs
retain their distinct styles. Retry/title reset clears the native proxies.

Lethal lock-laser arrivals use an original SF impact sequence: a compact white/
cyan contact, an oblique eroding shock sheet (TitleTracer UV36), then an orange
core rupture. The frozen accepted wreck pose anchors the light to the actual
sensor. The old long cut and early radial sparks are suppressed for these
contacts; connection arcs and the existing shield/fall choreography continue.
Core brightness fades sooner to retain the impact sheet and metal silhouette.
Four core chips lead twelve armor chips emitted at the first shield's detach
time from its rendered joint. Released fragments use fixed world origins,
inherited velocity, drag and gravity; tumbling faceted metal (TitleDust UV4000)
outlives its short cooling luminous trail. Lethal contacts last at most 0.84 s;
nonlethal hits retain their 0.25 s impact and emit no armor debris. Each contact
has at most sixteen chips, with a shared 160-chip rendering budget and a
simultaneous-contact brightness cap. One evaluation supplies both solid and
glowing passes. Pause and reset share the authoritative volley clock, and
damage remains committed once at the rendered laser arrival. No third-party
asset files or additional per-particle collision queries are required.

Lock arrivals also carry a tapered white/cyan penetrating spear (UV38) and two
unequal short energy jets. Ordinary hits retain a short 90 ms accent; kills
hold the strong spear through 40 ms and decay by 145 ms. The killing spear and
shock sheet extend beyond the shields, with a bounded reference-resolution
minimum footprint so distant contacts do not collapse into a tiny core dot.
The torn sheet is present on the first arrival frame and hands over to the
larger orange rupture and slightly longer cooling fragment trails. Brightness
normalization counts the whole 160 ms peak window across simultaneous hits.
Traveling lock lasers have a thicker core and concentrate light near arrival;
their old head glow stops once resolved so the contact owns the visible point.
