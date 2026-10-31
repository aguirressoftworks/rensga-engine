# platformer2d_psp

The `samples/platformer2d` game as a PSP project (480x272, 5650 framebuffer, PSP texture formats, 22050 Hz audio).

**This folder is generated. Do not edit it by hand.**

```sh
python3 samples/platformer2d/tools/make_sample.py --profile psp --out samples/platformer2d_psp
```

The generator writes the same assets, scenes, scripts and music as `samples/platformer2d`. Only `ProjectSettings/Project.json` (the
target) and `ProjectSettings/ConsoleTarget.json` (profile `psp`, 480x272, `rgba5551`, 16-bit framebuffer, PSP budgets) differ, because
every scene uses UI anchors. The test `psp_project_is_generated_and_differs_from_ps2_only_in_project_settings` (ctest `rensga_platformer_tests`) regenerates both projects
into a temp folder and compares them with the committed folders byte for byte, so a change to the generator or the PS2 sample that
is not regenerated here fails the build. To update this folder after changing the sample, run the command above (and the PS2 one
without `--profile`), then commit both.

`README.md` is the only hand-written file here (the generator never touches it). `Library/` is build output and is ignored.

## Run it

```sh
tools/ci/psp_2d.sh              # cook, build the PSP ELF, boot the TitleBot in PPSSPPHeadless, dump a frame per room and the results
tools/ci/psp_2d.sh stress       # + 1000 sprites (PSP vs PS2 table), input test scene, 16-voice audio stress at 222 and 333 MHz
tools/ci/determinism.sh 2d      # Linux rensga_headless vs the PSP sim ELF playing TitleBot: equal state hashes
```

The PSP build is `rensga build --target psp samples/platformer2d_psp` (EBOOT.PBP) or `rensga play --target psp samples/platformer2d_psp`.
The music files in `Streaming/` go next to `game.rpak`.
