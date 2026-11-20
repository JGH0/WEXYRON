#!/usr/bin/env python3
"""
Generate the WEXYRON sound effects and music.

The game itself only ever *loads* files (assets/audio/...), so this script is
the place where the audio content lives. Every sound is synthesized from noise
and sine waves - no samples are downloaded - which keeps the repository free of
third-party audio and makes the sounds tweakable.

Usage:
    python3 tools/generate_sounds.py            # write assets/audio/**
    python3 tools/generate_sounds.py --check    # only verify the files exist

Output (16-bit PCM WAV, 44.1 kHz):
    assets/audio/effects/<name>.wav   - sound effects (1 channel, mono)
    assets/audio/music/menu.wav       - main-menu music (stereo, loops)
    assets/audio/music/game.wav       - in-game ambience (stereo, loops)
"""

import math
import os
import struct
import sys
import wave

import numpy as np

RATE = 44100
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EFFECTS_DIR = os.path.join(ROOT, "assets", "audio", "effects")
MUSIC_DIR = os.path.join(ROOT, "assets", "audio", "music")

rng = np.random.default_rng(20251031)


def reseed(seed):
    global rng
    rng = np.random.default_rng(seed)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def t(seconds):
    return np.arange(int(RATE * seconds), dtype=np.float64) / RATE


def noise(seconds):
    n = int(RATE * seconds)
    return rng.uniform(-1.0, 1.0, n)


def sine(freq, seconds, phase=0.0):
    x = t(seconds)
    # freq may be an array (sweep) or a number
    if np.isscalar(freq):
        return np.sin(2.0 * np.pi * freq * x + phase)
    f = np.asarray(freq, dtype=np.float64)
    return np.sin(2.0 * np.pi * np.cumsum(f) / RATE + phase)


def sweep(f0, f1, seconds, curve=1.0):
    """Frequency sweep from f0 to f1 (exponential by default)."""
    n = int(RATE * seconds)
    if f0 <= 0 or f1 <= 0 or curve <= 0:
        return np.linspace(f0, f1, n)
    return f0 * (f1 / f0) ** (np.linspace(0.0, 1.0, n) ** curve)


def exp_env(seconds, attack=0.002, decay=0.3, power=2.0):
    """Fast attack, exponential-ish decay. `decay` in seconds."""
    n = int(RATE * seconds)
    x = np.arange(n) / RATE
    env = np.exp(-np.maximum(x - attack, 0.0) / max(decay, 1e-4))
    if attack > 0:
        a = int(RATE * attack) + 1
        env[:a] *= np.linspace(0.0, 1.0, a)
    return env ** power if power != 1.0 else env


def adsr(seconds, a=0.01, d=0.1, s=0.7, r=0.2):
    n = int(RATE * seconds)
    env = np.zeros(n)
    ai, di, ri = int(RATE * a), int(RATE * d), int(RATE * r)
    ai = min(ai, n)
    di = min(di, max(n - ai, 0))
    ri = min(ri, max(n - ai - di, 0))
    if ai:
        env[:ai] = np.linspace(0, 1, ai)
    if di:
        env[ai:ai + di] = np.linspace(1, s, di)
    sus = n - ai - di - ri
    if sus > 0:
        env[ai + di:ai + di + sus] = s
    if ri:
        env[n - ri:] = np.linspace(s if sus > 0 else 1.0, 0, ri)
    return env


def lowpass(x, cutoff):
    """One-pole low-pass (cutoff in Hz)."""
    from scipy.signal import lfilter
    a = math.exp(-2.0 * math.pi * cutoff / RATE)
    return lfilter([1.0 - a], [1.0, -a], x)


def highpass(x, cutoff):
    return x - lowpass(x, cutoff)


def bandpass(x, low, high):
    return lowpass(highpass(x, low), high)


def saturate(x, drive=2.0):
    return np.tanh(x * drive) / np.tanh(drive)


def place(buf, sound, at):
    """Mix `sound` into `buf` at time `at` seconds (buffer is float array)."""
    start = int(RATE * at)
    end = min(len(buf), start + len(sound))
    if start >= len(buf):
        return
    buf[start:end] += sound[:end - start]


def normalize(x, peak=0.9):
    m = np.max(np.abs(x)) if len(x) else 0.0
    if m < 1e-9:
        return x
    return x * (peak / m)


