#!/usr/bin/env python3
"""Regenerates samples/animator_player: ProjectSettings, the player sheet, three property clips (Idle, Run, Jump), the Player animator
controller and the scene. The PlayerBot script in Assets/Scripts is hand-written (its .meta is only created when missing).
  python3 samples/animator_player/tools/make_sample.py"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anim_gen as ag

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
gen = ag.Gen(ROOT, "AnimatorPlayer", 1)
ag.settings(gen, ["PlayerBot"])
ag.copy_player(gen)
guids, ctl = ag.write_anim_assets(gen, with_jump=True, with_events=False)
objs = ag.backdrop(gen)
objs.append(ag.player_obj(gen, [("PlayerBot", {"runFrom": 60, "runTo": 260, "jumpAt": [140, 330], "endAt": 420})], ctl))
objs.append(ag.label(gen, "StateLabel", 8, 8))
objs.append(ag.label(gen, "ParamLabel", 8, 24))
gen.wj("Assets/Scenes/Main.scene", {"format": 1, "guid": gen.scene_guid, "objects": objs})
gen.meta("Assets/Scenes/Main.scene", gen.scene_guid)
ag.script_meta(gen, "PlayerBot")
print("animator_player: clips, controller, scene and settings written")
