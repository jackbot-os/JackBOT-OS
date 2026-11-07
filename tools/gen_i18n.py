#!/usr/bin/env python3
import sys, json

DATA = sys.argv[1]
OUT_H = sys.argv[2]
OUT_CS = sys.argv[3]

with open(DATA, encoding="utf-8") as f:
    data = json.load(f)

keys = data["keys"]
langs = data["langs"]

# Only ship languages that carry a REAL translation for every key. A
# language with holes would fall back to English and leave foreign text
# on screen, which is exactly what the "one language, no leftovers"
# rule forbids. Add translations first, then the language appears.
_full = []
_missing = {}
for lg in langs:
    miss = [k for k in keys if not (lg["strings"].get(k) or "").strip()]
    if miss and lg["code"] != "en":
        _missing[lg["code"]] = miss
    else:
        _full.append(lg)
if _missing:
    print("i18n: dropping %d incomplete languages (first: %s, missing %d keys)"
          % (len(_missing), sorted(_missing)[0], len(next(iter(_missing.values())))),
          file=sys.stderr)
langs = _full
if len(langs) != len(data["langs"]):
    print("i18n: shipping %d complete languages" % len(langs), file=sys.stderr)

def cesc(s):
    out = ""
    for ch in s:
        if ch == "\\":
            out += "\\\\"
        elif ch == '"':
            out += '\\"'
        elif ord(ch) < 32:
            out += "\\x%02x" % ord(ch)
        else:
            out += ch
    return out

cps = set()
for lg in langs:
    for k in keys:
        for ch in lg["strings"].get(k, ""):
            if ord(ch) >= 128:
                cps.add(ord(ch))
    for ch in lg["name"]:
        if ord(ch) >= 128:
            cps.add(ord(ch))

# Arabic is shaped at runtime in gfxtext.c (initial/medial/final/iso
# forms plus the lam+alef ligatures), so the bitmap font must carry the
# whole Presentation Forms-B block the shaper can emit.
if any(0x0600 <= c <= 0x06FF for c in cps):
    for c in range(0xFE80, 0xFEFD):
        cps.add(c)

with open(OUT_H, "w", encoding="utf-8") as f:
    f.write("#ifndef I18N_DATA_H\n#define I18N_DATA_H\n")
    f.write("#define I18N_NLANG %d\n" % len(langs))
    f.write("#define I18N_NKEY %d\n" % len(keys))
    for i, k in enumerate(keys):
        f.write("#define STR_%s %d\n" % (k.upper(), i))
    f.write("static const char *const i18n_codes[I18N_NLANG] = {\n")
    for lg in langs:
        f.write('  "%s",\n' % lg["code"])
    f.write("};\n")
    f.write("static const char *const i18n_names[I18N_NLANG] = {\n")
    for lg in langs:
        f.write('  "%s",\n' % cesc(lg["name"]))
    f.write("};\n")
    f.write("static const unsigned char i18n_rtl_flags[I18N_NLANG] = {\n  ")
    f.write(",".join("1" if lg.get("rtl") else "0" for lg in langs))
    f.write("\n};\n")
    en = langs[0]["strings"]
    f.write("static const char *const i18n_str[I18N_NLANG][I18N_NKEY] = {\n")
    for lg in langs:
        f.write("  {\n")
        for k in keys:
            val = lg["strings"].get(k)
            if not val:
                val = en.get(k, "")
            f.write('    "%s",\n' % cesc(val))
        f.write("  },\n")
    f.write("};\n#endif\n")

with open(OUT_CS, "w", encoding="utf-8") as f:
    for cp in sorted(cps):
        f.write("%d\n" % cp)
