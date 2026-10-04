#!/usr/bin/env python3
import sys, json

DATA = sys.argv[1]
OUT_H = sys.argv[2]
OUT_CS = sys.argv[3]

with open(DATA, encoding="utf-8") as f:
    data = json.load(f)

keys = data["keys"]
langs = data["langs"]

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
    f.write("static const char *const i18n_str[I18N_NLANG][I18N_NKEY] = {\n")
    for lg in langs:
        f.write("  {\n")
        for k in keys:
            f.write('    "%s",\n' % cesc(lg["strings"].get(k, "")))
        f.write("  },\n")
    f.write("};\n#endif\n")

with open(OUT_CS, "w", encoding="utf-8") as f:
    for cp in sorted(cps):
        f.write("%d\n" % cp)
