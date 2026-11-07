#!/usr/bin/env python3
# JackBOT self-trained intent network.
# Pure stdlib: word-boundary matching, real forward/backprop, cross-entropy, Adam.
# Trains a small ReLU MLP on labelled sentences, then exports Q16.16 C weights.
import json
import math
import os
import random

random.seed(20261031)


def is_letter(c):
    return c.isalpha()


def has_word(text, trig):
    t = text.lower()
    w = trig.lower()
    i = 0
    while True:
        j = t.find(w, i)
        if j < 0:
            return False
        before_ok = j == 0 or not is_letter(t[j - 1])
        after = j + len(w)
        after_ok = after >= len(t) or not is_letter(t[after])
        if before_ok and after_ok:
            return True
        i = j + 1


# ---- feature vocabulary: word-boundary triggers, case-insensitive ----
FEATURES = [
    ["hello", "hi", "hey", "greetings", "howdy", "hiya",
     "good morning", "good evening", "good afternoon"],
    ["help", "assist", "support", "what can", "options", "skills", "what now"],
    ["time", "clock", "o'clock"],
    ["calc", "compute", "math", "plus", "multiply", "add",
     "calculate", "times", "minus", "sum"],
    ["system", "cpu", "spec", "processor", "specs", "computer", "machine"],
    ["ram", "memory", "mem", "info"],
    ["weather", "temperature", "rain", "sunny", "forecast", "raining"],
    ["light", "lamp", "device", "appliance", "smart", "lights"],
    ["thanks", "thank", "appreciate", "thx", "cheers"],
    ["date", "today", "calendar", "day"],
    ["disk", "storage", "space", "drive", "hard disk"],
    ["version", "build", "release"],
    ["who", "your name", "what are you", "introduce", "yourself", "name"],
    ["goodbye", "bye", "later", "farewell", "see you", "see ya"],
    ["network", "internet", "wifi", "online", "connection"],
    ["file", "folder", "document"],
]
NF = len(FEATURES)

INTENT_NAMES = [
    "GREET", "HELP", "TIME", "COMPUTE", "SYSTEM", "DATE",
    "VERSION", "DISK", "IDENTITY", "THANKS", "GOODBYE",
    "WEATHER", "SMART",
]
NO = len(INTENT_NAMES)
NH = 12

RAW = {
    "GREET": [
        "hello", "hi", "hey there", "hello jackbot", "hi there",
        "greetings", "howdy", "hey", "hello there", "good morning",
        "hi jackbot", "hello assistant", "hey buddy", "good evening",
        "hello can you hear me", "hiya", "good afternoon",
    ],
    "HELP": [
        "help", "help me", "what can you do", "i need help",
        "show me help", "assist me", "what can i ask", "give me support",
        "help please", "what are your skills", "can you assist",
        "i need support", "options please", "what now",
    ],
    "TIME": [
        "what time is it", "tell me the time", "time", "current time",
        "clock", "what time", "give me the time", "time please",
        "show clock", "what is the time", "tell time", "time now",
        "what's the time", "do you know the time",
    ],
    "COMPUTE": [
        "calc 5 plus 3", "compute 12 times 8", "what is 4 add 9",
        "math 7 minus 2", "calculate 10 plus 5", "multiply 6 and 7",
        "can you add 3 and 4", "multiply 5 and 6", "compute sum",
        "math please", "calc 100 minus 35", "do the math 8 plus 1",
        "calculate 9 times 9", "add these numbers",
    ],
    "SYSTEM": [
        "system info", "show cpu", "cpu info", "system spec",
        "processor info", "how much ram", "memory status",
        "show system", "computer info", "ram size", "cpu and memory",
        "system resources", "tell me specs", "machine info",
        "how much memory do i have",
    ],
    "DATE": [
        "what is the date", "today", "date", "current date",
        "what day is it", "tell me today", "calendar date",
        "date please", "what is today", "which day", "show the date",
        "today's date", "give me the calendar day",
    ],
    "VERSION": [
        "what version", "version", "build number", "which release",
        "version number", "show version", "what release is this",
        "current build", "give me the version", "release info",
        "what is your build number",
    ],
    "DISK": [
        "disk space", "free space", "storage info", "how much disk",
        "hard disk space", "drive space", "show storage",
        "disk free", "remaining space", "disk drive info",
        "how much storage left", "check the drive",
    ],
    "IDENTITY": [
        "who are you", "what are you", "your name", "who r u",
        "tell me about yourself", "what is your name", "who is this",
        "introduce yourself", "what are you called", "who am i talking to",
    ],
    "THANKS": [
        "thanks", "thank you", "thanks a lot", "appreciate it",
        "thank you so much", "many thanks", "cheers thanks",
        "thx", "thanks buddy", "i appreciate you",
    ],
    "GOODBYE": [
        "goodbye", "bye", "see you later", "see you", "later",
        "good bye", "bye bye", "catch you later", "farewell",
        "gotta go bye", "see ya",
    ],
    "WEATHER": [
        "what is the weather", "weather", "temperature outside",
        "is it raining", "weather please", "how sunny is it",
        "tell me the weather", "current temperature", "forecast",
        "will it rain",
    ],
    "SMART": [
        "turn on the light", "lamp on", "control my device",
        "turn on appliance", "switch the light", "smart device",
        "activate the lamp", "turn on the lights", "home appliance",
        "control the lamp",
    ],
}

