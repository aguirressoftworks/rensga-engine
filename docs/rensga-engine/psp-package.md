# PSP package (EBOOT.PBP)

`rensga build --target psp <project>` (or File > Build and Run in the editor) writes `<project>/Library/Build/psp-release/`:

| File | What it is |
| --- | --- |
| `EBOOT.PBP` | PARAM.SFO, ICON0, PIC1, optional SND0.AT3, and the game as a PRX |
| `game.rpak` | the cooked pack, read from the folder of the EBOOT |
| `music*.wav` | every streamed track (cooked ones first, then `<project>/Streaming/`) |
| `LICENSES/` | the engine's MIT license, stb, and (module installed) PSPSDK, SDL2 and newlib licenses, plus `README-LICENSES.txt` |
| `package/` | the placeholder art made for this build, when the project has none |

Copy the folder contents to `ms0:/PSP/GAME/<any folder>/`. The game finds `game.rpak` next to the EBOOT.

## Package settings

`ProjectSettings/Package/Package.json` (all keys optional):

```json
{ "format": 1, "title": "Pequi Quest", "discId": "PEQU00042", "version": "1.50" }
```

- `title`: PARAM.SFO `TITLE`, at most 127 bytes. Default: the project name.
- `discId`: PARAM.SFO `DISC_ID`, four capital letters and five digits. Default: `RNSG` and five digits made from the project name, so a name always gives the same ID. An invalid value is replaced and the build says so.
- `version`: `<major>.<minor>`; `DISC_VERSION` is `1.50`, `APP_VER` is `01.50`. Default: `version` of `Project.json`, else `1.00`.

## Art

Put your own files in `ProjectSettings/Package/` (or `Assets/Package/`): `ICON0.png` (144x80), `PIC1.png` (480x272), `SND0.AT3`.
A file of another size is used as it is and the build warns. Without `ICON0.png` / `PIC1.png`, `tools/psp/make_package_art.py` draws
placeholders from the project name and the brand tokens of `docs/rensga-engine/brand/palette.json` (needs `python3`; without it the
build uses the bundled ICON0 and no PIC1). There is no AT3 encoder in the build support module, so SND0 is only ever your own file.

## Checking a package

```sh
python3 tools/psp/pbp_info.py <project>/Library/Build/psp-release/EBOOT.PBP [--json] [--extract <dir>]
```

prints the sections and the PARAM.SFO entries and can write ICON0, PIC1 and SND0 out to look at them. `tools/psp/package_check.sh` (ctest `psp_package_eboot`)
builds a project with its own `Package.json` and music files and checks all of the above.

## Hardware

`tools/psp/psplink_deploy.sh` and `docs/rensga-engine/psp-hardware-checklist.md` are for a real PSP. Neither has been run.
