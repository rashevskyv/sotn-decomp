#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Release checks of docs/TESTING.md (rows 2, 3, 4, 5, 7, 8) on the built files. Reads only.

usage: release_check.py [--version V] [--release <dir>] <sotn.exe> <sotn.nro> ...
  --version  the expected version (default: SOTN_VERSION of CMakeLists.txt)
  --release  the release folder: it must hold no stock data
A file is a Windows exe (MZ) or a Switch NRO (NRO0); others are refused.
Prints PASS, FAIL or SKIP per check and the SHA-1 of each file. Exit code 1 if a check fails.
"""
import hashlib
import mmap
import os
import re
import shutil
import struct
import subprocess
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
BAD_DLLS = ("libgcc", "libwinpthread", "libstdc++")
STOCK_DIRS = ("data", "voice", "music", "lang/en")
STOCK_EXT = (".pcm", ".at3", ".mp3", ".flac", ".xa", ".iso", ".cso", ".cue", ".bin")  # .bin: disc data, art.bin
HD_NAME = re.compile(r"__\d+_\d+_\d+x\d+_(?:4|8|16)_[0-9a-f]{8}(?:_c[0-9a-f]{8})?\.png$")
fails = 0


def say(ok, name, msg=""):
    global fails
    word = {True: "PASS", False: "FAIL", None: "SKIP"}[ok]
    fails += ok is False
    print(f"{word} {name}" + (f": {msg}" if msg else ""))


def cmake_version():
    with open(os.path.join(REPO, "CMakeLists.txt"), encoding="utf-8") as f:
        m = re.search(r'set\(SOTN_VERSION "([^"]+)"\)', f.read())
    return m.group(1) if m else None


def around(m, at):
    """The printable string that holds offset at (as strings -a shows it)."""
    lo, hi = at, at
    while lo > 0 and 0x20 <= m[lo - 1] < 0x7F:
        lo -= 1
    while hi < len(m) and 0x20 <= m[hi] < 0x7F:
        hi += 1
    return m[lo:hi].decode("ascii")


def hits(m, pattern):
    seen = []
    for h in re.finditer(pattern, m):
        s = around(m, h.start())
        if s not in seen:
            seen.append(s)
    return seen


def versions(m):
    """Every NUL-ended "0.8.x" literal: the version strings in the file."""
    return sorted({v.decode() for v in re.findall(rb"\x00(\d+\.\d+\.\d+)\x00", m)})


def nacp_version(m):
    size = struct.unpack_from("<I", m, 0x18)[0]  # NRO header: the NRO size, then the asset section
    if m[size:size + 4] != b"ASET":
        return None
    off, length = struct.unpack_from("<QQ", m, size + 0x18)
    nacp = m[size + off:size + off + length]
    return nacp[0x3060:0x3070].split(b"\0")[0].decode() if length >= 0x3070 else None


def pe_subsystem(m):
    pe = struct.unpack_from("<I", m, 0x3C)[0]
    assert m[pe:pe + 4] == b"PE\0\0", "no PE header"
    return struct.unpack_from("<H", m, pe + 24 + 68)[0]  # optional header, Subsystem


def dll_names(path):
    tool = shutil.which("x86_64-w64-mingw32-objdump") or shutil.which("objdump")
    if not tool:
        return None
    r = subprocess.run([tool, "-p", path], capture_output=True, text=True, timeout=600)
    return sorted({l.split("DLL Name:")[1].strip() for l in r.stdout.splitlines() if "DLL Name:" in l})


def check_file(path, want):
    with open(path, "rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as m:
        kind = "exe" if m[:2] == b"MZ" else "nro" if m[0x10:0x14] == b"NRO0" else None
        name = os.path.basename(path)
        if not kind:
            say(False, name, "not a Windows exe or a Switch NRO")
            return
        found = versions(m)
        has = want in found and m.find(b"[boot] sotn-native %s") != -1
        say(has, f"{name} version", f"want {want}, the file holds {found or 'none'}")
        if kind == "nro":
            nv = nacp_version(m)
            say(nv == want, f"{name} NACP version", f"{nv}")
        home = hits(m, rb"/home/")
        say(not home, f"{name} no /home/ strings", f"{len(home)}: {home[:5]}" if home else "0")
        if kind == "nro":
            rumble = hits(m, rb"(?i)rumble")
            say(bool(rumble), f"{name} rumble strings", f"{len(rumble)}: {rumble[:6]}")
        if kind == "exe":
            sub = pe_subsystem(m)
            say(sub == 2, f"{name} Windows GUI subsystem", f"subsystem {sub} (2 = GUI, 3 = console)")
    if kind == "exe":
        dlls = dll_names(path)
        if dlls is None:
            say(None, f"{name} DLL list", "no objdump")
        else:
            bad = [d for d in dlls if d.lower().startswith(BAD_DLLS)]
            say(not bad, f"{name} DLL list", ", ".join(dlls))
    sha = hashlib.sha1()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            sha.update(block)
    print(f"SHA-1 {sha.hexdigest()}  {name}")


def check_release(root):
    bad = [d + "/" for d in STOCK_DIRS if os.path.isdir(os.path.join(root, d))]
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            rel = os.path.relpath(os.path.join(dirpath, f), root)
            if f.lower().endswith(STOCK_EXT) or HD_NAME.search(f):
                bad.append(rel)
    say(not bad, "release folder holds no stock data", f"{len(bad)}: {bad[:10]}" if bad else root)


def main(argv):
    want = argv[argv.index("--version") + 1] if "--version" in argv else cmake_version()
    release = argv[argv.index("--release") + 1] if "--release" in argv else None
    skip = {argv.index(a) + 1 for a in ("--version", "--release") if a in argv}
    files = [a for i, a in enumerate(argv) if not a.startswith("--") and i not in skip]
    if not files and not release:
        sys.exit(__doc__)
    print("version wanted:", want)
    for path in files:
        check_file(path, want)
    if release:
        check_release(release)
    print("failed:", fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main(sys.argv[1:])
