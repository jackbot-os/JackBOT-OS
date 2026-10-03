#!/usr/bin/env python3
import socket, json, sys, time

sock_path = sys.argv[1]
cmds = sys.argv[2:]

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.time() + 10
while True:
    try:
        s.connect(sock_path)
        break
    except OSError:
        if time.time() > deadline:
            raise
        time.sleep(0.2)

f = s.makefile("rw")

def recv():
    while True:
        line = f.readline()
        if not line:
            return None
        line = line.strip()
        if line:
            return json.loads(line)

hello = recv()

def send(obj):
    f.write(json.dumps(obj) + "\r\n")
    f.flush()

send({"execute": "qmp_capabilities"})
recv()

for c in cmds:
    send({"execute": "human-monitor-command",
          "arguments": {"command-line": c}})
    r = recv()
    out = r.get("return", "") if r else ""
    print("==== %s ====" % c)
    print(out)
