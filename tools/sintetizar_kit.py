#!/usr/bin/env python3
# Kupata — synthesises the factory kit ("Kit Analógico").
# Copyright (C) 2026 Lautaro Martín Vieyra
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Every sound is built from scratch with the principles of the classic analog drum
# machines of the early 80s: damped resonators (the bridged-T circuit) struck by a
# pulse, filtered noise, and a bank of six detuned square-wave oscillators for the
# metal sounds. No samples are used, so the kit can be shared freely.
#
#   python3 tools/sintetizar_kit.py            writes Resources/kit/*.flac
#   python3 tools/sintetizar_kit.py --report kit.png   also prints an analysis and saves spectrograms

import json
import sys
from pathlib import Path

import numpy as np
from scipy import signal

SR = 48000
ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Resources" / "kit"


def t_axis(seconds):
    return np.arange(int(seconds * SR)) / SR


def noise(seconds, seed):
    return np.random.default_rng(seed).uniform(-1.0, 1.0, int(seconds * SR))


def resonator(freq, tau, seconds, bend=0.0, bend_tau=0.01, phase=0.0):
    """A struck bridged-T: a damped sine whose pitch falls a little at the start."""
    t = t_axis(seconds)
    f = freq * (1.0 + bend * np.exp(-t / bend_tau))
    ph = 2.0 * np.pi * np.cumsum(f) / SR + phase
    return np.sin(ph) * np.exp(-t / tau)


def square(freq, seconds, phase):
    """Band-limited square wave (odd harmonics below 20 kHz)."""
    t = t_axis(seconds)
    out = np.zeros_like(t)
    k = 1
    while k * freq < 20000.0:
        out += np.sin(2.0 * np.pi * k * freq * t + k * phase) / k
        k += 2
    return out * (4.0 / np.pi)


METAL = (205.3, 304.4, 369.6, 522.7, 540.0, 800.0)   # the six oscillators of the metal sounds


def metal(seconds, seed):
    rng = np.random.default_rng(seed)
    return sum(square(f, seconds, rng.uniform(0, 2 * np.pi)) for f in METAL) / len(METAL)


def bandpass(x, lo, hi, order=2):
    sos = signal.butter(order, [lo, hi], btype="bandpass", fs=SR, output="sos")
    return signal.sosfilt(sos, x)


def highpass(x, fc, order=2):
    return signal.sosfilt(signal.butter(order, fc, btype="highpass", fs=SR, output="sos"), x)


def lowpass(x, fc, order=2):
    return signal.sosfilt(signal.butter(order, fc, btype="lowpass", fs=SR, output="sos"), x)


def drive(x, amount):
    """Soft saturation, as the analog output stages do."""
    return np.tanh(amount * x) / np.tanh(amount)


def env(seconds, tau, attack=0.0):
    t = t_axis(seconds)
    e = np.exp(-t / tau)
    if attack > 0.0:
        e *= np.clip(t / attack, 0.0, 1.0)
    return e


def click(seconds, freq=1500.0, width=0.0012):
    """The trigger pulse that leaks into the bass drum: a short, rounded thump."""
    t = t_axis(seconds)
    c = np.where(t < width, np.sin(np.pi * t / width), 0.0)
    return lowpass(c, freq, 1)


def finish(x, peak_db, fade=0.006):
    n = int(fade * SR)
    if n > 0 and len(x) > n:
        x[-n:] *= 0.5 + 0.5 * np.cos(np.linspace(0.0, np.pi, n))
    return x / np.max(np.abs(x)) * 10.0 ** (peak_db / 20.0)


# ---------------------------------------------------------------------------------------------

def kick(f0, tau, seconds, bend, bend_tau, click_level, amount):
    body = resonator(f0, tau, seconds, bend, bend_tau)
    return drive(body + click_level * click(seconds), amount)


def snare(f1, f2, tau_tone, tau_noise, snappy, hp, seed, seconds=0.6):
    tone = 0.62 * resonator(f1, tau_tone, seconds, 0.08, 0.008) + 0.38 * resonator(f2, tau_tone * 0.7, seconds, 0.05, 0.008)
    n = lowpass(highpass(noise(seconds, seed), hp, 2), 9500.0, 1) * env(seconds, tau_noise, 0.0004)
    return drive(tone + snappy * n * 2.2, 1.3)


