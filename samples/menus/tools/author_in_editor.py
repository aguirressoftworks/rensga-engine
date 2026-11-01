#!/usr/bin/env python3
"""Builds the menus in the editor: runs rensga-editor under xvfb with --ui-event verbs (GameObject > UI creation, UnityEvent wiring, scripts)
and lets it save Title, Game and TitleBot. The scene files in samples/menus are what the editor wrote.
   python3 samples/menus/tools/author_in_editor.py <rensga-editor> [<project dir>] [<screenshot dir>]
The editor is the build tree's (project scripts hot-build in the background, so `waitbuild` comes first). Needs xvfb-run."""
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
def button(name, label, x, y, w=110, sort=None, parent=""):
    e("uicreate:Button|", parent, "|", name)
    p = {"UIButton__x": x, "UIButton__y": y, "UIButton__w": w}
    if sort is not None: p["UIButton__sortingOrder"] = sort
    props(name, **p)
    q = {"UIText__label": '"%s"' % label, "UIText__x": x, "UIText__y": y}
    if sort is not None: q["UIText__sortingOrder"] = sort + 1
    props(name + "Label", **q)
def text(name, label, x, y, anchor=4, parent="", sort=None):
    e("uicreate:Text|", parent, "|", name)
    p = {"UIText__label": '"%s"' % label, "UIText__x": x, "UIText__y": y, "UIText__anchor": anchor}
    if sort is not None: p["UIText__sortingOrder"] = sort
    props(name, **p)

def common():
    e("uicreate:Canvas||Canvas")
    e("uicreate:EventSystem||EventSystem")

def title(bot):
    e("openscene:", "TitleBot" if bot else "Title")
    common()
    text("Heading", "RENSGA MENUS", 0, -90)
    text("Hint", "D-PAD MOVE   CONFIRM SELECT", 0, 110)
    button("PlayButton", "PLAY", -110, -30)
    button("OptionsButton", "OPTIONS", -110, 0)
    button("QuitButton", "QUIT", -110, 30)
    e("uicreate:Image||OptionsPanel")
    props("OptionsPanel", UIImage__x=105, UIImage__y=0, UIImage__w=200, UIImage__h=120, UIImage__color="[30,30,60,220]", UIImage__sortingOrder=-3)
    text("VolumeText", "VOLUME 100%", 105, -45, parent="OptionsPanel")
    e("uicreate:Slider|OptionsPanel|VolumeSlider")
    props("VolumeSlider", UIImage__x=105, UIImage__y=-18, UIImage__w=160, Slider__value=1.0)
    props("VolumeSliderFill", UIImage__x=105, UIImage__y=-18, UIImage__w=160)
    props("VolumeSliderHandle", UIImage__x=185, UIImage__y=-18)
    e("uicreate:Toggle|OptionsPanel|MuteToggle")
    props("MuteToggle", UIImage__x=45, UIImage__y=18, Toggle__isOn="false")
    props("MuteToggleCheck", UIImage__x=45, UIImage__y=18)
    props("MuteToggleLabel", UIText__label='"MUTE OFF"', UIText__anchor=4, UIText__x=110, UIText__y=18)
    e("new:MenuFlowObject"); e("select:MenuFlowObject"); e("addscript:MenuFlow")
    if bot:
        e("new:MenuBotObject"); e("select:MenuBotObject"); e("addscript:MenuBot")
    e("uievent:PlayButton|UIButton.onClick|MenuFlowObject|MenuFlow.Play")
    e("uievent:OptionsButton|UIButton.onClick|MenuFlowObject|MenuFlow.ToggleOptions")
    e("uievent:QuitButton|UIButton.onClick|MenuFlowObject|MenuFlow.Quit")
    e("uievent:VolumeSlider|Slider.onValueChanged|MenuFlowObject|MenuFlow.SetVolume|dynamic")
    e("uievent:MuteToggle|Toggle.onValueChanged|MenuFlowObject|MenuFlow.SetMute|dynamic")
    e("select:Heading")
    if shots and not bot:
        e("shot:%s/menus-title-editor.png" % shots)
    e("uiactive:OptionsPanel|0")
    e("save")

def game():
    e("openscene:Game")
    common()
    text("GameTitle", "GAME RUNNING", 0, -40)
    text("ClockText", "TIME 00:00", 10, 10, anchor=0)
    text("HintText", "START PAUSE", -10, -10, anchor=8)
    e("uicreate:Image||PausePanel")
    props("PausePanel", UIImage__x=0, UIImage__y=0, UIImage__w=480, UIImage__h=272, UIImage__color="[0,0,0,170]", UIImage__sortingOrder=5)
    text("PauseHeading", "PAUSED", 0, -50, parent="PausePanel", sort=7)
    button("ResumeButton", "RESUME", 0, -10, w=150, sort=6, parent="PausePanel")
    button("QuitToTitleButton", "QUIT TO TITLE", 0, 30, w=150, sort=6, parent="PausePanel")
    e("new:MenuFlowObject"); e("select:MenuFlowObject"); e("addscript:MenuFlow"); e("setfield:MenuFlow.mode=1")
    e("uievent:ResumeButton|UIButton.onClick|MenuFlowObject|MenuFlow.Resume")
    e("uievent:QuitToTitleButton|UIButton.onClick|MenuFlowObject|MenuFlow.ToTitle")
    e("select:PausePanel")
    if shots:
        e("shot:%s/menus-pause-editor.png" % shots)
    e("uiactive:PausePanel|0")
    e("save")

e("waitbuild")
title(False)
game()
title(True)
e("quit")
cmd = ["xvfb-run", "-a", "-s", "-screen 0 1600x900x24", editor, "--project", proj, "--no-splash", "--auto-rebuild", "--quit-after", "4000"]
for x in ev: cmd += ["--ui-event", x]
env = dict(os.environ)
env.pop("WAYLAND_DISPLAY", None)
env.update({"QT_QPA_PLATFORM": "xcb", "SDL_VIDEODRIVER": "x11"})
sys.exit(subprocess.call(cmd, env=env))
