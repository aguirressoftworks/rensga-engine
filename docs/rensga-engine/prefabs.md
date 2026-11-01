# Prefab format (Editor 6, F6.01)

Contract written by E0, implemented by A1 (editor behaviour) and the cooker. Data structs and JSON read/write: `editor/core/include/rensga/editor/prefab_format.h`. No runtime section: **prefabs are merged at cook time**, the pack contains ordinary objects.

Rules this follows: text for people, binary for machines (RULES 1.6); references by GUID (1.7); one reflection description (1.4).

## Files

- `Assets/**/<name>.prefab`, with a `.meta` carrying its GUID (kind `Prefab`).
- Object ids inside a prefab are `fileId`s: integers allocated from `nextFileId`, **never reused**, so overrides survive renames and reordering.
- An id that reaches into a nested prefab is the path of ids from the outer prefab: `"3/7"` = object 7 of the prefab nested at object 3. On disk plain ids are JSON numbers, paths are strings. In C++ both are `FileId` (a string).

### Base prefab

```json
{ "format": 1, "guid": "<asset guid>", "rootFileId": 1, "nextFileId": 6,
  "objects": [
    { "fileId": 1, "name": "Enemy", "parent": null, "active": true,
      "transform": {"position": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1]},
      "components": [ {"fileId": 2, "type": "MeshRenderer", "fields": {"mesh": "<guid>"}},
                      {"fileId": 3, "type": "Enemy", "fields": {"speed": 2}} ] },
    { "fileId": 4, "name": "Weapon", "parent": 1, "transform": {...},
      "prefab": {"asset": "<sword prefab guid>", "fileId": 1,
                 "overrides": [{"fileId": 1, "path": "MeshRenderer.color", "value": [255,0,0,255]}]},
      "components": [] } ] }
```

- Exactly one root (`parent: null`). `tag` and `layer` are written only when non-default; `active` only when false.
- `components[].fields` use the same JSON as a scene (reflection names and values).
- An object with `prefab` is the root of a **nested** prefab. It carries only the modifications (below); the nested prefab's objects come from its asset and are addressed as `"4/<id>"` by outer overrides.

### Variant

```json
{ "format": 1, "guid": "<asset guid>", "nextFileId": 1,
  "variant": { "base": "<base prefab guid>",
               "overrides": [{"fileId": 3, "path": "Enemy.speed", "value": 4}],
               "addedComponents": [...], "removedComponents": [...], "removedObjects": [...], "addedObjects": [...] } }
```

A variant has no objects of its own. `nextFileId` is used for the ids of objects/components it adds.

## Modifications

One shared shape (`PrefabModifications`) for a scene instance, a nested entry and a variant:

| Key | Entry |
| --- | --- |
| `overrides` | `{"fileId", "path", "occurrence"?, "value"}` |
| `addedComponents` | `{"fileId": <object>, "componentId": <id>, "type", "fields"}` |
| `removedComponents` | `{"fileId": <object>, "type", "occurrence"?}` |
| `removedObjects` | `[<fileId>, ...]` (the object and its subtree) |
| `addedObjects` | object entries in the owner's format (prefab object entries in a prefab/variant, scene object entries in a scene), `parent` = a base fileId (prefab) or a scene GUID (scene) |

`path` is the scene-level form of reflect.h's `FieldPath` with the component type in front: `Type.field`, `Type.field.x` (Vector3/Quaternion part), `Type.field.2` and `Type.field.2.x` (Array element). `occurrence` picks one of several components of the same type on an object (default 0). Object-level properties: `@name`, `@active`, `@tag`, `@layer`, `@position`, `@rotation`, `@scale`. Values use the field's scene JSON. Fields are checked against reflection by the validator (A1); `validate(PrefabAsset)` checks only the structure.

The root object's `@position`, `@rotation` and `@scale` are **never** overrides: each instance has its own placement.

## Instance in a scene

The root object of an instance is an ordinary scene object (`guid`, `name`, `parent`, `active`, `transform`) plus:

```json
{ "guid": "<scene guid>", "name": "Enemy (3)", "parent": null, "active": true, "transform": {...},
  "components": [],
  "prefab": {"asset": "<prefab guid>", "fileId": 1},
  "overrides": [{"fileId": 3, "path": "Enemy.speed", "value": 5}],
  "addedComponents": [], "removedComponents": [], "removedObjects": [], "addedObjects": [] }
```

