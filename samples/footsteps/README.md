# footsteps

F6.10 (API 8) exercise: footstep sounds driven by animation events on a running character, and the audio mixer groups. PSP profile (480x272), one scene, one script (`Footsteps`).

- The Run clip (`Assets/Animation/Run.clip.json`, 12 fps, 4 ticks, looping) has two animation events, `Footstep` with `float` 1.0 at tick 0 and 0.7 at tick 2. Run is 20 fixed steps long at 60 Hz, so one footstep lands every 10 steps.
- The `Footsteps` script has `METHOD(Footstep)` taking a `Scalar`. The animation system calls it through the method registry; it plays `step.wav` on the runner's `AudioSource` (`outputGroup` SFX) and logs `rensga: footsteps: n=<k> step=<fixed step> vol=<v>`.
- `Ambience` is an `AudioSource` with `playOnAwake`, `loop` and `outputGroup` Music (the generated `hum.wav`). `Footsteps.Start` sets `AudioMixerGroups::SetVolume(Music, 0.4)` and `SetVolumeDb(SFX, -3)`.
- The runner idles for 30 steps, runs until step 330 and quits at step 380 with `rensga: footsteps: done steps=380 footsteps=<k>`.

```sh
tools/ci/animation.sh footsteps     # desktop (SDL disk audio must not be silent) + PPSSPPHeadless: the same events at the same steps, every gap 10
python3 samples/footsteps/tools/make_sample.py   # regenerates the sample and its two generated sounds (the script is hand-written)
```
