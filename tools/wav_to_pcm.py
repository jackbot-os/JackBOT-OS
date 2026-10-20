#!/usr/bin/env python3
import sys, struct

def find_chunks(data):
    chunks = {}
    if len(data) < 12 or data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    off = 12
    while off + 8 <= len(data):
        cid = data[off:off + 4]
        size = struct.unpack_from("<I", data, off + 4)[0]
        body = off + 8
        if cid == b"fmt ":
            chunks["fmt"] = data[body:body + size]
            if body + size <= len(data):
                pass
        elif cid == b"data":
            chunks["data"] = data[body:min(body + size, len(data))]
        off = body + size + (size & 1)
    return chunks

def main():
    if len(sys.argv) != 4:
        sys.stderr.write("usage: wav_to_pcm.py in.wav out.pcm target_rate\n")
        return 2
    inp, outp, trate = sys.argv[1], sys.argv[2], int(sys.argv[3])

    with open(inp, "rb") as f:
        data = f.read()

    chunks = find_chunks(data)
    if "fmt" not in chunks:
        raise ValueError("missing fmt chunk")
    fmt = chunks["fmt"]
    fmttag, ch, sr, _br, _ba, sw = struct.unpack_from("<HHIIHH", fmt, 0)
    if fmttag != 1:
        sys.stderr.write("warning: non-PCM tag=%d, assuming PCM\n" % fmttag)
    if sw not in (16, 8):
        sys.stderr.write("only 8/16-bit PCM supported, got %d\n" % sw)
        return 2
    if "data" not in chunks:
        raise ValueError("missing data chunk")
    raw = chunks["data"]

    if sw == 8:
        vals = [b - 128 for b in raw]
    else:
        ns = len(raw) // 2
        vals = list(struct.unpack_from("<%dh" % ns, raw, 0))

    n = len(vals) // ch
    if ch == 1:
        mono = vals[:n]
    else:
        mono = [0] * n
        for i in range(n):
            acc = 0
            for c in range(ch):
                acc += vals[i * ch + c]
            mono[i] = acc // ch

    if sr == trate:
        out = mono
    else:
        total = int(len(mono) * trate / sr)
        out = [0] * total
        ratio = sr / trate
        for j in range(total):
            pos = j * ratio
            i0 = int(pos)
            frac = pos - i0
            a = mono[i0]
            b = mono[i0 + 1] if i0 + 1 < len(mono) else a
            v = int(a + (b - a) * frac)
            if v > 32767:
                v = 32767
            if v < -32768:
                v = -32768
            out[j] = v

    with open(outp, "wb") as f:
        f.write(struct.pack("<%dh" % len(out), *out))
    sys.stderr.write("%s: %dHz %dch %dbit -> %dHz mono, %d samples (%.2fs)\n"
                     % (inp, sr, ch, sw, trate, len(out), len(out) / trate))
    return 0

if __name__ == "__main__":
    sys.exit(main())
