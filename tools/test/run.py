#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Test runner of the port (docs/TESTING.md). Plain asserts, no framework.

usage: run.py offline [--textures <dir>] [-k <text>]
  offline     level 1: needs only this repository
  --textures  a folder with uk/, en/, common/ (for example <game>/data/textures) for the HD tests
  -k          run only the tests whose name holds <text>
Levels "game" and "build" are PLANNED (docs/TESTING.md).
Each test prints PASS, FAIL or SKIP. Exit code 1 if a test fails.
"""
import glob
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
UK = os.path.join(REPO, "tools", "lang", "uk")
sys.path.insert(0, os.path.join(REPO, "tools", "lang"))
sys.dont_write_bytecode = True

DLG_LINE_CELLS = 24  # sotn_lang.py DLG_LINE_CELLS: the dialogue line sprite is 192 px wide
# ua.json boxes with no place in places.json: the boss boxes of bo2 and bo6, still stock (open).
# A new box without a place must fail, so the list is pinned.
UNPLACED = {
    "script 82CD15B8:0", "script 82CD15B8:1", "script 82CD15B8:2", "script 82CD15B8:3",
    "script 82CD15D0:0", "script 82CD15D0:1", "script 82CD15D0:3", "script 82CD15E8:0",
    "script 82D326B4:0", "script 82D326B4:1", "script 82D326B4:2", "script 82D326B4:3",
    "script 82D326CC:0",
}
# the HD loader name (tools/lang/FORMAT.md, "HD pictures")
HD_NAME = re.compile(r"^(?P<free>.+?)__(?P<rect>\d+_\d+_\d+x\d+_(?:4|8|16))_(?P<hash>[0-9a-f]{8})"
                     r"(?:_c(?P<clut>[0-9a-f]{8}))?\.png$")
HD_MAX = 2048
HD_PACK_DIRS = {"tools/lang/textures/uk", "tools/lang/textures/en", "tools/lang/textures/common"}
NO_GIT_EXT = (".pcm", ".mp3", ".flac", ".at3", ".at9", ".wav", ".xa")
NO_GIT_NAMES = ("art.bin", "ps1vram_uk.bin")


class Skip(Exception):
    pass


def load(name):
    with open(os.path.join(UK, name), encoding="utf-8") as f:
        return json.load(f)


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=300, **kw)


# ---------------------------------------------------------------- level 1

def t_lang_selftest(_o):
    r = sh([sys.executable, os.path.join(REPO, "tools", "lang", "sotn_lang.py"), "selftest"])
    assert r.returncode == 0 and "selftest ok" in r.stdout, (r.stdout + r.stderr)[-800:]


def t_wide_maths(_o):
    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        raise Skip("no C compiler")
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "wide_test")
        r = sh([cc, "-o", exe, os.path.join(REPO, "src", "pc", "wide_test.c")])
        assert r.returncode == 0, "compile: " + r.stderr[-800:]
        r = sh([exe])
        assert r.returncode == 0, (r.stdout + r.stderr)[-800:]


def box_lines(markup):
    """Text lines of a box: split at \\n and at a run of two or more native ops (a new speaker).
    ponytail: a guess at the op kinds; the exact check is sotn_lang.py width (needs the owner's sources)."""
    from sotn_lang import markup_text
    out = []
    for part in re.split(r"(?:\{\}){2,}", re.sub(r"\{[sw]\d+\}", "", markup)):
        out += markup_text(part).split("\n")
    return out


def t_script_tables(_o):
    notes = []
    files = ["ua.json", "ua_psp.json", "ua_menu.json", "ua_port.json", "ua_zones.json", "ua_code.json", "places.json"]
    data = {f: load(f) for f in files}  # each file parses
    ua, psp = data["ua.json"], data["ua_psp.json"]
    places = {x["id"] for x in data["places.json"]["lines"] if x["kind"] == "script"}
    boxes = {k for k in ua if k.startswith("script ")}
    unplaced = boxes - places
    assert unplaced <= UNPLACED, "box without a place in places.json: %s" % sorted(unplaced - UNPLACED)[:10]
    notes.append(f"{len(boxes)} boxes, {len(unplaced)} known without a place")
    over = []
    for name, table in (("ua.json", ua), ("ua_psp.json", psp)):
        for k, v in table.items():
            if k.startswith("script "):
                over += [f"{name} {k} line {i}: {len(l)} cells" for i, l in enumerate(box_lines(v))
                         if len(l) > DLG_LINE_CELLS]
    assert not over, "lines over %d cells: %s" % (DLG_LINE_CELLS, over[:10])
    overrides = [("ua_psp.json", psp)]
    jp = os.path.join(UK, "ua_jp.json")
    if os.path.exists(jp):
        overrides.append(("ua_jp.json", load("ua_jp.json")))
    else:
        notes.append("no ua_jp.json (PLANNED): its part skipped")
    for name, table in overrides:
        assert set(table) <= boxes, f"{name} has boxes not in ua.json: {sorted(set(table) - boxes)[:10]}"
        same = [k for k in table if table[k] == ua[k]]
        assert not same, f"{name} repeats ua.json: {same[:10]}"
    en_jp = os.path.join(REPO, "tools", "lang", "en", "en_jp.json")
    if os.path.exists(en_jp):
        with open(en_jp, encoding="utf-8") as f:
            en = json.load(f)
        missing = sorted(boxes - set(en))
        assert not missing, f"en_jp.json misses {len(missing)} boxes: {missing[:10]}"
    else:
        notes.append("no tools/lang/en/en_jp.json (PLANNED): its part skipped")
    return "; ".join(notes)


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    assert head[:8] == b"\x89PNG\r\n\x1a\n" and head[12:16] == b"IHDR", f"not a PNG: {path}"
    return struct.unpack(">II", head[16:24])


def pack_dirs(o):
    root = o.get("textures")
    if not root:
        raise Skip("no --textures folder (the HD packs are local)")
    dirs = {d: os.path.join(root, d) for d in ("uk", "en", "common") if os.path.isdir(os.path.join(root, d))}
    if not dirs:
        raise Skip(f"no uk/, en/ or common/ in {root}")
    return dirs


def t_hd_names(o):
    bad, n = [], 0
    for d, path in pack_dirs(o).items():
        for p in sorted(glob.glob(os.path.join(path, "*.png"))):
            n += 1
            name = os.path.basename(p)
            if not HD_NAME.match(name):
                bad.append(f"{d}/{name}: name does not match the loader pattern")
                continue
            w, h = png_size(p)
            if w > HD_MAX or h > HD_MAX:
                bad.append(f"{d}/{name}: {w}x{h} is larger than {HD_MAX}")
    assert not bad, "; ".join(bad[:20])
    return f"{n} files"


def read_list(name):
    """{first word: rest of the line} of a tools/test list file; # starts a comment."""
    out = {}
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                word, _, rest = line.partition(" ")
                out[word] = rest.strip()
    return out


def t_hd_parity(o):
    dirs = pack_dirs(o)
    if "uk" not in dirs or "en" not in dirs:
        raise Skip("needs both uk/ and en/")
    exc = read_list("hd_parity_exceptions.txt")
    assert all(exc.values()), "an exception has no reason: %s" % [k for k, v in exc.items() if not v]
    names = {}
    for d, path in dirs.items():
        names[d] = [m for m in (HD_NAME.match(os.path.basename(p)) for p in glob.glob(os.path.join(path, "*.png"))) if m]
    pair = {d: {(m["free"], m["rect"], m["clut"]) for m in names[d]} for d in ("uk", "en")}
    bad = []
    title = {"uk": "titlebg_uk", "en": "titlebg_en"}
    for a, b in (("uk", "en"), ("en", "uk")):
        for free, rect, clut in sorted(pair[a], key=lambda k: (k[0], k[1], k[2] or "")):
            counterpart = title[b] if free == title[a] else free
            if (counterpart, rect, clut) not in pair[b] and free not in exc:
                bad.append(f"{a}/{free}__{rect} c{clut}: no {b} file")
    key = {d: {(m["rect"], m["hash"], m["clut"]) for m in names[d]} for d in dirs}
    for d in ("uk", "en"):
        both = key[d] & key.get("common", set())
        bad += [f"{d} and common/ both hold {k}" for k in sorted(both)]
    assert not bad, "; ".join(bad[:20])
    return f"uk {len(names['uk'])}, en {len(names['en'])}, common {len(names.get('common', []))} files"


def git_blob_sha1(data):
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data, usedforsecurity=False).hexdigest()


