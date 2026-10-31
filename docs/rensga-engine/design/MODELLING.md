# Modelling for Rensga Engine

How to make 3D models in Blender for PSP, PS2 and PSX projects, and the limits each console sets.

Status (2026-10-31): the cooker imports **static glTF meshes** with vertex colour, normals and base colour. UVs, textures on meshes, bones and animation clips are not imported yet. They arrive with the 3D phases: PSP in F5 (PSP Engine 6, PSP API), PS2 in F8 (Engine 7–8), PSX in F9 (PSX Engine 7, PSX API). Build models to the limits below now, and they will need no rework later.

Limits marked **enforced** are checked by the cooker today and stop the build with an error that names the asset. The others are guidance or planned checks.

## Shared rules (every console)

1. **Units and scale.** 1 Blender unit = 1 metre. Put the origin at the character's feet. A 1.7–1.8 m character matches the template controllers.
2. **Low poly from the start.** Block the model out of simple shapes: limbs as 6–8 sided cylinders, mitten hands, simple head. Keep the silhouette strong and put detail in the texture or vertex colour.
3. **Vertex lighting only.** No normal, specular, roughness or metal maps, and no PBR. Bake shading and ambient occlusion into vertex colour or into the texture.
4. **One texture atlas per character**, painted at its final size with a limited palette. Textures are turned into 4-bit or 8-bit palettes, so 16 or 256 colours is what you get.
5. **Few materials.** Each material is one more draw call. Aim for 1–2 per character.
6. **Small polygons near the camera.** Large polygons that cross the near plane glitch on all three consoles (GU guard band on PSP, affine warping on PSX). This matters most for levels: subdivide floors and walls into a grid.
7. **Clean up before export.**
   - Apply rotation and scale (Ctrl+A), so no negative scale is left.
   - Recalculate normals to point outside, and delete loose geometry.
   - Check the triangle count in the viewport's Statistics overlay.
8. **Export as glTF 2.0 (.glb):**
   - Turn on Apply Modifiers, Normals and vertex colour. In Blender 4.2+, set Data > Mesh > Use Vertex Color: Active.
   - Leave +Y Up on.
   - Turn Draco compression **off**; the cooker's reader can't decode it.
   - Leave Tangents off, and Animation off until clips are supported.
9. **Import:** put the `.glb` under the project's `Assets/`, reference it from a MeshRenderer, cook, press Play.

What the importer accepts today: triangle lists only (the exporter triangulates for you), `POSITION` required, `NORMAL` and `COLOR_0` read, material base colour factor read.

## PSP

Profile: float, 480x272, 32 MB RAM budget (PSP-1000, so it runs on every model), 2 MB VRAM. Hardware transform and lighting on the GU.

| Item | Target | Notes |
| --- | --- | --- |
| Triangles | Hero 800–2,000; enemies 300–800 | The PSP ConsoleTarget budget is 20,000 triangles per frame for the whole scene (**enforced** per scene). |
| Texture size | 128x128 or 256x256 atlas, power of two, max 512 | Power of two and the 512 limit are **enforced** by the PSP cooker plugin (F4.05). About 1.2 MB of VRAM holds textures; the rest are read from RAM, more slowly. |
| Texture format | CLUT8 (256 colours) or CLUT4 (16) | Swizzled at cook time. 16-bit (5551, 4444) is allowed for gradients. |
| Skeleton | 15–30 bones, at most 8 bones per mesh part | The GU skins 8 bones per draw. The engine plans to split skinned meshes automatically (PSP API). |
| Bone weights | 1–2 per vertex, 4 at most | In Blender: Weights > Limit Total (2), then Normalize All. |
| Lights | 4 per object | The PSP profile picks the 4 most important lights per object. |
| Vertex data | Compact 16-bit positions | The cooker quantizes positions when precision allows. Keep a single mesh within a few metres of its origin for best precision. |

Typical rig: hips, spine, chest, neck, head; 3 bones per arm and per leg.

## PS2

Profile: float, 640x448, 32 MB RAM, 4 MB VRAM. ps2gl on VU1 for 3D.

