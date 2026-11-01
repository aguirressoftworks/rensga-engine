#!/usr/bin/env python3
"""Regenerates samples/footsteps: ProjectSettings, the player sheet, Idle and Run clips (Run has two Footstep animation events), the
controller, the generated step and hum sounds, and the scene. The Footsteps script is hand-written.
  python3 samples/footsteps/tools/make_sample.py"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "animator_player", "tools"))
import anim_gen as ag

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
STEP, HUM = "00000000-0000-4000-8000-00000000d201", "00000000-0000-4000-8000-00000000d202"
gen = ag.Gen(ROOT, "Footsteps", 2)
ag.settings(gen, ["Footsteps"])
ag.copy_player(gen)
guids, ctl = ag.write_anim_assets(gen, with_jump=False, with_events=True)
ag.write_wav(os.path.join(ROOT, "Assets/Audio/step.wav"), ag.step_sound())
gen.meta("Assets/Audio/step.wav", STEP)
ag.write_wav(os.path.join(ROOT, "Assets/Audio/hum.wav"), ag.hum_sound())
gen.meta("Assets/Audio/hum.wav", HUM)
objs = ag.backdrop(gen)
runner = ag.player_obj(gen, [("AudioSource", {"clip": STEP, "volume": 1.0, "playOnAwake": False, "outputGroup": 2}),
                             ("Footsteps", {"idleUntil": 30, "runUntil": 330, "endAt": 380})], ctl)
objs.append(runner)
objs.append(gen.obj("Ambience", comps=[("AudioSource", {"clip": HUM, "volume": 0.5, "loop": True, "playOnAwake": True, "outputGroup": 1})]))
objs.append(ag.label(gen, "StateLabel", 8, 8))
gen.wj("Assets/Scenes/Main.scene", {"format": 1, "guid": gen.scene_guid, "objects": objs})
gen.meta("Assets/Scenes/Main.scene", gen.scene_guid)
ag.script_meta(gen, "Footsteps")
print("footsteps: clips, controller, sounds, scene and settings written")
