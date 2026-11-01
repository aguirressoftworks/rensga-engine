# prefab_enemies

Exercise project for prefabs (F6.01, Editor 6). A small arena for the PSP profile:

- `Assets/Prefabs/Enemy.prefab`: a green body with an eye (child object).
- `Assets/Prefabs/Boss.prefab`: a **variant** of Enemy (purple body, bigger eye).
- `Assets/Prefabs/Squad.prefab`: a prefab that **nests** two Enemy prefabs (the second has a blue body set in the nested entry).
- `Assets/Scenes/Arena.scene`: five Enemy instances (the third has its colour overridden to red), a Boss and a Squad. The scene file
  stores only the instance roots and their overrides; the members are expanded by the editor and by the cooker.

Try it:

```sh
build/host/editor/app/rensga-editor --project samples/prefab_enemies
```

Select an Enemy, change its Mesh Renderer colour: the field turns blue with a bar and the scene file gets one `overrides` entry.
Open the prefab (the `>` button in the Hierarchy, or double-click it in the Project window), change the body colour and leave prefab
mode: the other instances follow, the overridden one keeps its red.

`tests/editor/exercise_prefab_enemies.sh` (ctest `exercise_prefab_enemies`) does this with `--ui-event` verbs and takes screenshots.
`make_sample.py` regenerates the project. Cook it with `rensga-cooker cook --project samples/prefab_enemies --target psp`.
