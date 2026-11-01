# Animation and audio (F6.03 Editor 9, F6.10 API 8)

This page is the format and behaviour reference for property clips, animator controllers, animation events, the Animator component and the audio mixer groups. The Animation window and the Animator graph edit the files described here. The pack records are in [pack-format.md](pack-format.md) ("F6 content sections").

## Property clips (`*.clip.json`)

A clip keyframes any reflected field of any component of an object and its children, plus two pseudo components. Time is an integer tick count at the clip's own `fps`.

```json
{ "format": 1, "kind": "PropertyClip", "name": "Run", "fps": 12, "length": 4, "loop": true,
  "curves": [
    { "target": "Arm/Hand",                       // optional: child path below the animated object
      "property": "SpriteRenderer.color.r",       // Component.field[.part]
      "interp": "linear",                         // linear | step | smooth
      "keys": [ { "t": 0, "v": 255 }, { "t": 4, "v": 0 } ] } ],
  "events": [ { "t": 2, "function": "Footstep", "float": 0.7 } ] }
```

| Property | Channels | Notes |
| --- | --- | --- |
| `Transform.localPosition` / `localScale` | `.x .y .z` | local values |
| `Transform.localRotation` | `.x .y .z .w` | quaternion, renormalised after each write |
| `Transform.localEulerAngles` | `.x .y .z` | degrees, ZXY order; the cooker bakes them to `localRotation` (one key per tick) because the runtime has no trig |
| `GameObject.active` | | Bool, step |
| any reflected field | Scalar: none; `Vector2/3`: `.x .y .z`; `Quaternion`: `.x .y .z .w`; `Color32`: `.r .g .b .a` (0..255) | `I32`, `Bool`, `Enum` and `Guid` fields are always step |

