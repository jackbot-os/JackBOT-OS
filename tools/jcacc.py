#!/usr/bin/env python3
"""jcacc - compile C/C++ source into a JackBOT .jca native payload.

Pipeline (runs on the Debian build VM):
    hello.c --chibicc--> hello.s --as--> hello.o
            --ld -Ttext=0x8000000000 -e _start -static --no-relax--> hello.elf
            --objcopy -O binary--> hello.bin --JCAN header--> hello.jca

With --libc the program links against the freestanding libc in user/libc
(crt0, string, stdlib, unistd, stdio and the syscall stub), so plain C can
use printf/fopen/malloc and the POSIX syscalls implemented in kernel/posix.c.

C++ note: a .cpp/.cc/.cxx file is compiled by the host g++ in freestanding
mode; the same libc and loader format apply.

Usage:
    python jcacc.py hello.c -o HELLO.JCA [--libc]
"""
import argparse
import os
import posixpath
import sys

try:
    import paramiko
except ImportError:  # pragma: no cover
    print("paramiko is required")
    sys.exit(2)

REMOTE_ROOT = "/home/jackbot/JackBOT-OS"
CHIBICC = REMOTE_ROOT + "/third_party/chibicc/chibicc"
WORK = "/home/jackbot/jcacc"
MAGIC = b"JCAN"

LIBC_C = ["crt0", "string", "stdlib", "unistd", "stdio"]
LIBC_ASM = ["sys"]


def connect(host, port, user, password):
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(host, port=port, username=user, password=password, timeout=20,
              banner_timeout=60, auth_timeout=60, look_for_keys=False,
              allow_agent=False)
    return c


def run(c, cmd, timeout=300):
    i, o, e = c.exec_command(cmd, timeout=timeout)
    out = o.read().decode("utf-8", "replace")
    err = e.read().decode("utf-8", "replace")
    rc = o.channel.recv_exit_status()
    return rc, out, err


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("-o", "--output", default=None)
    ap.add_argument("--libc", action="store_true",
                    help="link the freestanding libc from user/libc")
    ap.add_argument("--host", default="127.0.0.1:2222")
    ap.add_argument("--user", default="jackbot")
    ap.add_argument("--password", default="1105")
    args = ap.parse_args()

    host, _, port = args.host.partition(":")
    port = int(port or 2222)
    src = os.path.abspath(args.source)
    base = os.path.splitext(os.path.basename(src))[0]
    out_local = args.output or (base + ".jca")
    ext = os.path.splitext(src)[1].lower()

    if ext in (".cpp", ".cc", ".cxx", ".c++"):
        compiler = "g++"
        flags = (" -x c++ -std=c++17 -O1 -ffreestanding -fno-exceptions "
                 "-fno-rtti -fno-threadsafe-statics -fno-stack-protector "
                 "-fno-use-cxa-atexit -mno-red-zone -fno-pic -fno-pie "
                 "-mcmodel=large")
    else:
        compiler = CHIBICC
        flags = ""
        ext = ".c"

    here = os.path.dirname(os.path.abspath(__file__))
    libc_dir = os.path.join(here, "..", "user", "libc")

    c = connect(host, port, args.user, args.password)
    run(c, "mkdir -p " + WORK)
    s = c.open_sftp()
    s.put(src, posixpath.join(WORK, base + ext))
    if args.libc:
        s.put(os.path.abspath(os.path.join(libc_dir, "jb.h")),
              posixpath.join(WORK, "jb.h"))
        for name in LIBC_C:
            s.put(os.path.abspath(os.path.join(libc_dir, name + ".c")),
                  "%s/user/libc/%s.c" % (REMOTE_ROOT, name))
        for name in LIBC_ASM:
            s.put(os.path.abspath(os.path.join(libc_dir, name + ".s")),
                  "%s/user/libc/%s.s" % (REMOTE_ROOT, name))
    else:
        stub = ("    .text\n"
                "    .globl sys_putc\n"
                "sys_putc:\n"
                "    movl $0, %eax\n"
                "    int $0x80\n"
                "    ret\n"
                "    .globl sys_exit\n"
                "sys_exit:\n"
                "    movl $2, %eax\n"
                "    int $0x80\n"
                "    ret\n")
        with s.open(posixpath.join(WORK, "stub.s"), "w") as f:
            f.write(stub)
    s.close()

    steps = ["cd " + WORK, "set -e"]
    steps.append("%s %s -S %s%s -o %s.s" % (compiler, flags, base, ext, base))
    steps.append("as %s.s -o %s.o" % (base, base))
    if args.libc:
        for name in LIBC_C:
            steps.append("%s %s -S %s/user/libc/%s.c -o %s.s"
                         % (compiler, flags, REMOTE_ROOT, name, name))
            steps.append("as %s.s -o %s.o" % (name, name))
        for name in LIBC_ASM:
            steps.append("as %s/user/libc/%s.s -o %s.o"
                         % (REMOTE_ROOT, name, name))
        # crt0 must come first: the JCAN loader enters at offset 0, so the
        # _start stub has to be the first thing in the image.
        objs = " ".join(["crt0.o", "%s.o" % base] +
                        ["%s.o" % n for n in LIBC_C if n != "crt0"] +
                        ["%s.o" % n for n in LIBC_ASM])
    else:
        steps.append("as stub.s -o stub.o")
        objs = "%s.o stub.o" % base
    steps.append("ld -static --no-relax -Ttext=0x8000000000 -e _start "
                 "-o %s.elf %s" % (base, objs))
    steps.append("objcopy -O binary %s.elf %s.bin" % (base, base))
    steps.append("ls -l %s.bin" % base)

    rc, out, err = run(c, " && ".join(steps))
    if rc != 0:
        print("compile failed:\n" + out + err)
        return 1

    s = c.open_sftp()
    with s.open(posixpath.join(WORK, base + ".bin"), "rb") as f:
        blob = f.read()
    s.close()
    c.close()

    hdr = MAGIC + len(blob).to_bytes(4, "little") + (0).to_bytes(4, "little")
    with open(out_local, "wb") as f:
        f.write(hdr + blob)
    print("wrote %s (%d bytes payload, %d bytes total)"
          % (out_local, len(blob), len(blob) + len(hdr)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