NAME2IDX = {n: i for i, n in enumerate(INTENT_NAMES)}


def vectorize(sentence):
    return [1.0 if has_word(sentence, w) else 0.0
            for group in FEATURES for w in [group[0]]]


def vectorize_full(sentence):
    return [1.0 if any(has_word(sentence, w) for w in group) else 0.0
            for group in FEATURES]


SAMPLES = []
for name, sents in RAW.items():
    y = NAME2IDX[name]
    for s in sents:
        SAMPLES.append((vectorize_full(s), y, s))

random.shuffle(SAMPLES)
print("samples:", len(SAMPLES), "features:", NF, "hidden:", NH, "intents:", NO)

# check for all-zero and conflicting vectors
from collections import defaultdict
v2 = defaultdict(set)
for x, y, _ in SAMPLES:
    v2[tuple(x)].add(INTENT_NAMES[y])
zeros = [v for v in v2 if 1 not in v]
conf = [v for v in v2 if len(v2[v]) > 1]
print("distinct vectors:", len(v2), "all-zero:", len(zeros),
      "conflicting:", len(conf))

TEST_N = max(1, len(SAMPLES) // 8)
TEST = SAMPLES[:TEST_N]
TRAIN = SAMPLES[TEST_N:]


def he(r, c):
    scale = math.sqrt(2.0 / r)
    return [[random.gauss(0, scale) for _ in range(c)] for _ in range(r)]


W1 = he(NF, NH)
b1 = [0.0] * NH
W2 = he(NH, NO)
b2 = [0.0] * NO


def softmax(z):
    m = max(z)
    e = [math.exp(v - m) for v in z]
    t = sum(e)
    return [v / t for v in e]


def forward(x):
    z1 = [b1[j] + sum(W1[i][j] * x[i] for i in range(NF))
          for j in range(NH)]
    h = [v if v > 0 else 0.0 for v in z1]
    z2 = [b2[k] + sum(W2[j][k] * h[j] for j in range(NH))
          for k in range(NO)]
    return z1, h, z2, softmax(z2)


# Adam state
def zeros_like_2(m, n):
    return [[0.0] * n for _ in range(m)]


mW1 = zeros_like_2(NF, NH)
vW1 = zeros_like_2(NF, NH)
mW2 = zeros_like_2(NH, NO)
vW2 = zeros_like_2(NH, NO)
mb1 = [0.0] * NH
vb1 = [0.0] * NH
mb2 = [0.0] * NO
vb2 = [0.0] * NO

EPOCHS = 260
LR = 0.02
B1, B2, EPS = 0.9, 0.999, 1e-8
step = 0

for ep in range(EPOCHS):
    ep_loss = 0.0
    order = list(range(len(TRAIN)))
    random.shuffle(order)
    for oi in order:
        x, y, _ = TRAIN[oi]
        z1, h, z2, p = forward(x)
        ep_loss -= math.log(max(p[y], 1e-12))
        step += 1
        dz2 = p[:]
        dz2[y] -= 1.0
        gW2 = [[h[j] * dz2[k] for k in range(NO)] for j in range(NH)]
        gb2 = dz2[:]
        dh = [sum(W2[j][k] * dz2[k] for k in range(NO)) for j in range(NH)]
        dz1 = [dh[j] if z1[j] > 0 else 0.0 for j in range(NH)]
        gW1 = [[x[i] * dz1[j] for j in range(NH)] for i in range(NF)]
        gb1 = dz1[:]
        bc1 = 1.0 - B1 ** step
        bc2 = 1.0 - B2 ** step

        def adam2(w, g, m, v):
            for a in range(len(w)):
                for b in range(len(w[0])):
                    m[a][b] = B1 * m[a][b] + (1 - B1) * g[a][b]
                    v[a][b] = B2 * v[a][b] + (1 - B2) * g[a][b] * g[a][b]
                    mh = m[a][b] / bc1
                    vh = v[a][b] / bc2
                    w[a][b] -= LR * mh / (math.sqrt(vh) + EPS)

        adam2(W1, gW1, mW1, vW1)
        adam2(W2, gW2, mW2, vW2)
        for a in range(NH):
            mb1[a] = B1 * mb1[a] + (1 - B1) * gb1[a]
            vb1[a] = B2 * vb1[a] + (1 - B2) * gb1[a] * gb1[a]
            b1[a] -= LR * (mb1[a] / bc1) / (math.sqrt(vb1[a] / bc2) + EPS)
        for a in range(NO):
            mb2[a] = B1 * mb2[a] + (1 - B1) * gb2[a]
            vb2[a] = B2 * vb2[a] + (1 - B2) * gb2[a] * gb2[a]
            b2[a] -= LR * (mb2[a] / bc1) / (math.sqrt(vb2[a] / bc2) + EPS)
    if ep % 30 == 0 or ep == EPOCHS - 1:
        print("epoch %3d  avg_loss %.5f" % (ep, ep_loss / len(TRAIN)))


def accuracy(data):
    correct = 0
    per_class = {}
    for x, y, _ in data:
        _, _, _, p = forward(x)
        pred = max(range(NO), key=lambda k: p[k])
        per_class.setdefault(y, [0, 0])
        per_class[y][1] += 1
        if pred == y:
            correct += 1
            per_class[y][0] += 1
    return correct / len(data), per_class


train_acc, _ = accuracy(TRAIN)
test_acc, pc = accuracy(TEST)
print("train accuracy: %.1f%%" % (train_acc * 100))
print("held-out accuracy: %.1f%% (%d samples)" % (test_acc * 100, len(TEST)))
for y in sorted(pc):
    c, t = pc[y]
    print("  %-8s %d/%d" % (INTENT_NAMES[y], c, t))

Q = 1 << 16
INT32_MIN, INT32_MAX = -(2**31), 2**31 - 1


def q16(v):
    return int(round(v * Q))


def in_i32(v):
    return INT32_MIN <= v <= INT32_MAX


def mulf16_sim(a, b, overflow):
    r = (a * b) >> 16
    if not in_i32(r):
        overflow.append(r)
    return r


def fixed_predict(x_float):
    overflow = []
    xq = [Q if v > 0.5 else 0 for v in x_float]
    hq = []
    for j in range(NH):
        acc = q16(b1[j])
        for i in range(NF):
            acc = acc + mulf16_sim(q16(W1[i][j]), xq[i], overflow)
        if not in_i32(acc):
            overflow.append(acc)
        hq.append(acc if acc > 0 else 0)
    oq = []
    for k in range(NO):
        acc = q16(b2[k])
        for j in range(NH):
            acc = acc + mulf16_sim(q16(W2[j][k]), hq[j], overflow)
        if not in_i32(acc):
            overflow.append(acc)
        oq.append(acc)
    pred = max(range(NO), key=lambda k: oq[k])
    return pred, overflow, max(abs(v) for v in hq + oq)


fixed_match = 0
worst = 0
all_overflow = []
for x, y, s in SAMPLES:
    fp, over, w = fixed_predict(x)
    _, _, _, p = forward(x)
    float_pred = max(range(NO), key=lambda k: p[k])
    if fp == float_pred:
        fixed_match += 1
    if fp != y:
        print("  MISMATCH fixed=%d true=%d :: %s" % (fp, y, s))
    worst = max(worst, w)
    all_overflow.extend(over)
print("fixed-point vs float agreement: %.1f%% (%d/%d)"
      % (fixed_match / len(SAMPLES) * 100, fixed_match, len(SAMPLES)))
print("max |Q16 activation| = %d (headroom %d)" % (worst, INT32_MAX))
print("int32 overflows during fixed inference:", len(all_overflow))


all_w = [abs(v) for row in W1 for v in row] + \
        [abs(v) for row in W2 for v in row]
print("max |weight| = %.4f" % max(all_w))
max_q = max(abs(q16(v)) for row in W1 for v in row)
print("max Q16 weight = %d (int32 headroom %d)" % (max_q, 2**31 - 1))

out = []
out.append("/* Auto-generated by tools/train_jbot_nn.py. Do not edit by hand. */")
out.append("/* Self-trained ReLU MLP, Q16.16. train %.1f%% held-out %.1f%% */"
           % (train_acc * 100, test_acc * 100))
out.append("#ifndef JBOT_TRAINED_WEIGHTS_H")
out.append("#define JBOT_TRAINED_WEIGHTS_H")
out.append("")
out.append("#define JBOT_NF %d" % NF)
out.append("#define JBOT_NH %d" % NH)
out.append("#define JBOT_NO %d" % NO)
out.append("")
out.append("static const int32_t JBOT_W1[JBOT_NF][JBOT_NH] = {")
for i in range(NF):
    vals = ", ".join(str(q16(W1[i][j])) for j in range(NH))
    out.append("    {" + vals + "},")
out.append("};")
out.append("")
out.append("static const int32_t JBOT_b1[JBOT_NH] = {")
out.append("    " + ", ".join(str(q16(b1[j])) for j in range(NH)))
out.append("};")
out.append("")
out.append("static const int32_t JBOT_W2[JBOT_NH][JBOT_NO] = {")
for j in range(NH):
    vals = ", ".join(str(q16(W2[j][k])) for k in range(NO))
    out.append("    {" + vals + "},")
out.append("};")
out.append("")
out.append("static const int32_t JBOT_b2[JBOT_NO] = {")
out.append("    " + ", ".join(str(q16(b2[k])) for k in range(NO)))
out.append("};")
out.append("")
out.append("#endif")

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
dst = os.path.join(root, "include", "kernel", "jbot_trained_weights.h")
with open(dst, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(out) + "\n")
print("wrote", dst)

report = {
    "samples": len(SAMPLES),
    "features": NF,
    "hidden": NH,
    "intents": NO,
    "epochs": EPOCHS,
    "optimizer": "Adam",
    "matching": "word-boundary",
    "train_accuracy": train_acc,
    "held_out_accuracy": test_acc,
    "max_abs_weight": max(all_w),
    "distinct_vectors": len(v2),
    "all_zero_vectors": len(zeros),
    "conflicting_vectors": len(conf),
    "seed": 20261031,
}
with open(os.path.join(here, "jbot_train_report.json"), "w",
          encoding="utf-8", newline="\n") as f:
    json.dump(report, f, indent=2)
print("wrote report")
