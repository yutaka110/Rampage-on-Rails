# Third-Party Notices

The following dependencies are part of the GE3 build or distribution. Their
approved content hashes are recorded in `Build/GE3.Dependencies.lock.json`.

| Dependency | Use | License | Included notice |
| --- | --- | --- | --- |
| Assimp | Source asset import; vendored headers and x64 static libraries | BSD-3-Clause, including bundled Poly2Tri notice | `externals/assimp/LICENSE.txt` |
| DirectXTex | Texture processing; source-built static library | MIT | `externals/DirectXTex/LICENSE.txt` |
| Dear ImGui | Editor UI | MIT | `externals/imgui/LICENSE.txt` |
| DXC/DXIL | Shader compilation runtime from the pinned Windows SDK | University of Illinois/NCSA Open Source License and LLVM-related notices distributed with the Windows SDK | Windows SDK installation |

Release packaging must reproduce every applicable notice alongside the
product documentation. Adding or upgrading a dependency requires an explicit
lock-file update and license review.

The title ground uses [Ground054 from ambientCG](https://ambientcg.com/view?id=Ground054),
provided under [CC0 1.0 Universal](https://docs.ambientcg.com/license/).
Import provenance is recorded in `Resources/terrain/Ground054_1K-JPG/README.md`.