def is_unapproved_stock(repo, path, index_blob, approved):
    lower = path.lower()
    parent, _, name = lower.rpartition("/")
    if name.endswith(NO_GIT_EXT) or name in NO_GIT_NAMES or (
        lower.startswith("tools/lang/") and name.endswith(".bin")
    ):
        return True

    hdt = name.endswith(".hdt")
    hd_name = HD_NAME.fullmatch(name[:-4] + ".png" if hdt else name)
    loose_png = name.endswith(".png") and (
        lower.startswith(("tools/lang/textures/", "data/textures/"))
        or any(part in ("dump", "play") or part.startswith("_bak_") for part in lower.split("/"))
    )
    if not (hdt or hd_name or loose_png):
        return False

    expected = approved.get(path)
    if parent not in HD_PACK_DIRS or name.startswith("_bak_") or not hd_name:
        return True
    if not expected or index_blob != expected:
        return True

    file = repo / path
    return file.is_symlink() or not file.is_file() or git_blob_sha1(file.read_bytes()) != expected


def t_stock_guard_selftest(_o):
    path = "tools/lang/textures/uk/sample__0_0_1x1_4_12345678.png"
    data = b"approved example"
    blob = git_blob_sha1(data)
    approved = {path: blob}
    with tempfile.TemporaryDirectory() as tmp:
        repo = Path(tmp)
        fixture = repo / path
        fixture.parent.mkdir(parents=True)
        fixture.write_bytes(data)
        assert not is_unapproved_stock(repo, path, blob, approved), "approved image rejected"
        other = path.replace("sample__", "other__")
        (repo / other).write_bytes(data)
        assert is_unapproved_stock(repo, other, blob, approved), "unlisted image accepted"
        for folder in ("dump", "play", "_bak_textures"):
            assert is_unapproved_stock(repo, f"{folder}/image.png", blob, approved), folder
        assert is_unapproved_stock(repo, path, git_blob_sha1(b"changed"), approved), "changed index accepted"
        fixture.write_bytes(b"changed")
        assert is_unapproved_stock(repo, path, blob, approved), "changed file accepted"
    return "approved image passes, unlisted path and changed bytes fail"


