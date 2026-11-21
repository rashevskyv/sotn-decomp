#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""uk/ua_ps1.json: the Ukrainian text of the US PS1 English dialogue, for the PS1 EN voice set
(script.bin under uk). ua.json is from the Japanese script and stays with the JP voice set.

  ua_ps1.py check        fail on any box that does not pair up (exit 1)
  ua_ps1.py dump FILE    the box-by-box pairing as JSON, with the stock English (never into git)

Pairing: uk/places.json gives each Xbox script id its yaml box key. The English is the yaml box
(SOTN_DATA_ROOT/assets), the speaker is actor_names[SET_PORTRAIT arg 0] of the US branch of the
stage's cutscene source. With SOTN_LANG_XBLA the check also compares the native ops and the speaker
of the Xbox English and Japanese units with the yaml box.

A ua_ps1.json box keeps the native box: the same {} count, the same {sN} {wN} {} sequence (the
PS1 English voice timing), the same line count (one more line only where a stock line is wider
than the box), lines of at most DLG_LINE_CELLS cells and only letters of the dialogue font.
"""
import collections
import glob
import json
import os
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sotn_lang as L  # noqa: E402

UA_PS1 = os.path.join(L.UK, "ua_ps1.json")
FLOW_TOK = re.compile(r"\{[sw]?\d*\}|\n")
PAGE_OPS = {5, 11}  # SET_PORTRAIT, SCRIPT_UNKNOWN_11: a new speaker or a cleared box starts a new page
# The Japanese script has another op layout in these boxes (ua_jp.json): context only, not an error.
JA_LAYOUT = {"script 82CEA118:2", "script 82CEA118:4", "script 82D2C66C:17", "script 82D38914:4"}
# A box with no SET_PORTRAIT that a SCRIPT_SWITCH reaches (not the box before it): its speaker.
SPEAKER_FIX = {"st/sel/us/cutscene_endings.yaml#30": "Maria"}  # the branch of box 24, Maria's answer
SCENES = {
    "st/st0/cutscene_script_psx.yaml": "Prologue: Richter and Dracula",
    "st/no3/cutscene_data.yaml": "Castle gate (NO3): Death",
    "st/nz0/cutscene_data.yaml": "Alchemy Laboratory: Maria again",
    "boss/mar/cutscene_data.yaml": "Marble Gallery: Maria, first meeting",
    "st/dai/cutscene_script_psx.yaml": "Royal Chapel: Maria, the Belmont news",
    "boss/bo5/cutscene_script_psx.yaml": "Royal Chapel: Maria after the boss",
    "st/lib/cutscene_data.yaml": "Long Library: Master Librarian",
    "st/no4/cutscene_data_ferryman_left_psx.yaml": "Underground Caverns: boatman (left)",
    "st/no4/cutscene_data_ferryman_right_psx.yaml": "Underground Caverns: boatman (right)",
    "st/dre/cutscene_script_psx.yaml": "Nightmare: Lisa and Succubus",
    "st/cen/cutscene_data.yaml": "Center: Maria and the orb",
    "st/top/cutscene_data.yaml": "Castle Keep: Richter freed",
    "boss/rbo2/cutscene_script_psx.yaml": "Reverse Keep: Death",
    "st/rcen/cutscene_script_psx.yaml": "Reverse Center: Shaft",
    "boss/rbo6/cutscene_script_father.yaml": "Final battle: Dracula (father)",
    "boss/rbo6/cutscene_script_mother.yaml": "Final battle: Dracula (mother)",
    "st/sel/us/cutscene_endings.yaml": "Endings",
}


def actor_names(rel):
    """actor_names of the US branch in the cutscene source of a yaml (assets/st/no3/.. -> src/st/no3)."""
    kind, stage = rel.split("/")[:2]
    for path in sorted(glob.glob(os.path.join(L.REPO, "src", kind, stage, "*.c"))):
        m = re.search(r"const char\* actor_names\[\] = \{(.*?)\};", open(path, encoding="utf-8").read(), re.S)
        if m:
            return re.findall(r'_S\("([^"]*)"\)', m.group(1))
    raise SystemExit(f"{rel}: no actor_names in src/{kind}/{stage}")


def native_markup(items):
    out = []
    for it in items:
        n = it[0]
        if n == "TEXT":
            out.append(it[1])
        elif n == "LINE_BREAK":
            out.append("\n")
        elif n == "SET_SPEED":
            out.append("{s%d}" % it[1])
        elif n == "SET_WAIT":
            out.append("{w%d}" % it[1])
        else:
            out.append("{}")
    return "".join(out)


def yaml_info():
    """{box key: {'markup', 'speakers', 'ops'}} for every box of the yaml files of places.json."""
    places = [p for p in L.load_json(L.PLACES)["lines"] if p["kind"] == "script"]
    out = {}
    for rel in sorted({p["key"].split("#")[0] for p in places}):
        names, cur, prev = actor_names(rel), None, None
        for b, items in enumerate(L.yaml_boxes(rel)):
            who = []
            for it in items:
                if it[0] == "SET_PORTRAIT":
                    cur = names[it[1]]
                    if who[-1:] != [cur]:
                        who.append(cur)
            key = f"{rel}#{b}"
            if key in SPEAKER_FIX:
                who = [SPEAKER_FIX[key]]
            elif not who and prev == "END_CUTSCENE" and any(it[0] == "TEXT" for it in items):
                who = ["?"]  # a jump target: the box before is not the speaker
            out[key] = {"markup": native_markup(items), "speakers": who or ([cur] if cur else []),
                        "ops": [it[0] for it in items if it[0] != "TEXT" and it[0] not in L.FLOW_NAMES]}
            prev = items[-1][0]
    return places, out


def pages(markup, ops):
    """Text lines of a markup: split at \\n and at a {} that is a PAGE_OPS native op."""
    lines, cur, k = [], [], 0
    for t in L.markup_tokens(markup):
        if t[0] == "tx":
            cur.append(t[1])
        elif t[1] == 1:
            lines.append("".join(cur)); cur = []
        elif t[1] == L.NATIVE_OP:
            if k < len(ops) and ops[k] in L.OPS and L.OPS.index(ops[k]) in PAGE_OPS and "".join(cur):
                lines.append("".join(cur)); cur = []
            k += 1
    lines.append("".join(cur))
    return lines


def flow_seq(markup):
    return [t for t in FLOW_TOK.findall(markup) if t != "\n"]


def check_box(key, en, uk, ops):
    """Problems of one ua_ps1 box against its native box (en: native markup)."""
    bad = []
    _data, nochar, ok = L.enc_dialogue(uk, ops)
    if not ok:
        bad.append(f"{{}} count {uk.count('{}')} != native {en.count('{}')}")
    if nochar:
        bad.append(f"no glyph for {''.join(sorted(set(nochar)))}")
    if flow_seq(uk) != flow_seq(en):
        a, b = flow_seq(en), flow_seq(uk)
        i = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
        bad.append(f"timing differs at token {i}: native {a[i:i + 3]} uk {b[i:i + 3]}")
    over = sum(len(x) > L.DLG_LINE_CELLS for x in pages(en, ops))
    nl_en, nl_uk = en.count("\n"), uk.count("\n")
    if not nl_en <= nl_uk <= nl_en + over:
        bad.append(f"{nl_uk + 1} lines, native {nl_en + 1}")
    for i, line in enumerate(pages(uk, ops)):
        if len(line) > L.DLG_LINE_CELLS:
            bad.append(f"line {i} has {len(line)} cells > {L.DLG_LINE_CELLS}: {line}")
        if line != line.strip() and line.strip():
            bad.append(f"line {i} starts or ends with a space: {line!r}")
    return bad


def pairing(check_xbox=True):
    places, info = yaml_info()
    ua = L.load_json(os.path.join(L.UK, "ua.json"))
    units = {x["id"]: x for x in L.xbla_units()} if check_xbox and L.XBLA and os.path.isdir(L.XBLA) else {}
    errs, rows = [], []
    keys = collections.Counter(p["key"] for p in places)
    by_block = collections.defaultdict(list)
    for p in places:
        key, uid = p["key"], p["id"]
        if key not in info:
            errs.append(f"{uid}: no yaml box {key}")
            continue
        if uid not in ua:
            errs.append(f"{uid}: not in ua.json")
        by_block[L.block_of(uid)].append((int(uid.rsplit(":", 1)[1]), int(key.split("#")[1]), key))
        nat = info[key]
        if "?" in nat["speakers"]:
            errs.append(f"{uid}: {key} is a jump target with no SET_PORTRAIT: add its speaker to SPEAKER_FIX")
        row = {"id": uid, "key": key, "scene": SCENES.get(key.split("#")[0], key.split("#")[0]),
               "speakers": nat["speakers"], "en": nat["markup"], "uk_jp": ua.get(uid)}
        x = units.get(uid)
        if units:
            if x is None:
                errs.append(f"{uid}: not in lines_game.json")
            else:
                nops = [L.OPS.index(o) if o in L.OPS else o for o in nat["ops"]]
                for field in ("en", "ja") if uid not in JA_LAYOUT else ("en",):
                    if L.xbox_ops(x[field]) != nops:
                        errs.append(f"{uid}: Xbox {field} ops differ from {key}")
                    sp = [int(t[2].split()[1], 16) for t in L.xbox_tokens(x[field]) if t[0] == "op" and t[1] == 5]
                    ny = [it[1] for it in L.yaml_boxes(key.split("#")[0])[int(key.split("#")[1])] if it[0] == "SET_PORTRAIT"]
                    if sp != ny:
                        errs.append(f"{uid}: Xbox {field} speakers {sp} != yaml {ny}")
                if L.norm(L.xbox_text(x["en"])) != L.norm(L.markup_text(nat["markup"])):
                    row["xbox_en_differs"] = L.xbox_text(x["en"])
                row["ja"] = L.xbox_text(x["ja"])
        rows.append(row)
    for blk, lst in by_block.items():
        lst = sorted(set((i, b) for i, b, _k in lst))
        boxes = [b for _i, b in lst]
        if len(lst) == len({i for i, _b in lst}) and boxes != sorted(boxes):
            errs.append(f"{blk}: units and boxes in a different order {lst}")
    placed = set(keys)
    for k, b in L.native_boxes().items():
        if k not in placed and b["text"].strip(" .\n"):
            errs.append(f"{k}: a box with text and no place: {b['text']!r}")
    for k, n in keys.items():
        if n > 1:
            ids = sorted(p["id"] for p in places if p["key"] == k)
            texts = {json.dumps(ua.get(i)) for i in ids}
            if len(texts) > 1:
                errs.append(f"{k}: {ids} share the box with different ua.json text")
    return places, info, rows, errs, len(keys)


def load_ua_ps1():
    """ua_ps1.json with a duplicate key check."""
    dup = []

    def hook(pairs):
        seen = {}
        for k, v in pairs:
            if k in seen:
                dup.append(k)
            seen[k] = v
        return seen
    return json.load(open(UA_PS1, encoding="utf-8"), object_pairs_hook=hook), dup


def cmd_check():
    places, info, rows, errs, nboxes = pairing()
    ps1, dup = load_ua_ps1()
    errs += [f"ua_ps1.json: key {k} twice" for k in dup]
    ids = {p["id"] for p in places}
    errs += [f"ua_ps1.json: {k} is no script id of places.json" for k in sorted(set(ps1) - ids)]
    errs += [f"ua_ps1.json: {k} missing" for k in sorted(ids - set(ps1))]
    by_key = collections.defaultdict(set)
    soft = 0
    for p in places:
        uk = ps1.get(p["id"])
        if uk is None or p["key"] not in info:
            continue
        by_key[p["key"]].add(uk)
        nat = info[p["key"]]
        for b in check_box(p["key"], nat["markup"], uk, nat["ops"]):
            errs.append(f"{p['id']} {p['key']}: {b}")
        w = max(len(x) for x in pages(nat["markup"], nat["ops"]))
        soft += any(len(x) > max(w, 22) for x in pages(uk, nat["ops"]))
    errs += [f"{k}: two different ua_ps1.json texts" for k, v in by_key.items() if len(v) > 1]
    for e in errs:
        print("FAIL:", e)
    print(f"ua_ps1: {len(places)} places, {nboxes} boxes, {len(ps1)} ua_ps1 ids, "
          f"{len(rows)} paired, {soft} boxes with a line wider than stock and 22 cells, {len(errs)} problems")
    sys.exit(1 if errs else 0)


def cmd_dump(path):
    _places, _info, rows, errs, nboxes = pairing()
    ps1 = load_ua_ps1()[0] if os.path.exists(UA_PS1) else {}
    for r in rows:
        r["uk_ps1"] = ps1.get(r["id"])
    json.dump({"boxes": nboxes, "problems": errs, "rows": rows}, open(path, "w", encoding="utf-8"),
              ensure_ascii=False, indent=1)
    print(f"{len(rows)} rows, {nboxes} boxes, {len(errs)} pairing problems -> {path}")


def selftest():
    ops = ["SET_PORTRAIT", "NEXT_DIALOG"]
    en = "{}{s4}Ab cd.\n{w28}Ef{w48}{}"
    assert check_box("k", en, "{}{s4}Аб.\n{w28}Вг{w48}{}", ops) == []
    assert check_box("k", en, "{}{s3}Аб.\n{w28}Вг{w48}{}", ops)[0].startswith("timing")
    assert check_box("k", en, "{}{s4}Аб.\n{w28}В\nг{w48}{}", ops)[0].startswith("3 lines")
    assert check_box("k", en, "{s4}Аб.\n{w28}Вг{w48}{}", ops)[0].startswith("{} count")
    assert "cells" in check_box("k", en, "{}{s4}" + "а" * 25 + "\n{w28}Вг{w48}{}", ops)[0]
    assert pages("А{}Б\nВ", ["SET_PORTRAIT"]) == ["А", "Б", "В"]


if __name__ == "__main__":
    L.TABLE, L.TABLE_INDEX = L.rust_table()
    selftest()
    if sys.argv[1:2] == ["check"]:
        cmd_check()
    elif sys.argv[1:2] == ["dump"] and len(sys.argv) == 3:
        cmd_dump(sys.argv[2])
    else:
        sys.exit(__doc__)
