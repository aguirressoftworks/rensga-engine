# The 3D toolset (Editor 8, F6.02)

What a level designer gets in the editor for 3D scenes, and what each tool does when the project is cooked.

## Scene view = the console preview

The Scene view draws the open scene with the same `GlRenderer` that Play in editor and the desktop player use:

- the scene is cooked from memory into the edit-mode World (`EditModeWorld`, the path of Play in editor) and drawn with its
  textures, `.mat.json` materials, `Light` components, fog and skinned meshes (first pose);
- each object is lit by at most `PreviewRules.maxLightsPerObject` lights (the target says how many; the Game view's Preview menu has
  the same switch), textures are the ones the target stores (palette banding, 16 bit colour), and the Preview menu's 5-6-5 and
  dither options apply to the Scene view too;
- the grid, selection outline, markers and gizmos are drawn over that image and still hide behind geometry; picking is the same
  ID-buffer picking as before. Without a World (the scene does not cook, scripts are still building) the old flat-shaded view stays;
- glTF models appear in the Scene view and can be picked and outlined (`MeshLibrary` indexes `.gltf` next to `.prim.json`);
- the Game view in edit mode shows the scene through its Camera with the same renderer (it was empty-looking before).

The editor's own camera is left-handed (Unity style) while the engine's Camera looks down its local -z with an OpenGL matrix, so the
Scene view is the mirror image of the game from the same position. Everything that is drawn in the Scene view uses one convention, so the
overlays line up. The Camera marker and frustum point down -z, as the game camera does. The Game view uses the engine's convention and
is the exact picture.

The toolbar's **Lit** button (or `scenelit:on|off`) switches between this and the old flat-shaded drawing.

Verbs for `--ui-event`: `scenelit:on|off`, `litdump` (prints `lit: frames= draws= objects= lights= maxLights= fog= ...`),
`scenecam:<ex>,<ey>,<ez>|<tx>,<ty>,<tz>[|<fov>]` (Scene camera at the eye, looking at the target).

## Gizmos and inspectors

| Component | Scene view | Inspector |
| --- | --- | --- |
| `Light` | directional: rays along the forward (+z) axis; point: range sphere (and half range) in the light's colour | what the light does, lights in the scene against the target's per-object limit, a warning when there are more |
| `Camera` | frustum along -z when selected | a live thumbnail of what this camera sees, at the target's aspect |
| `AudioSource` | speaker marker; range spheres when the project's AudioSource has `minDistance` / `maxDistance` | missing-clip warning, mixer group (the mixer has no 3D attenuation today) |
| `BoxCollider`, `SphereCollider`, `CapsuleCollider` | wire shapes (solid green, trigger blue, selected yellow) | **Fit to mesh** from the object's MeshRenderer, one undo step |
| `Rigidbody` | gravity arrow and axes when selected | (reflected fields) |
| `MeshRenderer` | | **Edit Material**, and what the cook does with this renderer (lit at run time / baked) |

`GameObject > Light > Directional Light | Point Light` creates the F5 `Light` (a project whose ConsoleTarget does not allow `Light` keeps
the old `DirectionalLight`). `GameObject > 3D Object` has cube, sphere, capsule (with a `CapsuleCollider` when the project allows it), plane,
ramp, camera and audio source. The `create:<shape>` verb takes `light`, `pointlight`, `camera`, `audio` as well.

## Material Editor

Opens from the MeshRenderer inspector (**Edit Material**) or with `matedit:<Assets/..mat.json | guid>`. Rows: texture, color, blend mode,
alpha test, unlit, fog. The preview sphere is lit like the player's renderer. What the target cannot draw is greyed out with its reason:
no `lighting` feature forces unlit, no `fog` feature shows fog as not drawn, a texture over `maxTextureSize` gets a downscale warning.
The blend mode is the colour's alpha (the renderers blend when it is below 255): Opaque pins it to 255. Edits are written at once
(atomic), one undo step each (`matundo`, `matredo`), and the lit Scene view recooks. Verbs: `matedit`, `matset:<key>=<value>`
(`texture`, `color=r,g,b`, `blend=opaque|alpha`, `alpha`, `unlit`, `alphatest`), `matundo`, `matredo`, `matclose`, `matdump`.
The file format is the one in `pack-format.md` ("Material asset"); `MaterialDocument` (editor/core) keeps unknown keys.

## Baked vertex lighting

`MeshRenderer.bakeLighting` (appended field, default off) makes the cooker bake into the mesh's vertex colours, per vertex:

```
color = clamp(ambient * ao + sum over lights of  light colour * intensity * shadow * attenuation * max(0, n . l))
```

- lights: every active `Light` (directional along +z, point with the runtime falloff `(1 - d / range)^2`); a scene with only the old
  `DirectionalLight` uses that; ambient is the scene's `RenderSettings.ambient` (0.35 grey without one);
- shadows and ambient occlusion are ray casts against every active static MeshRenderer of the scene (16 cosine-weighted samples from a
  fixed pattern, 2 m reach, strength 0.65); no random numbers, so a bake is a pure function of the scene and the same on every host;
- a baked mesh without a material takes the cooker's own baked-light path with a neutral light; a baked mesh with a material gets
  `kMeshLightBaked` in its MSHL record (`pack_format.h`), and `renderWorld` draws it unlit whatever its material says (its light is in the
  colours; the material asset is untouched and other renderers keep using it lit). The runtime draws it as any other vertex-coloured, textured
  mesh on PSP, PS2, GL and Android; the Inspector's light-limit warning ignores baked renderers;
- scenes where no renderer sets the field cook to the same bytes as before (checked against psp3d_room for psp and linux).

The **Lighting** window (`lighting` verb, or the MeshRenderer inspector) flags every static mesh in one undo step, cooks the scene on a worker
thread and shows a progress line (`Baking... n / m vertices`), then `Baked N meshes, V vertices, R rays (C from cache) in T s`. Results are
cached per mesh (scene lights, occluder geometry and the mesh are the key), so the Scene view's recook after the bake is instant.
Verbs: `bakeall`, `bake`, `bakeclear`, `bakewait`, `bakedump`.

## Sample

`samples/level_walk` (generated by `tools/samples/gen_level.py`): three halls, three glTF files, three materials, seven lamps and a moon,
a first-person `LevelWalker` script. Scenes `Level` (baked), `LevelRaw` (runtime lights), and the self-walking `LevelBot`, `LevelRawBot`.
`tools/ci/psp_level.sh` runs the two bots in PPSSPPHeadless and writes before/after PNGs.
