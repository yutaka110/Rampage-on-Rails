# Ground054 title ground

Source: user-supplied `Ground054_1K-JPG.zip`, imported on 2026-10-06.
Archive SHA-256: `b325ec2f167dce95a0fabc61a2104641288cf20915419578c3efdb46200c563e`.

Asset: [Ground054 from ambientCG](https://ambientcg.com/view?id=Ground054).
License: [Creative Commons CC0 1.0 Universal](https://docs.ambientcg.com/license/).
The supplied maps and preview render are original archive bytes.

Runtime material: `Resources/terrain/materials/title_ground.terrainmaterial`.
The surface covers approximately 3.5 m x 3.5 m per tile. Color is sampled as
sRGB; normal, AO, roughness and height are linear data. NormalGL matches the
existing terrain decoder. AO/roughness are packed into ORM with metallic=0 by
the existing texture-array loader. Height is retained in the PBR height array;
this material does not displace the ground/collision mesh.

Texture slice 3 and material mode 9 are reserved for the title ground. The
original three gameplay PBR layers remain intact, including the floor layer
used on upward-facing parts of title rocks. Extra procedural sand detail is
reduced so the supplied normal map defines the visible ground relief.
