#!/usr/bin/env python3
"""Read-only stealth audit for a live WSM install (never writes to the game).

Checks, in order:
  1. module install : /data/adb/modules/wsm_gt tree + nonce file mode (expect 0600 root-only)
  2. game process   : maps for wsm/memfd names, environ for WSM_* leftovers,
                      open fd scan for module-dir references
  3. transport      : command/ack files exist and (when keyed) carry only E1: frames
  4. logcat         : CTL lines must stay first-token only (no full commands)

Exit code 0 = no findings, 2 = findings, 1 = tooling error.
"""
import argparse
import re
import shlex
import subprocess
import sys

MODULE_DIR = "/data/adb/modules/wsm_gt"
NONCE = MODULE_DIR + "/.nonce"
CMD = "/data/user/0/com.kakaogames.gdts/files/.7d1b0c33aa94e6f28e5b10c4d9a2f607"
ACK = "/data/user/0/com.kakaogames.gdts/files/.7d1b0c33aa94e6f28e5b10c4d9a2f608"
PKG = "com.kakaogames.gdts"
NEUTRAL = ("jit-cache", "dalvik-jit-code-cache", "jit-zygote-cache")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--adb", required=True)
    p.add_argument("--serial", required=True)
    p.add_argument("--su", default="su")
    a = p.parse_args()
    prefix = [a.adb, "-s", a.serial]

    def sh(cmd, root=False):
        if root:
            cmd = shlex.quote(a.su) + " -c " + shlex.quote(cmd)
        r = subprocess.run(prefix + ["shell", cmd], text=True, encoding="utf-8",
                           errors="replace", capture_output=True, timeout=30)
        return (r.stdout or "").strip()

    findings = []
    print("== WSM stealth audit ==")

    # 1 -- module install
    tree = sh("ls %s 2>/dev/null | head -8" % MODULE_DIR, root=True)
    print("[1] module dir: " + (tree.replace("\n", ", ") if tree else "(not found)"))
    nonce = sh("cat %s 2>/dev/null" % NONCE, root=True)
    if re.fullmatch(r"[0-9a-fA-F]{16}", nonce):
        perm = sh("stat -c '%%a %%U' %s 2>/dev/null" % NONCE, root=True)
        print("[1] nonce: present; owner/perms: %s" % perm)
        if not perm.startswith("600"):
            findings.append("nonce file is not 0600 root-only")
    else:
        print("[1] nonce: absent (module not reinstalled yet, or drop failed)")

    # 2 -- live game process
    pid = sh("pidof %s" % PKG).split()
    if not pid:
        print("[2] game not running - process checks skipped")
    else:
        pid = pid[0]
        print("[2] game pid=%s" % pid)
        raw = sh("grep -iE 'wsm|memfd:' /proc/%s/maps | sort -u | head -14" % pid, root=True)
        suspicious = [l for l in raw.splitlines()
                      if "wsm" in l.lower() or not any(n in l for n in NEUTRAL)]
        if suspicious:
            print("    map names needing review:")
            for l in suspicious[:10]:
                print("      " + l[:120])
            for l in suspicious:
                if "wsm" in l.lower():
                    findings.append("wsm string in maps: " + l.strip()[:80])
        else:
            print("    maps: clean (only neutral jit-cache class names)")
        env = sh("tr '\\0' '\\n' < /proc/%s/environ | grep WSM_" % pid, root=True)
        if env:
            print("    environ LEAK: " + env.replace("\n", " | ")[:140])
            findings.append("WSM_* present in process environ")
        else:
            print("    environ: clean (no WSM_*)")
        fds = sh("ls -l /proc/%s/fd 2>/dev/null | grep -iE 'wsm|modules/' | head -6" % pid, root=True)
        if fds:
            print("    fd references: " + fds.replace("\n", "; ")[:160])
            if "wsm" in fds.lower():
                findings.append("fd table references wsm paths")
        else:
            print("    fds: no module-dir references")

    # 3 -- transport files
    for name, path in (("cmd", CMD), ("ack", ACK)):
        body = sh("ls -la %s 2>/dev/null" % path, root=True)
        if not body:
            print("[3] %s: absent" % name)
            continue
        head = sh("head -c 24 %s 2>/dev/null" % path, root=True)
        print("[3] %s: exists; first bytes: %r" % (name, head[:24]))
        if name == "cmd" and head and not head.startswith("E1:"):
            findings.append("request frame is plaintext (not keyed)")
        if "wsm_cmd" in body or "wsm_ack" in body:
            findings.append("legacy transport name present in listing")

    # 4 -- logcat CTL hygiene
    logs = sh("logcat -d -t 400 -v brief WSM:I '*:S' 2>/dev/null | grep -E 'CTL\\[' | tail -5")
    print("[4] recent CTL lines: " + (logs.replace("\n", " || ")[:200] if logs else "(none)"))
    bad = [l for l in logs.splitlines()
           if re.search(r"@\d|feat \w+ \d|\bgm \w+ \d", l)]
    if bad:
        findings.append("CTL logcat contains full command text")

    print()
    if findings:
        print("FINDINGS (%d):" % len(findings))
        for f in findings:
            print(" - " + f)
        return 2
    print("No leaks found.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.SubprocessError) as e:
        print("audit error: " + str(e), file=sys.stderr)
        sys.exit(1)
