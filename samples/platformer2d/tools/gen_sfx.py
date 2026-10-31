#!/usr/bin/env python3
"""Generates the platformer2d sound effects and the music loop (MIT, stdlib only, own synthesis).

Writes 22050 Hz mono 16-bit WAVs. Only integer maths and square/triangle/noise oscillators are used (no libm), so
the output is byte-identical on every machine. Run: python3 tools/gen_sfx.py   (from samples/platformer2d)
  Assets/Audio/jump.wav coin.wav hit.wav save.wav   short effects, packed into the pack (AUDI)
  Assets/Audio/lose.wav clear.wav select.wav over.wav  (F4.01) life lost, room cleared / goal, menu blip, game over
  Streaming/music.wav music2.wav music3.wav          ~6-8 s loops, NOT packed: copied next to the pack and streamed from disc
                                                    (music = title / room 1 / results, music2 = room 2, music3 = room 3)
make_sample.py calls generate(<project dir>) so a generated project (psp output) gets the same audio.
"""
import os
import struct
import wave

RATE = 22050
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Noise:
    def __init__(self, seed=0x1234ABCD):
        self.s = seed

    def next(self):  # 32-bit LCG -> -1..1 (as an integer ratio)
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return ((self.s >> 16) & 0xFFFF) - 32768


def square(phase, duty=0.5):  # phase 0..1 -> +-32767
    return 32767 if phase < duty else -32767


def tri(phase):
    return int((4 * phase - 1 if phase < 0.5 else 3 - 4 * phase) * 32767)


def tone(freq, seconds, wave_fn, vol, f_end=None, attack=0.004, release=0.4):
    n = int(RATE * seconds)
    out, ph = [], 0.0
    for i in range(n):
        t = i / n
        f = freq if f_end is None else freq + (f_end - freq) * t
        ph = (ph + f / RATE) % 1.0
        env = min(1.0, i / max(1, attack * RATE))
        if t > 1.0 - release:
            env *= (1.0 - t) / release
        out.append(int(wave_fn(ph) * vol * env))
    return out


def write(path, samples):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", max(-32768, min(32767, s))) for s in samples))
    print("wrote", os.path.relpath(path, ROOT), len(samples), "frames")


def mix_at(dst, src, at):
    for i, s in enumerate(src):
        if at + i < len(dst):
            dst[at + i] += s


NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def hz(name):  # e.g. "A4"; equal temperament via a table of 2^(k/12) so no libm is needed
    semis = NOTE[name[0]] + (1 if "#" in name else 0) + 12 * (int(name[-1]) - 4) - 9
    f = 440.0
    ratio = 1.0594630943592953
    for _ in range(abs(semis)):
        f = f * ratio if semis > 0 else f / ratio
    return f


def jump():
    return tone(220, 0.18, lambda p: square(p, 0.25), 0.45, f_end=620, release=0.5)


def coin():
    a = tone(hz("B5"), 0.07, lambda p: square(p, 0.5), 0.4, release=0.1)
    b = tone(hz("E6"), 0.30, lambda p: square(p, 0.5), 0.4, release=0.9)
    return a + b


def hit():
    n, out, lp = Noise(), [], 0
    for i in range(int(RATE * 0.22)):
        t = i / (RATE * 0.22)
        lp += (n.next() - lp) // 3  # one-pole low pass
        out.append(int(lp * 0.9 * (1.0 - t) ** 2))
    return out


def save():
    out = []
    for name in ("C5", "E5", "G5", "C6"):
        out += tone(hz(name), 0.09, tri, 0.55, release=0.3)
    return out + tone(hz("C6"), 0.18, tri, 0.55, release=1.0)


def lose():  # a falling minor third and a low thud
    return tone(hz("E4"), 0.12, lambda p: square(p, 0.5), 0.4, release=0.2) + tone(hz("C4"), 0.12, lambda p: square(p, 0.5), 0.4, release=0.2) + \
        tone(hz("G3"), 0.28, tri, 0.5, f_end=hz("C3"), release=0.9)


def clear():  # a short rising fanfare
    out = []
    for name, sec in (("C5", 0.10), ("E5", 0.10), ("G5", 0.10), ("C6", 0.26), ("G5", 0.08), ("C6", 0.40)):
        out += tone(hz(name), sec, lambda p: square(p, 0.25), 0.34, release=0.4)
    return out


def select():  # menu blip
    return tone(hz("A5"), 0.05, lambda p: square(p, 0.5), 0.3, release=0.4)


def over():  # sad descending line
    out = []
    for name, sec in (("G4", 0.18), ("E4", 0.18), ("C4", 0.18), ("G3", 0.50)):
        out += tone(hz(name), sec, tri, 0.5, release=0.5)
    return out


def music_track(bpm, bass, lead, bass_wave, lead_duty, lead_vol, hat_every):
    """A 16-beat loop: a bass note every 2 beats, a lead note every beat, noise hi-hats. All integer / table maths."""
    beats = 16
    beat = 60.0 / bpm
    total = int(RATE * beat * beats)
    buf = [0] * total
    for i, name in enumerate(bass):
        mix_at(buf, tone(hz(name), beat * 1.9, bass_wave, 0.35, release=0.25), int(RATE * beat * 2 * i))
    for i, name in enumerate(lead):
        mix_at(buf, tone(hz(name), beat * 0.9, lambda p: square(p, lead_duty), lead_vol, release=0.5), int(RATE * beat * i))
    n = Noise(7)
    for i in range(beats * 2 // hat_every):  # hi-hat on every half beat (or sparser)
        at = int(RATE * beat * 0.5 * i * hat_every)
        mix_at(buf, [int(n.next() * 0.10 * (1 - k / 600)) for k in range(600)], at)
    return buf


def music():  # title, room 1 and the results: the original cheerful loop, 120 bpm in C major
    return music_track(120, ["C3", "C3", "A2", "A2", "F2", "F2", "G2", "G2"],
                       ["E4", "G4", "C5", "G4", "E4", "A4", "C5", "A4", "F4", "A4", "C5", "A4", "D4", "G4", "B4", "G4"], tri, 0.25, 0.16, 1)


def music2():  # room 2 (slopes and planks): a bouncier loop in G major
    return music_track(132, ["G2", "G2", "E2", "E2", "C3", "C3", "D3", "D3"],
                       ["B4", "D5", "G5", "D5", "B4", "E5", "G5", "E5", "C5", "E5", "G5", "E5", "D5", "F5", "A5", "F5"], tri, 0.5, 0.15, 1)


def music3():  # room 3 (the gauntlet): faster, minor
    return music_track(150, ["A2", "A2", "F2", "F2", "D2", "D2", "E2", "E2"],
                       ["A4", "C5", "E5", "C5", "A4", "F5", "E5", "C5", "F4", "A4", "D5", "A4", "E4", "G#4", "B4", "G#4"], square_bass, 0.125, 0.17, 1)


def square_bass(p):
    return square(p, 0.5) // 2


def generate(root):
    global ROOT
    ROOT = root
    for name, fn in (("jump", jump), ("coin", coin), ("hit", hit), ("save", save), ("lose", lose), ("clear", clear), ("select", select),
                     ("over", over)):
        write(os.path.join(root, "Assets/Audio", name + ".wav"), fn())
    write(os.path.join(root, "Streaming/music.wav"), music())
    write(os.path.join(root, "Streaming/music2.wav"), music2())
    write(os.path.join(root, "Streaming/music3.wav"), music3())


if __name__ == "__main__":
    generate(ROOT)