- `prefab.fileId` is the prefab's `rootFileId` when the instance was made (lets the editor notice a re-rooted prefab).
- The other members are **not stored** in the scene. Their scene GUID is derived: `instanceObjectGuid(rootSceneGuid, memberFileId)` (deterministic 128-bit hash, v5-style text; the root's own member id maps to the root's GUID). Selection, Undo and references to a member from other scene objects use that GUID. Empty modification lists are omitted. Scenes without `prefab` are unchanged (the old `"prefab": null` key is still accepted and ignored).
- `readInstanceKeys` / `writeInstanceKeys` convert a scene-object JSON to and from `PrefabInstance`.

## Precedence

Expanding an instance, innermost first:

1. the base prefab's own values;
2. **nested** modifications recorded in the prefab that nests another one;
3. **variant** modifications (a variant expands its base, then applies its own);
4. the **instance** modifications in the scene.

So `instance > variant > nested > base`. Within one list, a later entry for the same `(fileId, path, occurrence)` wins; the editor writes at most one. A removed object drops its added components and overrides. An override whose target no longer exists is kept and reported (Console warning, shown in the Inspector) until the user reverts it.

## Operations (A1)

- **Apply** an override writes the value into the prefab that owns the field (the variant for a variant instance, else the nested/base prefab the user picks) and deletes the override. **Revert** deletes the override.
- Apply/revert exist per field, per object and for the whole instance.
- **Unpack** replaces the instance with its expanded objects (new GUIDs), dropping the link.
- A prefab cannot contain itself, directly or through nesting or variant bases; nesting and variant chains are at most `kPrefabMaxNesting` (8) deep. The validator reports a cycle with the asset names.
- Cooking expands every instance with the rules above into plain scene objects; the cooked scene has no trace of prefabs.

## Open points for A1

- The Hierarchy shows members under their root; creating a prefab from a selection assigns fileIds in hierarchy order.
- `addedObjects` entries of a scene instance carry their own scene GUID and may hold nested instances (same keys).

## Implementation notes (A1, F6.01)

- **One expander.** `tools/cooker/src/prefab_expand.cpp` (`rensga/cooker/prefab_expand.h`) expands an instance root into plain scene objects. The editor builds `SceneDocument::scene()` with it and the cooker calls it before it reads a scene (`prefab_cook.cpp`), so the Hierarchy shows what the pack holds. A scene that uses prefabs cooks to the same bytes as the same scene expanded by hand (`rensga_prefab_tests`, `prefab_cook_matches_manual_expansion`). Projects without `.prefab` files cook exactly as before.
- **Member ids.** The root of an instance is object `rootFileId`; the root of a nested prefab is addressed by the id of its entry (`"3"`) and also by `"3/<its root id>"`. Ids of objects added by a variant are `v<first 8 hex of the variant guid>_<n>`. Component fields of a prefab that name another object of the same prefab are written `"@<fileId>"` (`"@4/7"` into a nested prefab) and become the member's GUID on expansion.
- **What is scene-owned.** An instance root keeps its own name, parent, active flag and transform in the scene; `@tag` and `@layer` on the root are overrides. Edits to anything else on a member become overrides with a whole-field value; setting a field back to the prefab's value removes the override. A child created under a member is stored in the root's `addedObjects`; a child created under the root is an ordinary scene object parented to it.
- **Apply** writes into the prefab the instance points at: the base file, the variant's modification lists, or (for ids that reach into a nested prefab) the nested entry's modifications. Undo covers the prefab file and the scene in one step. **Unpack** drops the link and keeps the GUIDs of the members, so references to them survive.
- **Instantiate from scripts.** A `Guid` field (or `Guid` array) that names a prefab asset is cooked as a reference to a template: the prefab expanded into inactive plain objects appended to that scene (`prefabTemplateGuid(scene guid, prefab guid)`), so `Instantiate(world->findByGuid(field))` clones it and `SetActive(true)` shows it. `appendPrefabTemplate` in `prefab_cook.h` adds a template for any other caller, for example a `Resources` entry.
- **Prefab edit mode** opens the prefab as a scene (`prefabToStage`): objects keep their ids in extra `fileId` keys, nested prefabs are instances, a variant is one instance of its base. Saving goes through `stageToPrefab`; objects created in the stage get ids that stay the same across saves, undo and redo.