def t_no_stock_in_git(_o):
    approved = read_list("hd_approved.txt")
    repo = Path(REPO)
    r = sh(["git", "-C", REPO, "ls-files", "--stage", "-z"], encoding="utf-8")
    assert r.returncode == 0, r.stderr
    bad = []
    for entry in r.stdout.split("\0")[:-1]:
        metadata, path = entry.split("\t", 1)
        mode, blob, stage = metadata.split()
        index_blob = blob if mode in ("100644", "100755") and stage == "0" else None
        if is_unapproved_stock(repo, path, index_blob, approved):
            bad.append(path)
    assert not bad, f"tracked stock files: {bad[:20]}"
    if not os.environ.get("SOTN_LANG_XBLA"):
        return "tracked files clean; guard skipped (SOTN_LANG_XBLA not set)"
    r = sh([sys.executable, os.path.join(REPO, "tools", "lang", "sotn_lang.py"), "guard"])
    assert r.returncode == 0, (r.stdout + r.stderr)[-1500:]
    return "tracked files clean; " + r.stdout.strip().splitlines()[-1]


OFFLINE = [t_lang_selftest, t_wide_maths, t_script_tables, t_hd_names, t_hd_parity,
           t_stock_guard_selftest, t_no_stock_in_git]


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.exit(__doc__)
    levels = [a for a in argv if a in ("offline", "game", "build")]
    o = {"textures": None, "k": ""}
    if "--textures" in argv:
        o["textures"] = argv[argv.index("--textures") + 1]
    if "-k" in argv:
        o["k"] = argv[argv.index("-k") + 1]
    if not levels:
        sys.exit(__doc__)
    planned = [lv for lv in levels if lv != "offline"]
    if planned:
        print("level", " and ".join(planned), "is PLANNED: no tests yet (docs/TESTING.md)")
        sys.exit(2)
    fails = 0
    for t in OFFLINE:
        name = t.__name__[2:]
        if o["k"] not in name:
            continue
        try:
            note = t(o)
            print(f"PASS {name}" + (f": {note}" if note else ""))
        except Skip as e:
            print(f"SKIP {name}: {e}")
        except AssertionError as e:
            fails += 1
            print(f"FAIL {name}: {e}")
        except Exception as e:  # a broken test is a failure too
            fails += 1
            print(f"FAIL {name}: {type(e).__name__}: {e}")
    print("failed:", fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main(sys.argv[1:])