- **Interpolation:** `linear`, `step` (constant), `smooth` (cubic Hermite from each key's `in` and `out` tangent, value change per tick). Bezier handles in the curve editor are these tangents.
- **Guid fields** (sprites, textures, any asset reference) take a guid string as `v`; the cooker stores a table of guids and the value switches at each key.
- **Enum keys** take a number or the label (`"v": "Vertical"`).
- **Limits:** 64 curves, 512 keys per curve, 64 events per clip. A clip cannot be shorter than its last key or event.
- **Unknown properties** are an error that names the clip and the property. Script components are known to the cooker only when it is built with the project's scripts (hook row F6.05).

### Animation events

An event calls `AnimationEvents::dispatch` on the animated object when the clip time reaches its tick. The default handler sends `Behaviour::OnAnimationEvent(const AnimationEvent&)` to every behaviour on the object and then calls the `METHOD(Name)` method called `function` on each behaviour that has one: `void()`, `void(Scalar)` (gets `float`), `void(i32)` or `void(bool)` (get `int`). `AnimationEvents::setHandler` replaces the default.

An event at tick `t` fires on the exact fixed step where the clip time reaches `t`; a looping clip fires tick 0 again when it wraps; events of a state that is being faded out are skipped.

## Animator controllers (`*.controller.json`)

```json
{ "format": 1, "kind": "AnimatorController", "name": "Player",
  "parameters": [ { "name": "speed", "type": "float", "default": 0 }, { "name": "jump", "type": "trigger" } ],
  "defaultState": "Idle",
  "states": [ { "name": "Idle", "motion": "<guid of a .clip.json or a glTF>", "loop": true, "speed": 1,
                "speedParameter": "", "pos": [60, 80] } ],
  "transitions": [ { "from": "Idle", "to": "Run", "hasExitTime": false, "exitTime": 1.0, "duration": 0.08,
                     "conditions": [ { "param": "speed", "mode": "greater", "value": 0.1 } ] } ] }
```

- **Parameters** are `float`, `int`, `bool` or `trigger` (at most 16). Conditions: `if` / `ifNot` for bool and trigger, `greater` / `less` / `equals` / `notEqual` for float and int.
- **Motion:** a property clip, or an F5 skeletal clip (the guid of a glTF is its first clip). An empty motion is an empty state.
- **Transitions** from `"*"` are any-state transitions. A transition starts when every condition holds and, with `hasExitTime`, the source state's normalised time (loops included) has reached `exitTime`. A trigger used by the transition that fires is reset.
- **`duration`** is in seconds; the cooker stores it in fixed steps (`Time.json fixedTimestep`, default 1/60). A cross-fade blends the two states' values: continuous values blend, `Bool` / `I32` / `Enum` / `Guid` values switch at 50 %, skeletal poses blend with nlerp.
- **Priority:** transitions of the current state in file order, then the any-state transitions.
- **Cook errors** name the controller and the state, transition or parameter.

## Animator component (API)

`Animator { controller, speed, applyRootMotion }` with Unity's names: `SetFloat/SetInt/SetBool/SetTrigger/ResetTrigger`, `GetFloat/GetInt/GetBool` (by name or `Animator::StringToHash`), `Play(state)`, `CrossFade(state, seconds)` and `GetCurrentStateInfo()` (`IsName`, `normalizedTime`, `length`). Root motion is not simulated.

- The Animator runs once per fixed step after `FixedUpdate`, so a script that sets parameters in `FixedUpdate` gives the same state trace on every platform. A state entered shows its pose at time 0 before it advances.
- **Capacity:** 32 Animators with a controller per world, 16 parameters per controller, a 512-matrix palette pool for skeletal states. No heap use per frame.
- **Determinism:** clip time is an exact integer (`fps * speed` units per fixed step, so a 12 fps clip on a 60 Hz step reaches tick 2 on step 10), the arithmetic is `+ - * /` only, and `tools/ci/determinism.sh anim` compares a state hash (states, times, parameters and every property value written) of every fixed step on Linux and in the PSP simulator.
- F5's legacy `Animation` component keeps working unchanged; the 3dlit hashes did not move.

## Editor

**Animation window** (Window > Animation, or double-click a `.clip.json`): dopesheet (a row per property, keys as diamonds, an events lane), curve editor (the `Curves` toggle), a property menu built from the reflected fields of the animated object and its children (Add Property), key and event inspector, undo (Ctrl+Z), and save. The playhead scrubs by dragging the ruler.

- **Preview:** the clip is sampled with the runtime sampler on a copy of the scene shown in the Scene view; the scene document is frozen (the same mechanism as Play in editor) and never changes. Closing the window or pressing Play ends the preview.
- **Record:** with Record on, a change made in the Inspector or with the Scene view gizmos at the playhead becomes a key (a new curve for each changed channel). With it off, the change only affects the preview.

**Animator graph** (Window > Animator, or double-click a `.controller.json`): states are nodes, transitions are links (drag from an `out` pin to an `in` pin), parameters on the left, the selected state (motion, loop, speed, speed parameter, default) or transition (exit time, duration, conditions) in the inspector below them. During Play in editor the running Animator's current state is green and the state it is blending to is blue.

### `--ui-event` verbs (tests and screenshots)

Animation window: `animwindow[:off]`, `animclip:<Assets/..clip.json>`, `animroot:<object>`, `animprop:<object>|<Component.field.part>`, `animkey:<object>|<Component.field.part>|<tick>|<value>`, `animevent:<tick>|<function>[|<int>|<float>|<string>]`, `animrecord:on|off`, `animpreview:on|off`, `animscrub:<tick>`, `animplay:on|off`, `animcurves:on|off`, `animsel:<curve>|<key>`, `animset:fps|length|loop=<v>`, `animundo`, `animredo`, `animsave`, `animdump` (prints `anim: clip=.. curves=.. tick=.. preview=.. record=..` and a line per curve and event), `animvalue:<object>|<Component.field.part>` (the preview's value).

Animator graph: `animatorgraph[:off]`, `animctl:<Assets/..controller.json>`, `animstate:add|rm|rename|default|motion|opts|..`, `animparam:add|<name>|<type>[|<default>]` / `animparam:rm|<name>`, `animtrans:<from or *>|<to>[|<cond>;<cond>]` (`speed>0.1`, `n==3`, `jump`, `!grounded`), `animtransset:<index>|exit=<t>|duration=<s>`, `animtransrm:<index>`, `animctlsel:state|<name>`, `animctlundo`, `animctlredo`, `animctlsave`, `animctldump`, `animlive` (the running Animator's state during Play in editor).

## Audio (API 8)

- **Mixer groups** `Master`, `Music`, `SFX` (`AudioMixerGroups`): `SetVolume(group, linear)` with linear 0..2 (default 1), `SetVolumeDb(group, dB)` (0 dB = 1, -6 dB about 0.5, -80 dB and below = silent, up to +6 dB; a table, no libm), `SetMuted`, `GetVolume`, `IsMuted`, `ResetAll`. The values are kept in the API and pushed to the mixer whenever one is bound, so menus can set them before the game starts. Master scales everything, Music scales the streamed music voice and the voices of `AudioSource`s whose `outputGroup` is Music, SFX the SFX voices. Changes apply to voices that are already playing. Mixing stays integer (Q15).
- **`AudioSource`:** `playOnAwake` plays the clip the first time the object is active and the component enabled (default off, so scenes made before F6 stay silent), `mute` plays at volume 0, `outputGroup` picks the group, `PlayOneShot` follows both.
- **`AudioSource::PlayClipAtPoint(clip, position, volume)`:** fire and forget through a mixer voice (SFX group). Pan comes from the main camera's right vector and the volume falls off linearly from 1 unit (full) to 40 units (not played); it returns false when the clip is unknown or out of range.
