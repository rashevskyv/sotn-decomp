#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The cutscene text of the owner's PSP image, matched to the US dialogue boxes.

    python3 tools/lang/disc_text.py [--psp IMAGE] [--out disks/text/disc_text.json]

IMAGE: the PSP image (.iso or .cso) of Castlevania: The Dracula X Chronicles
(default: the first PSP image in games/). The image is only read. Writes one
JSON file under disks/ (not in git: it holds game text):
  psp_en  {box key: markup}  the English PSP boxes (PSP_GAME/USRDIR/res/ps/PSPBIN/<zone>.bin)
  jp      {box key: markup}  the Japanese boxes (res/ps/hdbin/<zone>.bin, cp932)
  stock   {box key: text}    the US boxes (assets/**/*.yaml), for the glossary and the guard
A box key is "<yaml under assets>#<box>" (sotn_lang.py native_boxes). The markup is the
ua.json markup: text, \\n, {sN} SET_SPEED, {wN} SET_WAIT, {} for each other op.
A PSP box goes to a US box by its voice clip (US id = PSP id + 2, JP id = PSP id),
then by index inside the same script when every clip pair has the same index shift.
A pair whose ops (without the text ops) differ from the US box is left out.
sotn_lang.py pack makes script_psp_en.bin from psp_en. Without an image, nothing
is written and the English PSP text stays stock.
"""
import argparse
import collections
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path[:0] = [HERE, REPO]
sys.dont_write_bytecode = True
import build  # noqa: E402  Disc, identify
import sotn_lang  # noqa: E402  native_boxes, yaml_boxes, OPS, FLOW

RES = "PSP_GAME/USRDIR/res/ps/"
DEFAULT_OUT = os.path.join(sotn_lang.DATA, "disks", "text", "disc_text.json")

# The script parser of cs.py (a port of tools/sotn-assets/assets/cutscene/handler.go):
# the argument bytes of each op; 4-byte words are nibble-encoded
ARGS = [[], [], [1], [1], [], [1, 1], [], [1, 1], [], [2], [], [], [4], [], [4, 4, 4], [4], [1], [1], [],
        [4, 1], [2], [], [1], [], [1]]


def nib(b):
    return all((b[i] & 0xF) == (b[i + 1] >> 4) for i in range(len(b) - 1))


def val(b):
    v = b[0]
    for x in b[1:]:
        v = (v << 4) | (x & 0xF)
    return v


def parse(d, start, jp, limit=0x1800):
    """One script from start -> ([(offset, op code or 'TEXT', args)], end) or None."""
    i, ops, txt = start, [], None
    while i < len(d) and i - start < limit:
        op = d[i]
        if op < len(ARGS):
            txt, o, a = None, i, []
            i += 1
            if op == 14:  # SCRIPT_SWITCH: the variable, then one word per case
                if not nib(d[i:i + 4]):
                    return None
                a.append(val(d[i:i + 4]))
                i += 4
                while nib(d[i:i + 4]) and d[i:i + 4] != b"\0\0\0\0":
                    a.append(val(d[i:i + 4]))
                    i += 4
            else:
                for p in ARGS[op]:
                    if p > 1 and not nib(d[i:i + p]):
                        return None
                    a.append(val(d[i:i + p]) if p > 1 else d[i])
                    i += p
            ops.append((o, op, a))
            if op == 0:
                return ops, i
        elif jp and (0x81 <= op <= 0x9F or 0xE0 <= op <= 0xFC) and i + 1 < len(d) and d[i + 1] >= 0x40:
            if txt is None:
                txt = [i, "TEXT", [bytearray()]]
                ops.append(txt)
            txt[2][0] += d[i:i + 2]
            i += 2
        elif 0x20 <= op < 0x7F or (not jp and 0xA0 <= op <= 0xDF):
            if txt is None:
                txt = [i, "TEXT", [bytearray()]]
                ops.append(txt)
            txt[2][0].append(op)
            i += 1
        else:
            return None
    return None


def find(d, jp):
    """All scripts in a file: [ops]. ponytail: a heuristic scan (ops, text share, alphabet), as
    voice_sets/psptext/extract.py; reading the script pointers of the overlay is the upgrade."""
    import re
    best = {}
    for s in range(0, len(d) - 8):
        if d[s] >= 25:
            continue
        r = parse(d, s, jp)
        if not r:
            continue
        ops, e = r
        if e in best or e - s < 40:
            continue
        runs = [bytes(o[2][0]) for o in ops if o[1] == "TEXT"]
        tb = b"".join(runs)
        if 6 not in [o[1] for o in ops] or not {5, 7} & {o[1] for o in ops}:
            continue
        if len(tb) < 6 * len(runs) or len(tb) * 3 < e - s:
            continue
        if jp:
            if len(re.findall(rb"[\x81-\x9f\xe0-\xfc].", tb, re.S)) < 8:
                continue
        elif len(re.findall(rb"[A-Za-z ,.'!?-]", tb)) < 0.9 * len(tb) or len(re.findall(rb"[a-z]{3,}", tb)) < 2:
            continue
        best[e] = (s, e, ops)
    out = []
    for v in sorted(best.values(), key=lambda v: v[0]):
        if out and v[0] < out[-1][1]:
            continue
        out.append(v)
    return [v[2] for v in out]


def boxes_of(ops, enc):
    """A script cut into boxes after each NEXT_DIALOG: [{'markup', 'ops' (non-flow codes), 'voice'}]."""
    out, cur = [], []
    for o in ops:
        cur.append(o)
        if o[1] == 6:
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    res = []
    for box in out:
        parts, nonflow, voice = [""], [], []  # parts[k]: the markup before op k, then the tail
        for _off, op, a in box:
            if op == "TEXT":
                parts[-1] += bytes(a[0]).decode(enc, "replace")
            elif op == 1:
                parts[-1] += "\n"
            elif op in (2, 3):
                parts[-1] += "{%s%d}" % ("s" if op == 2 else "w", a[0])
            else:
                parts.append("")
                nonflow.append(op)
                if op == 9 and a[0] >= 0x300:
                    voice.append(a[0])
        res.append({"parts": parts, "ops": nonflow, "voice": voice})
    return res


MOVABLE = {11}  # SCRIPT_UNKNOWN_11 (wait for the sound, no args) stands elsewhere in the PSP boxes


def fit(q, ops):
    """The text of disc box q on the US ops of a box: markup, or None when the ops (without
    MOVABLE) differ. The text before the k-th other op stays before it."""
    if q["ops"] == ops:
        return "{}".join(q["parts"])
    fixed = [o for o in ops if o not in MOVABLE]
    if [o for o in q["ops"] if o not in MOVABLE] != fixed:
        return None
    before, c = [""] * (len(fixed) + 1), 0
    for k, o in enumerate(q["ops"]):
        before[c] += q["parts"][k]
        c += o not in MOVABLE
    before[c] += q["parts"][-1]
    mk, c = "", 0
    for o in ops:
        if o not in MOVABLE:
            mk += before[c]
            c += 1
        mk += "{}"
    return mk + before[c]


def us_scripts():
    """{yaml: [{'key', 'ops', 'voice', 'text'}]} of the US dialogue boxes."""
    out = collections.defaultdict(list)
    for key, b in sotn_lang.native_boxes().items():
        out[b["file"]].append(dict(b, key=key))
    for rel, bs in out.items():
        bs.sort(key=lambda b: int(b["key"].rsplit("#", 1)[1]))
        for b, items in zip(bs, sotn_lang.yaml_boxes(rel)):
            b["voice"] = [it[1] for it in items if it[0] == "PLAY_SOUND" and it[1] >= 0x300]
    return out


def pair(us, scripts, delta):
    """US boxes of one yaml -> {US index: disc box} from the disc script with most voice hits."""
    best = None
    for sc in scripts:
        m, taken = {}, set()
        for i, u in enumerate(us):
            for j, q in enumerate(sc):
                if j not in taken and u["voice"] and {v - delta for v in u["voice"]} & set(q["voice"]):
                    m[i] = j
                    taken.add(j)
                    break
        if m and (best is None or len(m) > len(best[1])):
            best = (sc, m)
    if not best:
        return {}
    sc, m = best
    shift = {j - i for i, j in m.items()}
    if len(shift) == 1:  # every clip pair has the same index shift: the others by index
        d = shift.pop()
        for i in range(len(us)):
            if i not in m and 0 <= i + d < len(sc) and i + d not in m.values():
                m[i] = i + d
    return {i: sc[j] for i, j in m.items()}


def zone_of(rel):
    return rel.split("/")[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--psp", help="PSP image (default: the first one in games/)")
    ap.add_argument("--out", default=os.environ.get("SOTN_LANG_DISC", DEFAULT_OUT))
    a = ap.parse_args()
    image = a.psp
    if not image:
        for p in sorted(glob.glob(os.path.join(REPO, "games", "**", "*"), recursive=True)):
            if os.path.isfile(p) and build.identify(p)[0] == "pspeu":
                image = p
                break
    if not image or build.identify(image)[0] != "pspeu":
        print("disc_text: no PSP image: the English PSP text stays stock")
        return
    disc = build.Disc(image)

    def read(rel):
        try:
            lba, n, _ = disc.find(RES + rel)
        except build.Failed:
            return None
        return disc.read(lba, n)

    us = us_scripts()
    cache, out = {}, {"image": os.path.basename(image), "id": build.identify(image)[1],
                      "psp_en": {}, "jp": {}, "stock": {}}
    stat = collections.Counter()
    for rel, bs in sorted(us.items()):
        for b in bs:
            out["stock"][b["key"]] = b["text"]
        z = zone_of(rel)
        for field, files, jp, delta in (("psp_en", ["PSPBIN/%s.bin" % z], False, 2),
                                        ("jp", ["hdbin/%s.bin" % z, "PSPBIN/%s.bin" % z], True, 2)):
            got = {}
            for f in files:
                if (f, jp) not in cache:
                    d = read(f)
                    cache[(f, jp)] = [boxes_of(ops, "cp932" if jp else "latin-1") for ops in find(d, jp)] if d else []
                got = pair(bs, cache[(f, jp)], delta)
                if got:
                    break
            for i, b in enumerate(bs):
                q = got.get(i)
                mk = q and fit(q, [sotn_lang.OP_ALIAS.get(o, o) for o in b["ops"]])
                if q is None:
                    stat[field + " no pair"] += 1
                elif mk is None:
                    stat[field + " ops differ"] += 1
                else:
                    out[field][b["key"]] = mk
                    stat[field + " ok"] += 1
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, ensure_ascii=False, indent=0, sort_keys=True)
    widest = max((len(line) for m in out["psp_en"].values() for line in sotn_lang.markup_text(m).split("\n")),
                 default=0)
    print(f"disc_text: {out['id']}: {len(out['psp_en'])} English PSP boxes, {len(out['jp'])} Japanese boxes,"
          f" {len(out['stock'])} US boxes; widest PSP line {widest} chars; {dict(sorted(stat.items()))} -> {a.out}")


if __name__ == "__main__":
    main()