def fade_edges(x, seconds=0.005):
    n = min(int(RATE * seconds), len(x) // 2)
    if n <= 0:
        return x
    x[:n] *= np.linspace(0, 1, n)
    x[-n:] *= np.linspace(1, 0, n)
    return x


def write_mono(name, data, peak=0.8):
    data = normalize(fade_edges(np.asarray(data, dtype=np.float64)), peak)
    path = os.path.join(EFFECTS_DIR, name + ".wav")
    write_wav(path, data, channels=1)


def write_wav(path, data, channels=2):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = np.clip(data, -1.0, 1.0)
    ints = (data * 32767.0).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(ints.tobytes())


# ---------------------------------------------------------------------------
# Sound effects
# ---------------------------------------------------------------------------

def synth_gunshot(body_freq=140.0, duration=0.32, punch=1.0, tail=0.12):
    n = int(RATE * duration)
    out = np.zeros(n)

    # 1) The crack: a very short, very bright noise burst.
    crack = highpass(noise(duration), 1800.0)
    crack *= exp_env(duration, 0.0, 0.012, 1.0)
    out += crack * 1.1

    # 2) The body: broadband noise through a moving low-pass, like a
    #    pressure wave. Big for shotguns, tight for pistols.
    body = bandpass(noise(duration), 60.0, 2600.0)
    body *= exp_env(duration, 0.001, 0.045, 1.2)
    out += saturate(body, 3.0) * 1.4 * punch

    # 3) The thump: a pitch-dropping sine for physical weight.
    thump = sine(sweep(body_freq * 3.0, body_freq * 0.6, duration, 2.0), duration)
    thump *= exp_env(duration, 0.0, 0.06, 1.5)
    out += thump * 1.0 * punch

    # 4) The room tail: filtered noise decaying slowly.
    room = lowpass(noise(duration), 900.0)
    room *= exp_env(duration, 0.02, tail, 1.0)
    out += room * 0.45

    # A little tape-like saturation over the whole hit.
    return saturate(out, 1.6)


def synth_click(freq=2400.0, duration=0.05, decay=0.008):
    x = highpass(noise(duration), 1500.0) * exp_env(duration, 0.0, decay, 1.0)
    x += sine(freq, duration) * exp_env(duration, 0.0, decay * 1.5, 2.0) * 0.4
    return x


def synth_reload():
    # Magazine out, magazine in, slide rack: three mechanical clicks with
    # different pitch/weight so the action reads clearly.
    dur = 0.85
    buf = np.zeros(int(RATE * dur))
    place(buf, synth_click(1900.0, 0.08, 0.010) * 1.0, 0.02)
    place(buf, synth_click(1200.0, 0.10, 0.018) * 0.7, 0.10)
    place(buf, synth_click(1500.0, 0.12, 0.020) * 1.1, 0.42)   # magazine slam
    place(buf, synth_click(2600.0, 0.10, 0.012) * 0.9, 0.60)   # slide back
    place(buf, synth_click(2100.0, 0.12, 0.016) * 1.0, 0.72)   # slide forward
    # Slight metallic ring on the last click.
    place(buf, sine(3600.0, 0.18) * exp_env(0.18, 0.001, 0.05, 2.0) * 0.08, 0.72)
    return buf


def synth_footstep(surface="concrete"):
    dur = 0.18
    if surface == "concrete":
        x = bandpass(noise(dur), 180.0, 1800.0) * exp_env(dur, 0.001, 0.028, 1.5)
        heel = sine(sweep(160.0, 70.0, dur, 2.0), dur) * exp_env(dur, 0.0, 0.03, 2.0) * 0.7
        return saturate(x + heel, 1.4)
    if surface == "metal":
        x = bandpass(noise(dur), 400.0, 5000.0) * exp_env(dur, 0.0, 0.02, 1.5)
        ring = (sine(1250.0, dur) + sine(1870.0, dur) * 0.6) * exp_env(dur, 0.0, 0.05, 2.0)
        return saturate(x + ring * 0.35, 1.5)
    # grass / soft
    x = lowpass(noise(dur), 1400.0) * exp_env(dur, 0.002, 0.05, 1.2)
    return x


def synth_explosion(duration=1.6):
    out = np.zeros(int(RATE * duration))

    # Sub-bass shock: very low, long decay.
    sub = sine(sweep(90.0, 25.0, duration, 3.0), duration)
    sub *= exp_env(duration, 0.005, 0.5, 1.4)
    out += sub * 1.5

    # Main blast: heavily saturated low-passed noise.
    blast = lowpass(noise(duration), 320.0) * exp_env(duration, 0.001, 0.28, 1.2)
    out += saturate(blast * 2.5, 3.0) * 1.2

    # Debris/roar: band-limited noise with a slower decay than the blast.
    roar = bandpass(noise(duration), 300.0, 4000.0) * exp_env(duration, 0.02, 0.45, 1.0)
    out += roar * 0.5

    # Cracks at the start for detail.
    crack = highpass(noise(duration), 2500.0) * exp_env(duration, 0.0, 0.04, 1.0)
    out += crack * 0.35
    return saturate(out, 1.3)


def synth_flashbang(duration=1.8):
    out = np.zeros(int(RATE * duration))
    # Sharp bang + a long, ear-ringing high sine that fades.
    place(out, synth_gunshot(body_freq=220.0, duration=0.5, punch=1.2, tail=0.25), 0.0)
    ring = (sine(4200.0, duration) + sine(6100.0, duration) * 0.5)
    ring *= exp_env(duration, 0.03, 0.55, 1.3)
    out += ring * 0.12
    return out


def synth_hit_flesh():
    dur = 0.16
    thud = sine(sweep(220.0, 90.0, dur, 2.0), dur) * exp_env(dur, 0.0, 0.035, 2.0)
    slap = bandpass(noise(dur), 250.0, 2500.0) * exp_env(dur, 0.0, 0.02, 1.5)
    return saturate(thud + slap * 0.8, 1.8)


def synth_hitmarker():
    # Very short, bright "tick" played through the UI bus when a shot lands.
    dur = 0.075
    x = highpass(noise(dur), 3000.0) * exp_env(dur, 0.0, 0.004, 2.0) * 0.7
    x += sine(3300.0, dur) * exp_env(dur, 0.0, 0.010, 2.0) * 0.45
    x += sine(2100.0, dur) * exp_env(dur, 0.0, 0.014, 2.0) * 0.20
    return x


def synth_kill_bling():
    # Two rising chimes for a confirmed kill.
    dur = 0.42
    out = np.zeros(int(RATE * dur))
    for i, freq in enumerate((1318.51, 1760.0, 2637.02)):
        note = (sine(freq, 0.28) + sine(freq * 2.0, 0.28) * 0.25)
        note *= adsr(0.28, 0.002, 0.05, 0.55, 0.16)
        place(out, note * (0.55 - i * 0.10), i * 0.055)
    return out


def synth_sniper_bolt():
    # Bolt-action cycle: unlock, pull back, push forward, lock down.
    dur = 0.72
    buf = np.zeros(int(RATE * dur))
    place(buf, synth_click(2400.0, 0.06, 0.008) * 0.8, 0.02)
    place(buf, synth_click(1500.0, 0.09, 0.015) * 0.9, 0.14)
    place(buf, bandpass(noise(0.10), 1200.0, 6000.0) * exp_env(0.10, 0.0, 0.025, 1.5) * 0.55, 0.24)
    place(buf, synth_click(1800.0, 0.09, 0.014) * 0.9, 0.42)
    place(buf, synth_click(1200.0, 0.10, 0.020) * 1.0, 0.54)
    place(buf, sine(3100.0, 0.20) * exp_env(0.20, 0.001, 0.04, 2.0) * 0.10, 0.54)
    return buf


def synth_sniper_fire_thunder():
    # The normal rifle crack plus a deeper pressure wave and a room echo.
    base = synth_gunshot(body_freq=48.0, duration=1.05, punch=2.15, tail=0.52)
    dur = 1.7
    out = np.zeros(int(RATE * dur))
    place(out, base, 0.0)
    sub = sine(sweep(72.0, 28.0, 0.8, 2.5), 0.8) * exp_env(0.8, 0.0, 0.28, 1.6)
    place(out, sub * 0.55, 0.0)
    place(out, lowpass(base, 900.0) * 0.30, 0.19)
    place(out, lowpass(base, 500.0) * 0.18, 0.39)
    return normalize(out)


def synth_bomb_explosion_echo():
    # Deeper blast with several decaying reflections: every player on the map
    # hears the same round-ending detonation.
    base = synth_explosion(2.8)
    dur = 4.6
    out = np.zeros(int(RATE * dur))
    place(out, base, 0.0)
    place(out, lowpass(base, 1100.0) * 0.48, 0.32)
    place(out, lowpass(base, 700.0)  * 0.30, 0.71)
    place(out, lowpass(base, 420.0)  * 0.20, 1.20)
    place(out, lowpass(base, 260.0)  * 0.12, 1.85)
    return normalize(out)


def synth_help():
    """Odd little confirmation blip (used as a generic positive feedback)."""
    dur = 0.35
    buf = np.zeros(int(RATE * dur))
    for i, (f, at) in enumerate(((880.0, 0.0), (1320.0, 0.09))):
        buf_dur = 0.22
        s = sine(f, buf_dur) * exp_env(buf_dur, 0.002, 0.06, 1.5)
        s += sine(f * 2.0, buf_dur) * exp_env(buf_dur, 0.002, 0.03, 1.5) * 0.3
        place(buf, s, at)
    return buf


def synth_pickup():
    dur = 0.4
    notes = [1046.5, 1318.5, 1568.0]
    buf = np.zeros(int(RATE * dur))
    for i, f in enumerate(notes):
        s = sine(f, 0.25) * exp_env(0.25, 0.002, 0.07, 1.3)
        s += sine(f * 2.0, 0.25) * exp_env(0.25, 0.002, 0.03, 1.5) * 0.25
        place(buf, s, i * 0.07)
    return buf


def synth_jump():
    dur = 0.22
    # Cloth rustle + a small, rising body.
    rustle = bandpass(noise(dur), 600.0, 5000.0) * exp_env(dur, 0.005, 0.05, 1.2)
    body = sine(sweep(180.0, 320.0, dur), dur) * exp_env(dur, 0.005, 0.05, 1.5)
    return rustle * 0.8 + body * 0.35


def synth_land():
    dur = 0.3
    thud = sine(sweep(140.0, 50.0, dur, 2.0), dur) * exp_env(dur, 0.0, 0.07, 1.8)
    grit = bandpass(noise(dur), 200.0, 2200.0) * exp_env(dur, 0.002, 0.05, 1.5)
    return saturate(thud * 1.1 + grit * 0.7, 1.6)


def synth_damage():
    dur = 0.35
    # Distorted body hit + a short ringing pain tone.
    body = saturate(lowpass(noise(dur), 700.0) * exp_env(dur, 0.001, 0.09, 1.4) * 2.5, 4.0)
    tone = sine(sweep(300.0, 160.0, dur, 2.0), dur) * exp_env(dur, 0.002, 0.10, 1.2)
    return body * 0.9 + tone * 0.5


def synth_death():
    dur = 1.4
    out = np.zeros(int(RATE * dur))
    # A low, falling tone that "drops" as the player dies.
    fall = sine(sweep(320.0, 60.0, dur, 1.6), dur) * adsr(dur, 0.01, 0.2, 0.6, 0.6)
    out += saturate(fall, 2.0) * 0.6
    out += lowpass(noise(dur), 500.0) * exp_env(dur, 0.01, 0.5, 1.0) * 0.3
    return out


def synth_round_start():
    dur = 1.6
    out = np.zeros(int(RATE * dur))
    # A short fanfare: two rising notes over a soft pad.
    for i, f in enumerate((523.25, 783.99)):
        s = (sine(f, 0.5) + sine(f * 2.0, 0.5) * 0.3) * adsr(0.5, 0.01, 0.1, 0.6, 0.3)
        place(out, s * 0.5, i * 0.18)
    pad = (sine(130.81, dur) + sine(196.0, dur) * 0.7 + sine(261.63, dur) * 0.5)
    pad *= adsr(dur, 0.15, 0.3, 0.7, 0.8) * 0.35
    out += pad
    return out


def synth_round_end():
    dur = 1.8
    out = np.zeros(int(RATE * dur))
    for i, f in enumerate((783.99, 523.25)):
        s = (sine(f, 0.6) + sine(f * 1.5, 0.6) * 0.25) * adsr(0.6, 0.01, 0.1, 0.6, 0.35)
        place(out, s * 0.5, i * 0.22)
    pad = (sine(98.0, dur) + sine(146.83, dur) * 0.6) * adsr(dur, 0.1, 0.3, 0.7, 0.9) * 0.3
    out += pad
    return out


def synth_ui_hover():
    dur = 0.05
    x = sine(1600.0, dur) * exp_env(dur, 0.001, 0.010, 2.0)
    x += highpass(noise(dur), 3000.0) * exp_env(dur, 0.0, 0.006, 2.0) * 0.3
    return x


def synth_ui_select():
    dur = 0.16
    x = sine(1200.0, dur) * exp_env(dur, 0.001, 0.03, 1.6)
    x += sine(1800.0, dur) * exp_env(dur, 0.001, 0.02, 1.6) * 0.4
    return x


def synth_ui_back():
    dur = 0.16
    x = sine(900.0, dur) * exp_env(dur, 0.001, 0.03, 1.6)
    x += sine(600.0, dur) * exp_env(dur, 0.001, 0.02, 1.6) * 0.4
    return x


def synth_bomb_beep():
    dur = 0.12
    x = sine(1760.0, dur) * adsr(dur, 0.002, 0.01, 0.8, 0.03)
    x += sine(3520.0, dur) * adsr(dur, 0.002, 0.01, 0.5, 0.03) * 0.2
    return x


def synth_plant():
    dur = 0.12
    return synth_click(700.0, dur, 0.02) * 1.2 + sine(sweep(240.0, 120.0, dur), dur) * exp_env(dur, 0, 0.03, 2.0) * 0.6


def synth_defuse():
    dur = 0.18
    return synth_click(1800.0, dur, 0.02) * 0.9 + sine(sweep(900.0, 1400.0, dur), dur) * exp_env(dur, 0, 0.06, 2.0) * 0.35


# ---------------------------------------------------------------------------
# Music (stereo, loops seamlessly)
# ---------------------------------------------------------------------------

def stereo(left, right):
    return np.stack([left, right], axis=1).reshape(-1)


def render_track(seconds, bpm, chords, melody, bass, pad_level, arp=False):
    """Render a simple ambient/electronic loop.

    chords: list of (root_hz, [intervals]) one per bar
    melody: list of (bar, beat, semitones_from_root_of_chord, length_beats)
    bass:   list of (bar, beat, semitones, length_beats)
    """
    beat = 60.0 / bpm
    bar = 4.0 * beat
    n = int(RATE * seconds)
    L = np.zeros(n)
    R = np.zeros(n)

    def note(buf_l, buf_r, start, length, freq, amp, attack=0.01, decay=0.15,
             sustain=0.55, release=0.2, pan=0.0, detune=0.0015):
        length = max(length, 0.02)
        x = t(length)
        env = adsr(length, attack, decay, sustain, release)
        s = np.zeros_like(x)
        # A couple of slightly detuned saw-ish sines make the pad wide and warm.
        for d in (-detune, 0.0, detune):
            f = freq * (1.0 + d)
            s += np.sin(2 * np.pi * f * x) + 0.35 * np.sin(2 * np.pi * f * 2.0 * x)
            s += 0.15 * np.sin(2 * np.pi * f * 3.0 * x)
        s /= 3.0
        s *= env * amp
        gl = math.sqrt(max(0.0, 0.5 * (1.0 - pan)))
        gr = math.sqrt(max(0.0, 0.5 * (1.0 + pan)))
        place(buf_l, s * gl, start)
        place(buf_r, s * gr, start)

    def semis(root, k):
        return root * (2.0 ** (k / 12.0))

    for b, (root, intervals) in enumerate(chords):
        start = b * bar
        # Pad: root + intervals, long and soft.
        for k in intervals:
            note(L, R, start, bar * 1.02, semis(root, k), pad_level, 0.08, 0.3, 0.75, 0.4,
                 pan=(-0.35 if k % 2 else 0.35))
        # Optional arpeggio: 8th notes through the chord.
        if arp:
            step = beat / 2.0
            seq = intervals + intervals[::-1]
            for i in range(8):
                k = seq[i % len(seq)]
                note(L, R, start + i * step, step * 1.1, semis(root, k) * 2.0,
                     pad_level * 0.35, 0.005, 0.05, 0.25, 0.12,
                     pan=0.5 if i % 2 else -0.5)

    for (b, bt, k, ln) in melody:
        root = chords[b][0]
        note(L, R, b * bar + bt * beat, ln * beat, semis(root, k), 0.16,
             0.01, 0.2, 0.6, 0.25, pan=0.15)

    for (b, bt, k, ln) in bass:
        root = chords[b][0]
        start = b * bar + bt * beat
        length = ln * beat
        x = t(length)
        f = semis(root, k - 12) * 0.5
        s = (np.sin(2 * np.pi * f * x) + 0.3 * np.sign(np.sin(2 * np.pi * f * x)))
        s *= adsr(length, 0.01, 0.15, 0.7, 0.15) * 0.35
        place(L, s, start)
        place(R, s, start)

    return L, R


def make_menu_music():
    # Slow, calm loop in A minor: Am - F - C - G, 4 bars, repeated twice.
    bpm = 76
    bar = 60.0 / bpm * 4.0
    chords = [(220.00, [0, 3, 7]),      # Am
              (174.61, [0, 4, 7]),      # F
              (130.81, [0, 4, 7]),      # C
              (196.00, [0, 4, 7])] * 2  # G
    melody = [(0, 0, 12, 1.5), (0, 2, 15, 1.0), (1, 0, 12, 2.0), (1, 2.5, 10, 1.0),
              (2, 0, 19, 1.5), (2, 2, 16, 1.0), (3, 0, 14, 1.5), (3, 2, 12, 2.0),
              (4, 0, 12, 1.0), (4, 1.5, 19, 1.0), (4, 2.5, 15, 1.0),
              (5, 0, 12, 2.0), (6, 0, 16, 1.5), (6, 2, 19, 1.0), (7, 0, 12, 3.0)]
    bass = [(0, 0, 0, 3.5), (1, 0, 0, 3.5), (2, 0, 0, 3.5), (3, 0, 0, 3.5),
            (4, 0, 0, 3.5), (5, 0, 0, 3.5), (6, 0, 0, 3.5), (7, 0, 0, 3.5)]
    L, R = render_track(bar * 8, bpm, chords, melody, bass, pad_level=0.10, arp=True)
    # Seamless loop: crossfade the tail into the head.
    return seamless(L, R)


def make_game_music():
    # A little more driving, lower pad + light pulse.
    bpm = 100
    bar = 60.0 / bpm * 4.0
    chords = [(146.83, [0, 3, 7]),      # Dm
              (130.81, [0, 4, 7]),      # C
              (116.54, [0, 3, 7]),      # Bb
              (110.00, [0, 4, 7])] * 2  # A
    melody = [(0, 0, 12, 0.75), (0, 1, 15, 0.75), (0, 2, 19, 1.5),
              (1, 0, 16, 1.5), (1, 2, 12, 1.0),
              (2, 0, 15, 1.5), (2, 2, 19, 1.0),
              (3, 0, 14, 2.0), (3, 2.5, 16, 1.0),
              (4, 0, 12, 1.0), (4, 1, 15, 1.0), (4, 2, 19, 1.5),
              (5, 0, 16, 1.0), (5, 1, 19, 1.0), (5, 2, 16, 2.0),
              (6, 0, 15, 2.0), (7, 0, 14, 3.0)]
    bass = [(b, 0, 0, 1.0) for b in range(8)] + [(b, 2, 0, 1.0) for b in range(8)]
    L, R = render_track(bar * 8, bpm, chords, melody, bass, pad_level=0.13, arp=False)
    return seamless(L, R)


def seamless(L, R, overlap=1.5):
    """Crossfade the end of a loop into its start so it repeats without a click."""
    k = int(RATE * overlap)
    if k * 2 >= len(L):
        return L, R
    headL, headR = L[:k].copy(), R[:k].copy()
    fade = np.linspace(0.0, 1.0, k)
    L[-k:] = L[-k:] * (1.0 - fade) + headL * fade
    R[-k:] = R[-k:] * (1.0 - fade) + headR * fade
    return L, R


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

EFFECTS = {
    # combat
    "pistol_fire":        lambda: synth_gunshot(body_freq=150.0, duration=0.30, punch=0.9, tail=0.10),
    "shotgun_fire":       lambda: synth_gunshot(body_freq=95.0, duration=0.55, punch=1.35, tail=0.18),
    "dry_fire":           lambda: synth_click(1400.0, 0.07, 0.006),
    "reload":             synth_reload,
    "grenade_throw":      lambda: bandpass(noise(0.2), 500.0, 4000.0) * exp_env(0.2, 0.01, 0.05, 1.2),
    "flashbang_throw":    lambda: bandpass(noise(0.18), 900.0, 5000.0) * exp_env(0.18, 0.01, 0.04, 1.2),
    "explosion":          synth_explosion,
    "flashbang_explode":  synth_flashbang,
    # hits
    "hit_flesh":          synth_hit_flesh,
    "bullet_impact":      lambda: synth_click(900.0, 0.12, 0.025) * 0.9
                          + bandpass(noise(0.12), 400.0, 3000.0) * exp_env(0.12, 0.0, 0.02, 1.5) * 0.8,
    "player_damage":      synth_damage,
    "player_death":       synth_death,
    "enemy_death":        lambda: saturate(lowpass(noise(0.5), 600.0) * exp_env(0.5, 0.005, 0.18, 1.3) * 2.0, 3.0)
                          + sine(sweep(300.0, 80.0, 0.5), 0.5) * exp_env(0.5, 0, 0.15, 1.5) * 0.5,
    # movement
    "footstep_1":         lambda: synth_footstep("concrete"),
    "footstep_2":         lambda: synth_footstep("concrete"),
    "footstep_3":         lambda: synth_footstep("concrete"),
    "footstep_4":         lambda: synth_footstep("concrete"),
    "jump":               synth_jump,
    "land":               synth_land,
    # round / bomb
    "round_start":        synth_round_start,
    "round_end":          synth_round_end,
    "bomb_plant":         synth_plant,
    "bomb_beep":          synth_bomb_beep,
    "bomb_defuse":        synth_defuse,
    "bomb_explode":       lambda: synth_explosion(2.6) * 1.0,
    # interface
    "ui_hover":           synth_ui_hover,
    "ui_select":          synth_ui_select,
    "ui_back":            synth_ui_back,
    "pickup":             synth_pickup,
    "help":               synth_help,
    # sniper: deeper body and a longer room tail than the other rifles.
    # Kept last so adding it does not change the RNG sequence of the sounds
    # above (the generator uses one shared seeded RNG).
    "sniper_fire":        lambda: synth_gunshot(body_freq=70.0, duration=0.85, punch=1.7, tail=0.35),
    # Extra combat feedback and the bolt-action / bomb-defusal sounds. Appended
    # so the RNG sequence of the older effects stays unchanged.
    "hitmarker":          synth_hitmarker,
    "kill_bling":         synth_kill_bling,
    "sniper_bolt":        synth_sniper_bolt,
    "sniper_fire_thunder": synth_sniper_fire_thunder,
    "bomb_explode_echo":  synth_bomb_explosion_echo,
}


def main(argv):
    check_only = "--check" in argv
    if check_only:
        missing = []
        expected = [os.path.join(EFFECTS_DIR, n + ".wav") for n in EFFECTS]
        expected += [os.path.join(MUSIC_DIR, n + ".wav") for n in ("menu", "game")]
        for p in expected:
            if not os.path.isfile(p):
                missing.append(os.path.relpath(p, ROOT))
        if missing:
            print("Missing audio files:")
            for m in missing:
                print("  " + m)
            return 1
        print("All %d audio files present." % len(expected))
        return 0

    os.makedirs(EFFECTS_DIR, exist_ok=True)
    os.makedirs(MUSIC_DIR, exist_ok=True)

    # Each footstep is the same synthesizer with a fresh noise seed so the
    # four steps don't sound like an exact copy.
    for i, name in enumerate(("footstep_1", "footstep_2", "footstep_3", "footstep_4")):
        reseed(1000 + i)
        write_mono(name, EFFECTS[name]())
    reseed(20251031)

    for name, fn in EFFECTS.items():
        if name.startswith("footstep_"):
            continue
        write_mono(name, fn())
        print("effects/%s.wav" % name)

    L, R = make_menu_music()
    write_wav(os.path.join(MUSIC_DIR, "menu.wav"), stereo(L, R) * 0.7, channels=2)
    print("music/menu.wav")

    L, R = make_game_music()
    write_wav(os.path.join(MUSIC_DIR, "game.wav"), stereo(L, R) * 0.7, channels=2)
    print("music/game.wav")

    print("Done: %d effects in %s" % (len(EFFECTS), os.path.relpath(EFFECTS_DIR, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
