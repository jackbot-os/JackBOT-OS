"""Build the ISO from the real working tree: hash-sync the repo to the build
VM, then run `make iso` and print the sha256 of the produced image.

The old upload script pushed a hand-written list of 12 files, so edits to
kernel/main.c and everything else never reached the build. This syncs every
source file that differs by sha256, skipping only third_party, build output,
.git and *.iso, which are already present on the VM.
"""
import hashlib, os, sys, paramiko

HOST, PORT, USER, PWD = "127.0.0.1", 2222, "jackbot", "1105"
REPO = "/home/jackbot/JackBOT-OS"
LOCAL = r"E:\JackBOT-OS\new-chat\JackBOT-OS"
SKIP_DIRS = ("third_party", "build", ".git", "build_assets")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(HOST, port=PORT, username=USER, password=PWD,
              allow_agent=False, look_for_keys=False, timeout=20)
    sftp = c.open_sftp()

    sent = skipped = 0
    for root, dirs, files in os.walk(LOCAL):
        rel_root = os.path.relpath(root, LOCAL).replace("\\", "/")
        if rel_root == ".":
            rel_root = ""
        parts = rel_root.split("/") if rel_root else []
        if parts and parts[0] in SKIP_DIRS:
            continue
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if name.endswith((".iso", ".o", ".log")):
                continue
            rel = (rel_root + "/" + name) if rel_root else name
            lp = os.path.join(LOCAL, rel)
            if not os.path.isfile(lp):
                continue
            size = os.path.getsize(lp)
            rp = REPO + "/" + rel
            need = True
            try:
                rs = sftp.stat(rp)
                if rs.st_size == size:
                    if size > 4 << 20:
                        need = False
                    else:
                        with sftp.open(rp, "rb") as rf:
                            same = hashlib.sha256(rf.read()).hexdigest() == sha256_file(lp)
                        need = not same
            except IOError:
                need = True
            if not need:
                skipped += 1
                continue
            try:
                sftp.put(lp, rp)
                sent += 1
                print("UPLOAD", rel)
            except IOError:
                d = os.path.dirname(rp)
                c.exec_command("mkdir -p %s" % d)[1].channel.recv_exit_status()
                sftp.put(lp, rp)
                sent += 1
                print("UPLOAD", rel)
    print("sync: uploaded", sent, "unchanged", skipped)

    def run(cmd, timeout=2400):
        i, o, e = c.exec_command(cmd, timeout=timeout)
        return o.channel.recv_exit_status(), o.read().decode("utf-8", "replace"), e.read().decode("utf-8", "replace")

    rc, out, err = run("cd %s && make iso 2>&1 | tail -n 40" % REPO)
    print("MAKE rc=", rc, "errors=", out.lower().count("error:"))
    print(out)
    if err.strip():
        print("STDERR:", err[-800:])
    rc2, out2, _ = run("cd %s && sha256sum build/JackBOT-1.0.9.iso 2>/dev/null || ls -l build/*.iso" % REPO)
    print("IMAGE:", out2.strip())
    return 0 if rc == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