def clap(seed, seconds=0.62):
    t = t_axis(seconds)
    n = bandpass(noise(seconds, seed), 750.0, 1700.0, 2)
    e = np.zeros_like(t)
    for start in (0.0, 0.0105, 0.021, 0.0315):   # four claps a few milliseconds apart
        k = (t >= start) & (t < start + 0.0105)
        e[k] = np.maximum(e[k], 1.0 - (t[k] - start) / 0.0105)
    tail = np.where(t >= 0.0315, 0.55 * np.exp(-(t - 0.0315) / 0.065), 0.0)
    return highpass(n * np.maximum(e, tail), 300.0, 1)


def rimshot(seconds=0.12):
    x = 0.6 * resonator(455.0, 0.0065, seconds) + 0.4 * resonator(1667.0, 0.0045, seconds)
    return highpass(drive(x * 3.0, 1.0), 300.0, 2)


def hat(tau, seconds, seed):
    m = metal(seconds, seed)
    x = highpass(bandpass(m, 5600.0, 9500.0, 2), 6800.0, 2)
    return x * env(seconds, tau, 0.0003)


def cymbal(seed, seconds=2.6):
    m = metal(seconds, seed)
    low = bandpass(m, 2800.0, 4200.0, 2) * env(seconds, 0.14, 0.0004)
    high = highpass(bandpass(m, 5600.0, 9500.0, 2), 6500.0, 2) * env(seconds, 0.55, 0.0004)
    x = 0.5 * low + 0.85 * high
    n = int(0.45 * SR)
    x[-n:] *= np.linspace(1.0, 0.0, n) ** 2
    return x


def tom(f0, tau, seconds):
    """Deep toms: a falling resonator, a softer partial a fifth above (the skin), and the stick's click."""
    body = resonator(f0, tau, seconds, 0.25, 0.06) + 0.18 * resonator(f0 * 1.5, tau * 0.4, seconds, 0.2, 0.04, 0.7)
    return drive(body + 0.2 * click(seconds, 1800.0, 0.001), 1.3)


def conga(f0, tau, seconds):
    """Congas as on the analog machines: the tom circuit tuned higher and shorter, an octave-ish
    partial for the skin, and the slap of the hand in the attack. No noise."""
    body = resonator(f0, tau, seconds, 0.06, 0.006) + 0.10 * resonator(f0 * 2.02, tau * 0.35, seconds, 0.0, 0.01, 1.1)
    return drive(body + 0.25 * click(seconds, 3000.0, 0.0006), 1.4)


def cowbell(seconds=0.65):
    x = 0.55 * square(540.0, seconds, 0.3) + 0.45 * square(800.0, seconds, 1.1)
    x = lowpass(bandpass(x, 480.0, 3200.0, 2), 5000.0, 2)
    t = t_axis(seconds)
    return drive(x * (0.7 * np.exp(-t / 0.006) + 0.32 * np.exp(-t / 0.11)), 1.4)


def clave(seconds=0.09):
    return drive(resonator(2500.0, 0.005, seconds) * 1.6, 1.6)


def maracas(seed, seconds=0.11):
    return highpass(noise(seconds, seed), 5000.0, 2) * env(seconds, 0.013, 0.004)


# ---------------------------------------------------------------------------------------------
# number of the sound, file, English, Spanish, choke group, sound
KIT = [
    (1,  "bombo-largo",    "Kick long",    "Bombo largo",      0, lambda: finish(kick(49.0, 0.28, 2.0, 0.35, 0.018, 0.25, 1.4), -1.0, 0.04)),
    (2,  "bombo-corto",    "Kick short",   "Bombo corto",      0, lambda: finish(kick(56.0, 0.075, 0.6, 0.6, 0.012, 0.5, 2.0), -1.0, 0.02)),
    (3,  "tambor",         "Snare",        "Tambor",           0, lambda: finish(snare(185.0, 330.0, 0.045, 0.055, 0.55, 1800.0, 3), -2.0, 0.03)),
    (4,  "tambor-brillante", "Snare bright", "Tambor brillante", 0, lambda: finish(snare(205.0, 360.0, 0.035, 0.075, 0.8, 2500.0, 4), -2.0, 0.03)),
    (5,  "palmas",         "Clap",         "Palmas",           0, lambda: finish(clap(5), -2.0, 0.02)),
    (6,  "aro",            "Rimshot",      "Aro",              0, lambda: finish(rimshot(), -4.0)),
    (7,  "hihat-cerrado",  "Closed hat",   "Hihat cerrado",    1, lambda: finish(hat(0.0125, 0.15, 7), -4.0)),
    (8,  "hihat-abierto",  "Open hat",     "Hihat abierto",    1, lambda: finish(hat(0.1, 0.8, 7), -5.0, 0.05)),
    (9,  "platillo",       "Cymbal",       "Platillo",         0, lambda: finish(cymbal(9), -6.0)),
    (10, "tom-grave",      "Low tom",      "Tom grave",        0, lambda: finish(tom(74.0, 0.26, 1.6), -2.0, 0.03)),
    (11, "tom-medio",      "Mid tom",      "Tom medio",        0, lambda: finish(tom(106.0, 0.21, 1.3), -2.0, 0.03)),
    (12, "tom-agudo",      "High tom",     "Tom agudo",        0, lambda: finish(tom(150.0, 0.17, 1.1), -2.0, 0.03)),
    (13, "cencerro",       "Cowbell",      "Cencerro",         0, lambda: finish(cowbell(), -4.0, 0.02)),
    (14, "clave",          "Clave",        "Clave",            0, lambda: finish(clave(), -5.0)),
    (15, "maracas",        "Maracas",      "Maracas",          0, lambda: finish(maracas(15), -6.0)),
    (16, "bombo-sub",      "Sub kick",     "Bombo sub",        0, lambda: finish(kick(41.2, 0.6, 3.6, 0.5, 0.02, 0.35, 3.0), -1.0, 0.3)),
    (17, "conga-grave",    "Low conga",    "Conga grave",      0, lambda: finish(conga(185.0, 0.11, 0.6), -3.0, 0.02)),
    (18, "conga-media",    "Mid conga",    "Conga media",      0, lambda: finish(conga(277.0, 0.085, 0.5), -3.0, 0.02)),
    (19, "conga-aguda",    "High conga",   "Conga aguda",      0, lambda: finish(conga(370.0, 0.065, 0.45), -3.0, 0.02)),
]

