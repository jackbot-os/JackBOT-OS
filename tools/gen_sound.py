#!/usr/bin/env python3
import sys, struct, math

SR = 48000

def adsr(i, n, attack, release):
    a = int(SR * attack)
    r = int(SR * release)
    if i < a:
        return i / a
    if i > n - r:
        v = (n - i) / r
        return v if v > 0 else 0.0
    return 1.0

def note(freq, dur, attack=0.02, release=0.25, harm=(1.0, 0.28, 0.10, 0.04)):
    n = int(SR * dur)
    out = [0.0] * n
    for i in range(n):
        ph = 2.0 * math.pi * freq * i / SR
        v = 0.0
        for h, amp in enumerate(harm, start=1):
            v += amp * math.sin(ph * h)
        out[i] = v * adsr(i, n, attack, release)
    return out

def mix(buf, seg, start, gain=1.0):
    s = int(SR * start)
    for i, v in enumerate(seg):
        idx = s + i
        if 0 <= idx < len(buf):
            buf[idx] += v * gain

def normalize(buf, peak=0.82):
    m = max(1.0, max(abs(v) for v in buf))
    g = peak / m
    return [v * g for v in buf]

def fade_ends(buf, fin=0.04, fout=0.5):
    n = len(buf)
    fi = int(SR * fin)
    fo = int(SR * fout)
    for i in range(n):
        g = 1.0
        if i < fi:
            g = i / fi
        elif i > n - fo:
            g = max(0.0, (n - i) / fo)
        buf[i] *= g
    return buf

def write_wav(path, mono):
    n = len(mono)
    frames = bytearray()
    for v in mono:
        x = int(max(-1.0, min(1.0, v)) * 32767)
        frames += struct.pack("<hh", x, x)
    data = bytes(frames)
    fmt = struct.pack("<HHIIHH", 1, 2, SR, SR * 4, 4, 16)
    with open(path, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", 36 + len(data)))
        f.write(b"WAVE")
        f.write(b"fmt ")
        f.write(struct.pack("<I", 16))
        f.write(fmt)
        f.write(b"data")
        f.write(struct.pack("<I", len(data)))
        f.write(data)
    sys.stderr.write("%s: %.2fs stereo 48000Hz, %d frames\n" % (path, n / SR, n))

def build_boot(path):
    total = 3.0
    buf = [0.0] * int(SR * total)
    # warm rising arpeggio C5 E5 G5 C6, legato
    seq = [(523.25, 0.00, 0.42), (659.25, 0.30, 0.42),
           (783.99, 0.60, 0.42), (1046.50, 0.90, 0.55)]
    for f, st, du in seq:
        mix(buf, note(f, du, 0.02, 0.18), st, 0.9)
    # resolving major chord C5 E5 G5 held, fading
    chord_start = 1.30
    for f, g in [(523.25, 0.9), (659.25, 0.75), (783.99, 0.6), (1046.50, 0.35)]:
        mix(buf, note(f, 1.65, 0.03, 1.3), chord_start, g)
    # soft bass C4 foundation
    mix(buf, note(261.63, 1.7, 0.05, 1.2, harm=(1.0, 0.2, 0.06)), 1.25, 0.5)
    buf = normalize(buf)
    buf = fade_ends(buf, 0.03, 0.7)
    write_wav(path, buf)

def build_warn(path):
    total = 1.4
    buf = [0.0] * int(SR * total)
    bright = (1.0, 0.4, 0.2, 0.08)
    # two attention beeps then a slightly higher resolving beep
    mix(buf, note(880.00, 0.22, 0.01, 0.08, harm=bright), 0.05, 0.95)
    mix(buf, note(880.00, 0.22, 0.01, 0.08, harm=bright), 0.34, 0.95)
    mix(buf, note(1174.66, 0.55, 0.01, 0.35, harm=bright), 0.63, 1.0)
    buf = normalize(buf)
    buf = fade_ends(buf, 0.01, 0.2)
    write_wav(path, buf)

if __name__ == "__main__":
    which = sys.argv[1]
    if which == "boot":
        build_boot(sys.argv[2])
    elif which == "warn":
        build_warn(sys.argv[2])
    else:
        sys.stderr.write("usage: gen_sound.py boot|warn out.wav\n")
        sys.exit(2)
