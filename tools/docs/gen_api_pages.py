#!/usr/bin/env python3
"""F5.18: writes docs/rensga-engine/api/*.md, one page per API type added in F5, with PSX/PSP/PS2 availability badges.

The badges are not typed by hand: every member that needs a profile feature names it (`feature`), and the table of that feature in
tools/cooker/data/profile_defaults.json decides yes or no per profile. Limits (lights per object, bones per draw ...) come from the same file.

Usage: gen_api_pages.py [--check]   (--check exits 1 when a committed page differs from what this script writes)
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = ROOT / "tools/cooker/data/profile_defaults.json"
OUTDIR = ROOT / "docs/rensga-engine/api"
PROFILES = [("psx", "PSX"), ("psp", "PSP"), ("ps2", "PS2")]

# Each page: name, one-line summary, intro paragraphs, members [(signature, meaning, feature or None)], limits [limit key, label], notes, example.
# `feature` is a key of profile_defaults.json "features"; None = every profile. A member's feature: None = follow the page, "any" = every profile.
PAGES = [
    dict(
        name="Light", feature="lighting",
        summary="A light that shines on lit meshes at run time (Unity: Light).",
        intro=["Add a `Light` to any object. A directional light shines along the object's forward axis; a point light shines from the object's position. "
               "A mesh with a material is lit by the strongest lights that reach it. A scene with no `Light` and a `DirectionalLight` stays baked into vertex colors."],
        members=[
            ("int type", "0 = directional, 1 = point.", None),
            ("Color32 color", "Light color.", None),
            ("Scalar intensity", "0 to 8. Multiplies the color.", None),
            ("Scalar range", "Point lights: the distance where the light reaches zero.", None),
            ("int renderMode", "0 = Auto, 1 = Important (always among the object's lights), 2 = NotImportant.", None),
        ],
        limits=[("maxLightsPerObject", "Lights per object")],
        notes=["The renderer takes at most 32 lights per frame and, for each object, the strongest N where N is the limit above. "
               "Order: directional and Important lights first, then by intensity times attenuation at the object, ties by component order.",
               "Lights that do not make the cut for an object are ignored for that object, not an error. The `Lights12` scene of `samples/psp3d_room` has 12 point lights on purpose.",
               "The cooker warns when most objects are reached by more lights than the limit."],
        example="""```cpp
Light* l = GameObject::Find("Lamp")->GetComponent<Light>();
l->intensity = 0.5f;           // dim it
l->renderMode = 1;             // keep it among the object's lights
```""",
    ),
    dict(
        name="RenderSettings", feature="lighting",
        summary="Scene-wide ambient light and distance fog (Unity: RenderSettings).",
        intro=["Put one `RenderSettings` on any object. The first active one is used. Without one, ambient is black and fog is off."],
        members=[
            ("Color32 ambient", "Light added to every lit surface.", "lighting"),
            ("bool fogEnabled", "Turns distance fog on.", "fog"),
            ("Color32 fogColor", "Color the scene fades to.", "fog"),
            ("Scalar fogStart", "Distance where fog begins.", "fog"),
            ("Scalar fogEnd", "Distance where fog is complete. Keep it above `fogStart` and inside the camera far plane.", "fog"),
        ],
        limits=[],
        notes=["Set the camera clear color to `fogColor` so the horizon matches the fog."],
        example="""```cpp
RenderSettings* r = GameObject::Find("RenderSettings")->GetComponent<RenderSettings>();
r->fogEnabled = true;
r->fogStart = 8; r->fogEnd = 24;
```""",
    ),
    dict(
        name="SkinnedMeshRenderer", feature="skinning",
        summary="Draws a skinned mesh from a glTF file, driven by an Animation on the same object.",
        intro=["`mesh` is the glTF asset (its first skinned mesh); `material` is a `.mat.json`. The cooker splits a mesh whose triangles touch more bones than "
               "the profile draws at once into batches, so the artist does not have to."],
        members=[
            ("Guid mesh", "The glTF asset.", None),
            ("Guid material", "The material: texture, color, unlit, alpha test.", None),
        ],
        limits=[("maxBonesPerDraw", "Bones per draw (0 = not batched)"), ("maxSkinnedJoints", "Skinned joints per skeleton (0 = none)")],
        notes=["The component needs the `skinning` feature. A scene that uses it on a profile without the feature is refused by the cooker, naming the object.",
               "Vertices are stored in a compact 16-bit form for the PSP; the PC builds use floats. Both give the same game state."],
        example="""```cpp
// Normally set in the editor. At run time, swap the material:
GameObject::Find("Hero")->GetComponent<SkinnedMeshRenderer>()->material = hurtMaterial;
```""",
    ),
    dict(
        name="Animation", feature="skinning",
        summary="Plays one skeletal clip on the SkinnedMeshRenderer of the same object (Unity: Animation).",
        intro=["Time is a whole number of fixed steps, so a clip plays the same on every profile. Skinning matrices are computed in the animation system, "
               "once per fixed step, and shared by every draw."],
        members=[
            ("Guid clip", "The glTF asset; its first animation is played.", None),
            ("bool playAutomatically", "Starts when the scene loads.", None),
            ("int wrapMode", "0 = the clip's own loop flag, 1 = once, 2 = loop.", None),
            ("Scalar speed", "1 = normal.", None),
            ("void Play()", "Restarts the clip from its first frame.", "skinning"),
            ("void Stop()", "Stops and keeps the pose.", "skinning"),
            ("bool isPlaying() const", "False after a one-shot clip ends.", None),
        ],
        limits=[("maxSkinnedJoints", "Skinned joints per skeleton (0 = none)")],
        notes=["`Play()` and `Stop()` fail to compile on a profile without `skinning`, with a hint.",
               "Switching between clips: put each clip on its own object, or restart with `Play()`. Blending is not part of F5."],
        example="""```cpp
Animation* a = GetComponent<Animation>();
if (moving && !a->isPlaying()) a->Play();
if (!moving && a->isPlaying()) a->Stop();
```""",
    ),
    dict(
        name="Application", feature=None,
        summary="Runtime facts and controls. F5 adds the CPU clock, the system language, the confirm button and the pause event.",
        intro=["`Application` already had `Quit`, `isPlaying`, `targetFrameRate` and `platform`. These members are new."],
        members=[
            ("static bool SetCpuClock(int mhz)", "PSP: 222 or 333. Returns false when the platform refuses the value or has no such control; PC builds return false and change nothing.", "cpuClock"),
            ("static SystemLanguage systemLanguage()", "Unity names (`English`, `French` ...). `Unknown` when the platform does not say; treat it as English.", None),
            ("static bool confirmButtonEast()", "True when the platform confirms with the east face button (some regions of the PSP). Pick the confirm button of your menus with it.", None),
            ("virtual void Behaviour::OnApplicationPause(bool paused)", "Called with true when the game is suspended (power switch, Home menu) and false on resume.", None),
        ],
        limits=[],
        notes=["After a resume the frame clock restarts, so the time spent paused is not one long frame.",
               "Run slower to save battery: `Application::SetCpuClock(222)` in a menu scene, 333 in play. PPSSPP accepts both and keeps its own clock, so measure on hardware."],
        example="""```cpp
void Start() override {
  if (Application::platform() == RuntimePlatform::Psp) Application::SetCpuClock(333);
}
void OnApplicationPause(bool paused) override { if (paused) autosave(); }
```""",
    ),
    dict(
        name="TouchScreenKeyboard", feature="osk",
        summary="The platform's on-screen keyboard (Unity: TouchScreenKeyboard).",
        intro=["One keyboard at a time. The text lives in a fixed buffer of 64 characters; there is no heap."],
        members=[
            ("static bool Open(const char* initial, const char* title, u32 maxLen)", "Opens it. False when unsupported or already open.", "osk"),
            ("static Status status()", "`None`, `Visible`, `Done`, `Canceled`, `LostFocus` (the platform failed it).", "any"),
            ("static const char* text()", "The typed text; final once `status()` is `Done`. Never null.", "any"),
        ],
        limits=[],
        notes=["`status()` and `text()` compile everywhere and report `None` and an empty text where there is no keyboard; `Open` is gated.",
               "On the PSP the game keeps drawing while the keyboard is open, and the system draws it on top."],
        example="""```cpp
if (!asked && TouchScreenKeyboard::Open("", "Your name")) asked = true;
if (TouchScreenKeyboard::status() == TouchScreenKeyboard::Status::Done) name = TouchScreenKeyboard::text();
```""",
    ),
    dict(
        name="SystemInfo", feature="battery",
        summary="Battery and memory facts (Unity: SystemInfo).",
        intro=["Sizes follow Unity: the `*Size` getters are in megabytes; `usedMemory` and `usedGraphicsMemory` are in bytes. 0 or -1 mean unknown."],
        members=[
            ("static Scalar batteryLevel()", "0 to 1, or -1 when unknown.", "battery"),
            ("static BatteryStatus batteryStatus()", "`Unknown`, `Charging`, `Discharging`, `NotCharging`, `Full`.", "battery"),
            ("static int systemMemorySize()", "Total RAM in MB. 0 = unknown.", "any"),
            ("static int graphicsMemorySize()", "Video memory in MB. 0 = unknown.", "any"),
            ("static u32 usedMemory()", "Bytes the game has in use (the arena).", "any"),
            ("static u32 usedGraphicsMemory()", "Bytes of video memory in use (buffers and textures).", "any"),
        ],
        limits=[("ramLimitBytes", "Device RAM limit (bytes, 0 = not checked)")],
        notes=["On the PSP the numbers describe the arena the engine made from the pack's ConsoleTarget, not the kernel's free memory: the engine takes almost all of RAM at boot."],
        example="""```cpp
Debug::Log("ram %u of %d MB", SystemInfo::usedMemory(), SystemInfo::systemMemorySize());
if (SystemInfo::batteryLevel() >= 0 && SystemInfo::batteryLevel() < 0.1f) showLowBattery();
```""",
    ),
    dict(
        name="Save", feature=None,
        summary="Saves. F5 adds a mode that uses the platform's own save dialog.",
        intro=["`Save::SaveFile`, `LoadFile`, `Exists`, `SaveInt` and `LoadInt` are unchanged. `setMode` chooses where they go."],
        members=[
            ("void setMode(SaveMode mode)", "`Auto` (default): files in the save folder. `SystemDialog`: the platform's save dialog when it has one (PSP: the system save/load utility). A platform without a dialog falls back to `Auto`.", "saveDialog"),
            ("SaveMode mode()", "The current mode.", None),
        ],
        limits=[],
        notes=["In `SystemDialog` mode a save or load shows the dialog and returns when the player is done. The dialog follows the system confirm button (circle or cross).",
               "Saves are small: at most 16 KB each."],
        example="""```cpp
Save::setMode(SaveMode::SystemDialog);   // PSP: the dialog; elsewhere: ignored, files as before
Save::SaveInt("best", score);
```""",
    ),
    dict(
        name="AudioSource", feature=None,
        summary="Plays a clip. F5 adds `priority`.",
        intro=["`priority` is Unity's: 0 is the most important, 255 the least, 128 the default. When all voices are busy, the mixer replaces the "
               "least important one (the highest number), the oldest among equals. A voice is never replaced by a less important one."],
        members=[
            ("int priority", "0 to 255. Appended field: scenes saved before F5 load with 128.", None),
        ],
        limits=[],
        notes=["Give music 0 or 32 and footsteps 200. With every priority at 128 the old rule holds: the oldest one-shot is replaced.",
               "The PSP mixer runs at 22050 Hz with 16 voices (PPSSPP measured 10% of a core at 222 MHz)."],
        example="""```cpp
GetComponent<AudioSource>()->priority = 40;   // keep this sound when voices run out
```""",
    ),
]


def member_feature(page, feat):
    # None = the member follows the page's feature; "any" = available on every profile
    return None if feat == "any" else (feat or page["feature"])


def badge(prof, feature):
    return "yes" if feature is None or feature in prof["features"] else "no"


def render_page(p, data):
    prof = data["profiles"]
    info = data["featureInfo"]
    o = [f"<!-- GENERATED by tools/docs/gen_api_pages.py from tools/cooker/data/profile_defaults.json. Do not edit by hand. -->\n", f"# {p['name']}\n", p["summary"] + "\n"]
    o.append("Availability: " + " | ".join(f"**{n}** {badge(prof[k], p['feature'])}" for k, n in PROFILES) + "\n")
    if p["feature"]:
        f = info[p["feature"]]
        o.append(f"The component or type needs the `{p['feature']}` feature ({f['what'].lower()}). If it is missing: {f['fix']}.\n")
    for t in p["intro"]:
        o.append(t + "\n")
    o.append("## Members\n")
    o.append("| Member | " + " | ".join(n for _, n in PROFILES) + " | Meaning |")
    o.append("| --- | " + " | ".join("---" for _ in PROFILES) + " | --- |")
    for sig, meaning, feat in p["members"]:
        cells = [badge(prof[k], member_feature(p, feat)) for k, _ in PROFILES]
        o.append(f"| `{sig}` | " + " | ".join(cells) + f" | {meaning} |")
    o.append("")
    if p["limits"]:
        o.append("## Limits\n")
        o.append("| Limit | " + " | ".join(n for _, n in PROFILES) + " |")
        o.append("| --- | " + " | ".join("---" for _ in PROFILES) + " |")
        for key, label in p["limits"]:
            o.append(f"| {label} | " + " | ".join(str(prof[k]["limits"].get(key, 0)) for k, _ in PROFILES) + " |")
        o.append("")
    if p["notes"]:
        o.append("## Notes\n")
        for n in p["notes"]:
            o.append(f"- {n}")
        o.append("")
    o.append("## Example\n")
    o.append(p["example"] + "\n")
    o.append("See also: [API availability by profile](../api-availability.md), [API index](index.md).\n")
    return "\n".join(o)


def render_index(data):
    prof = data["profiles"]
    o = ["<!-- GENERATED by tools/docs/gen_api_pages.py from tools/cooker/data/profile_defaults.json. Do not edit by hand. -->\n", "# F5 API pages\n",
         "The API added for the PSP track. \"yes\" in a profile column means the whole page compiles on that profile; \"part\" means some members are gated "
         "(the page shows which). The tables come from `tools/cooker/data/profile_defaults.json`; the profile table is in "
         "[api-availability.md](../api-availability.md).\n",
         "| Page | PSX | PSP | PS2 | What |", "| --- | --- | --- | --- | --- |"]
    for p in PAGES:
        cells = []
        for k, _ in PROFILES:
            allv = [badge(prof[k], member_feature(p, f)) for _, _, f in p["members"]]
            cells.append("yes" if all(v == "yes" for v in allv) else ("no" if all(v == "no" for v in allv) else "part"))
        o.append(f"| [{p['name']}]({p['name']}.md) | " + " | ".join(cells) + f" | {p['summary']} |")
    o.append("")
    return "\n".join(o)


def all_files():
    data = json.loads(DATA.read_text())
    files = {"index.md": render_index(data)}
    for p in PAGES:
        files[p["name"] + ".md"] = render_page(p, data)
    return files


if __name__ == "__main__":
    files = all_files()
    if "--check" in sys.argv:
        bad = [n for n, t in files.items() if not (OUTDIR / n).exists() or (OUTDIR / n).read_text() != t]
        for n in bad:
            print("DIFF", n)
        sys.exit(1 if bad else 0)
    OUTDIR.mkdir(parents=True, exist_ok=True)
    for n, t in files.items():
        (OUTDIR / n).write_text(t)
    print(f"wrote {len(files)} pages to {OUTDIR.relative_to(ROOT)}")