# The kit as it comes: which sound goes on each pad, from pad 1 (bottom left) to 16, in the order of
# Sitala's 808 kit. The sounds left out (short kick, bright snare, sub kick) stay in the list of
# factory sounds, for any pad.
LAYOUT = [1, 3, 7, 8,     # kick, snare, closed hat, open hat
          9, 10, 11, 12,  # cymbal, low, mid and high tom
          17, 18, 19, 5,  # low, mid and high conga, clap
          14, 15, 13, 6]  # clave, maracas, cowbell, rimshot


def write_kit():
    import soundfile as sf
    OUT.mkdir(parents=True, exist_ok=True)
    sounds = {}
    for pad, name, en, es, choke, make in KIT:
        x = make().astype(np.float64)
        sf.write(OUT / f"{pad:02d}-{name}.flac", x, SR, subtype="PCM_24")
        sounds[pad] = x
    manifest = {"sounds": [{"number": p, "file": f"{p:02d}-{n}.flac", "en": en, "es": es, "choke": c} for p, n, en, es, c, _ in KIT],
                "layout": LAYOUT}
    (OUT / "kit.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    return sounds


def report(sounds, image):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(5, 4, figsize=(16, 14))
    for (pad, name, en, es, choke, _), ax in zip(KIT, axes.ravel()):
        x = sounds[pad]
        peak = np.max(np.abs(x))
        e = np.abs(signal.hilbert(x))
        above = np.nonzero(e > peak * 1e-3)[0]
        length = (above[-1] + 1) / SR if len(above) else 0.0
        spec = np.abs(np.fft.rfft(x * np.hanning(len(x)), 1 << 17))
        freqs = np.fft.rfftfreq(1 << 17, 1 / SR)
        centroid = float(np.sum(freqs * spec) / np.sum(spec))
        strongest = float(freqs[np.argmax(spec[freqs > 25]) + np.count_nonzero(freqs <= 25)])
        print(f"{pad:2d} {es:17s} peak {20*np.log10(peak):6.1f} dB  -60 dB at {length*1000:6.0f} ms  "
              f"centroid {centroid:7.0f} Hz  strongest {strongest:7.1f} Hz  choke {choke}")
        f, tt, S = signal.spectrogram(x, SR, nperseg=1024, noverlap=896)
        ax.pcolormesh(tt * 1000, f, 10 * np.log10(S + 1e-12), shading="auto", vmin=-120, vmax=-30, cmap="magma")
        ax.set_yscale("symlog", linthresh=200)
        ax.set_ylim(20, 20000)
        ax.set_title(f"{pad} · {es}", fontsize=10)
        ax.tick_params(labelsize=7)
    fig.tight_layout()
    fig.savefig(image, dpi=70)


if __name__ == "__main__":
    s = write_kit()
    if "--report" in sys.argv:
        i = sys.argv.index("--report")
        report(s, sys.argv[i + 1] if i + 1 < len(sys.argv) else "kit.png")