| Item | Target | Notes |
| --- | --- | --- |
| Triangles | Hero 3,000–8,000; enemies 1,000–3,000 | The PS2 ConsoleTarget budget is 100,000 triangles per frame (**enforced** per scene). The cooker turns meshes into triangle strips, so keep clean edge flow and avoid isolated triangles. |
| Texture size | 256x256 atlas per character, power of two | RGBA textures over 256 px are refused by the GS upload today. Framebuffers take about 2.2 MB of the 4 MB VRAM, so textures stream in and out per frame; fewer, smaller textures stream faster. |
| Texture format | CLUT8 or CLUT4 | 32-bit RGBA only where a palette clearly fails. |
| Skeleton | 20–40 bones | Skinning runs on the CPU first (Engine 8). Every bone costs EE time, so keep it modest. |
| Bone weights | Up to 4 per vertex, 2 preferred | Same Blender steps as PSP. |
| Lights and fog | Vertex lighting, several lights, fog | The PS2 template shows the look: smoother shapes, fog, a blob shadow quad under the character. |

The PS2 is the most forgiving of the three for polygons. The usual limits are VRAM (texture streaming) and EE time for skinning.

## PSX

Profile: **fixed-point** (20.12), 320x240, 2 MB RAM, 1 MB VRAM. The GTE does transforms and lighting. There is no depth buffer and no skinning hardware.

| Item | Target | Notes |
| --- | --- | --- |
| Triangles | Hero 200–500; enemies 100–300 | Polygons are sorted per object by average depth, not depth-tested. Very long or very thin polygons sort badly. Use quads and triangles of similar size. |
| Characters | **Segmented**, not skinned | Build the character from separate rigid pieces (torso, head, upper and lower arms and legs) parented to bones, with no weights. This is exactly a PSX model: the Animator moves the pieces (SegmentedMeshRenderer). The editor will offer to convert skinned meshes (planned in PSX Editor). |
| Texture size | 64x64 to 128x128, max 256x256 (one texture page) | VRAM is 1024x512 16-bit units, shared with both framebuffers and every CLUT. An offline packer places the textures; check the VRAM map window. |
| Texture format | CLUT4 first, CLUT8 when needed | 15-bit only for small special cases. Share palettes between textures where you can (CLUT sharing groups). |
| Vertex colour | Preferred over textures for small or distant objects | The PSX template uses vertex colours instead of textures (a sphere is about 40 triangles). |
| Lights and fog | 3 lights per object, depth-cue fog | Bake the rest into vertex colour. |
| Polygon size | Subdivide anything large | Texture mapping is affine (no perspective correction), so big textured polygons warp as the camera moves. The engine subdivides near the camera, but a well-subdivided mesh looks better and costs less. |
| Coordinates | Keep models small around their origin | Vertices become 16-bit fixed-point. A mesh spread over a large range loses precision or overflows; the validator will flag fields too large for fixed-point (planned). |
| Memory | Count everything | 2 MB holds code, the level, characters and audio together. Levels are streamed in areas, so a character's mesh and texture are a big share of what stays resident. |

Expect the wobble: vertices snap to whole screen pixels. The editor's PSX preview mode shows it, so check your model there, not only in Blender.

## Quick comparison

| | PSX | PSP | PS2 |
| --- | --- | --- | --- |
| Hero triangles | 200–500 | 800–2,000 | 3,000–8,000 |
| Character texture | 64–128 px, CLUT4 | 128–256 px, CLUT8 | 256 px, CLUT8 |
| Animation | Rigid segments | Skinned, 8 bones per part | Skinned (CPU), 20–40 bones |
| Lights per object | 3 | 4 | several |
| Main risk | Memory, warping, sort order | VRAM, near-plane clipping | VRAM streaming, skinning cost |

## Licensing

Only use models, textures and sounds you made, commissioned, or that carry a license compatible with the project (CC0, or CC-BY with credit in `THIRD_PARTY.md`). Never use assets ripped from commercial games, even in a non-commercial demo (see RULES.md §5).
