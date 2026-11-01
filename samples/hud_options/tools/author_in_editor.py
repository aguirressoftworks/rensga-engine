#!/usr/bin/env python3
"""Builds the HUD and the options menu in the editor (rensga-editor --ui-event verbs under xvfb); the scene files are what the editor saved.
   python3 samples/hud_options/tools/author_in_editor.py <rensga-editor> [<project dir>] [<screenshot dir>]"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE = os.path.normpath(os.path.join(HERE, ".."))
editor = sys.argv[1]
proj = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else SAMPLE
shots = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else None
ev = []

def reset_scenes():  # authoring is idempotent: start every scene the editor will fill from an empty one (same guid)
    import glob, json
    for f in glob.glob(os.path.join(proj, "Assets", "Scenes", "*.scene")):
        j = json.load(open(f))
        j["objects"] = []
        json.dump(j, open(f, "w"), indent=1)
        open(f, "a").write("\n")
reset_scenes()
def e(*a): ev.append("".join(a))
def props(obj, **kw): e("uiprops:", obj, "|", ";".join("%s=%s" % (k.replace("__", "."), v) for k, v in kw.items()))
def text(name, label, x, y, anchor=4, sort=None):
    e("uicreate:Text||", name)
    p = {"UIText__label": '"%s"' % label, "UIText__x": x, "UIText__y": y, "UIText__anchor": anchor}
    if sort is not None: p["UIText__sortingOrder"] = sort
    props(name, **p)

def hud(bot):
    e("openscene:", "HudBot" if bot else "Hud")
    e("uicreate:Canvas||Canvas")
    e("uicreate:Image||HealthBack")
    props("HealthBack", UIImage__anchor=0, UIImage__x=10, UIImage__y=10, UIImage__w=154, UIImage__h=14, UIImage__color="[30,30,50,255]", UIImage__sortingOrder=0)
    e("uicreate:Image||HealthFill")
    props("HealthFill", UIImage__anchor=0, UIImage__x=12, UIImage__y=12, UIImage__w=150, UIImage__h=10, UIImage__color="[230,70,60,255]", UIImage__sortingOrder=1,
          UIImage__fillMethod=1, UIImage__fillAmount=1.0)
    text("HpText", "HP 100/100", 14, 28, anchor=0)
    text("ScoreText", "SCORE 000000", -10, 12, anchor=2)
    text("TimerText", "00:00", 0, 12, anchor=1)
    text("Hint", "STARTS IN THE HUD, THEN OPTIONS", 0, -10, anchor=7)
    e("new:HudObject"); e("select:HudObject"); e("addscript:HudDemo")
    if bot:
        e("new:HudBotObject"); e("select:HudBotObject"); e("addscript:HudBot")
    e("select:HealthFill")
    if shots and not bot:
        e("shot:%s/hud-editor.png" % shots)
    e("save")

def options(verify):
    e("openscene:", "OptionsVerify" if verify else "Options")
    e("uicreate:Canvas||Canvas")
    e("uicreate:EventSystem||EventSystem")
    text("Heading", "OPTIONS", 0, -85)
    text("MusicText", "MUSIC 50%", 0, -55)
    e("uicreate:Slider||MusicSlider")
    props("MusicSlider", UIImage__x=0, UIImage__y=-35, UIImage__w=220, Slider__value=0.5)
    props("MusicSliderFill", UIImage__x=0, UIImage__y=-35, UIImage__w=110)
    props("MusicSliderHandle", UIImage__x=0, UIImage__y=-35)
    text("SfxText", "EFFECTS 50%", 0, -5)
    e("uicreate:Slider||SfxSlider")
    props("SfxSlider", UIImage__x=0, UIImage__y=15, UIImage__w=220, Slider__value=0.5)
    props("SfxSliderFill", UIImage__x=0, UIImage__y=15, UIImage__w=110)
    props("SfxSliderHandle", UIImage__x=0, UIImage__y=15)
    e("uicreate:Button||BackButton")
    props("BackButton", UIButton__x=0, UIButton__y=65, UIButton__w=110)
    props("BackButtonLabel", UIText__label='"BACK"', UIText__x=0, UIText__y=65)
    e("new:OptionsObject"); e("select:OptionsObject"); e("addscript:OptionsMenu")
    if verify:
        e("setfield:OptionsMenu.verifyOnly=1")
    e("uievent:MusicSlider|Slider.onValueChanged|OptionsObject|OptionsMenu.SetMusic|dynamic")
    e("uievent:SfxSlider|Slider.onValueChanged|OptionsObject|OptionsMenu.SetSfx|dynamic")
    e("uievent:BackButton|UIButton.onClick|OptionsObject|OptionsMenu.Back")
    e("select:MusicSlider")
    if shots and not verify:
        e("shot:%s/options-editor.png" % shots)
    e("save")

e("waitbuild")
hud(False); options(False); options(True); hud(True)
e("quit")
cmd = ["xvfb-run", "-a", "-s", "-screen 0 1600x900x24", editor, "--project", proj, "--no-splash", "--auto-rebuild", "--quit-after", "4000"]
for x in ev: cmd += ["--ui-event", x]
env = dict(os.environ)
env.pop("WAYLAND_DISPLAY", None)
env.update({"QT_QPA_PLATFORM": "xcb", "SDL_VIDEODRIVER": "x11"})
sys.exit(subprocess.call(cmd, env=env))
