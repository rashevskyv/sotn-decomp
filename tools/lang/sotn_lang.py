#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Ukrainian language pack tools for the native port (stage 1: data only).

The git tree keeps only the Ukrainian side: tools/lang/uk/*.json ({id: ua}), the 8x8 cells, the
private 12x16 glyphs and the owner's label pictures. The English game text and every stock byte stay
local: the pack builder joins the two at build time from the game data and the translation sources.

Subcommands (see tools/lang/FORMAT.md):
  extract  translation JSON -> tools/lang/uk/ua*.json
  glyphs   Xbox 360 mod art -> tools/lang/uk/font8.png, font8_map.json, bios_glyphs_uk.h
  join     match every Ukrainian line to its native source; print counts
  width    flag lines too wide for their native box
  art      prove where the 28 VRAM art records go natively -> tools/lang/uk/art_map.json
  pack     write the runtime pack (text.bin, dialogue*.bin, font8.bin, bios.bin, art.bin)
  vendor   the owner's sources -> tools/lang/uk/places.json, glyphs.json, dfont.png, bios12.png, art/
  guard    fail if a staged file holds English/Japanese game text or Xbox script addresses

pack reads only this repo and the extracted US disc: SOTN_DATA_ROOT (a tree with assets/, disks/us/
and tools/psyz checked out; default: this repo). One input is optional: SOTN_LANG_MEDIA, a folder
with the Xbox 360 mod's ps1vram_uk.bin. Only it has the labels that keep stock pixels (STOCK_PIXELS);
without it those labels stay English.
The owner's tools (extract, glyphs, join, width, art, vendor, guard) also read SOTN_LANG_XBLA
(lines_game.json, lines_psp.json) and SOTN_LANG_PICORIPI (the castlevania_sotn_xbla plugin folder).
"""
import difflib
import glob
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import unicodedata

sys.dont_write_bytecode = True  # we import modules from the Picoripi tree; leave no .pyc there

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
UK = os.path.join(HERE, "uk")
DATA = os.environ.get("SOTN_DATA_ROOT", REPO)
XBLA = os.environ.get("SOTN_LANG_XBLA", "")
PICO = os.environ.get("SOTN_LANG_PICORIPI", "")
MEDIA = os.environ.get("SOTN_LANG_MEDIA", "")
OUT = os.environ.get("SOTN_LANG_OUT") or os.path.join(REPO, "build", "lang", "uk")
EN = os.path.join(HERE, "en")  # en_jp.json: our English text from the Japanese script
# the PSP text of the owner's image (disc_text.py; not in git)
DISC_TEXT = os.environ.get("SOTN_LANG_DISC") or os.path.join(DATA, "disks", "text", "disc_text.json")
PLACES = os.path.join(UK, "places.json")  # join_sources() without the English text (cmd_vendor)
GLYPHS = os.path.join(UK, "glyphs.json")  # the char order of dfont.png and bios12.png
KEEP = 16  # art/<name>.png: the palette index "keep the stock texel"

# ---------------------------------------------------------------- shared tables

# The Xbox mod's 8x8 cell order and its Latin look-alikes (the letters drawn with the Latin cell).
UA_CELLS = "АБВГҐДЕЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЬЮЯабвгґдеєжзиіїйклмнопрстуфхцчшщьюя«»—’…"
HOMOGLYPHS = dict(zip("АВЕКМНОРСТХІаеорсухі", "ABEKMHOPCTXIaeopcyxi"))
HOMOGLYPHS["З"] = "3"  # the mod's З cell is pixel-equal to the stock digit 3: reuse the stock cell
FIRST_UK_CELL = 0x66  # native kana cells from ヲ on
UK_CODE = {}
for _ch in UA_CELLS:
    if _ch not in HOMOGLYPHS:
        UK_CODE[_ch] = FIRST_UK_CELL + len(UK_CODE)
SEL_FIRST = 0x60  # the mod's cells start at code 0x60 of the sheet
# The pause menu draws its letters with the serif cells (mfont.bin), but keeps the stock digits:
# there З cannot share the digit 3 cell. Strings of these tables write З as its own cell, which
# font8.bin fills with the stock 3 for the other 8x8 drawers (the item pick-up line).
Z_CELL = 0x98
NO_CELL = 0x99  # the № of ua_port.json: a kana cell no uk string takes (serif_latin.py CELL)
MENU_TABLES = ("MENU", "EQUIP_NAME", "ACC_NAME")
# g_MenuStr rows of the status page that the port writes in sentence case (PORT 100 + row):
# EXP NEXT GOLD LEVEL TIME ROOMS KILLS HEART STATUS, the status words GOOD POISON STONE CURSE alucart
STATUS_ROWS = (5, 6, 7, 8, 9, 10, 11, 14, 15, 37, 38, 39, 40, 45)
DLG_LINE_CELLS = 24  # the dialogue line sprite is 192 px wide

# 12x16 letters with no Shift-JIS code: private codes that make Krom2RawAdd fail over to g_CustomGlyphs.
PRIVATE = "ҐЄЇґєїІі"  # order of the private cells in bios_glyphs_uk.bin
CUSTOM = {ch: 0x8540 + i for i, ch in enumerate("ҐЄЇґєї")}
DROP_SERIF = "ЁЪЫЭёъыэ"  # not Ukrainian, and bios_font.DESIGN has no design for them
BIOS_FALLBACK = {"«": "≪", "»": "≫", "—": "―", "–": "－", " ": " "}

OPS = ["END_CUTSCENE", "LINE_BREAK", "SET_SPEED", "SET_WAIT", "HIDE_DIALOG", "SET_PORTRAIT", "NEXT_DIALOG",
       "SET_POS", "CLOSE_DIALOG", "PLAY_SOUND", "WAIT_FOR_SOUND", "SCRIPT_UNKNOWN_11", "SET_EVENTS",
       "SCRIPT_UNKNOWN_13", "SCRIPT_SWITCH", "SCRIPT_UNKNOWN_15", "WAIT_FOR_FLAG", "SET_FLAG",
       "SCRIPT_UNKNOWN_18", "LOAD_PORTRAIT", "SCRIPT_UNKNOWN_20", "SCRIPT_UNKNOWN_21", "RESET_FLAG",
       "SCRIPT_UNKNOWN_23", "WAIT_FOR_FLAG_RESET"]
FLOW = {1, 2, 3}
XBOX_ONLY_OPS = {0x19}  # the Xbox scripts carry {19} after a branch; the native yaml has no such op
NATIVE_OP = 0x1F  # pack placeholder: "the next native non-flow op of this box"

# Native string tables (pack table ids).
TABLES = ["MENU", "EQUIP_NAME", "EQUIP_DESC", "ACC_NAME", "ACC_DESC", "ENEMY", "SPELL_NAME", "SPELL_DESC",
          "RELIC_NAME", "RELIC_DESC", "HELP", "STATUS", "SEL_AREA", "ACTOR", "BESTIARY", "OTHER_CELL",
          "OTHER_SJIS", "ZONE", "PORT"]
FONT = {"cell": 0, "sjis": 1, "actor": 2, "alt": 3}
# Xbox block -> native tables to look in first.
BLOCK_TABLES = {
    "menu words": ["MENU", "OTHER_CELL"], "item names": ["EQUIP_NAME"], "item descriptions": ["EQUIP_DESC"],
    "equip names": ["ACC_NAME"], "equip descriptions": ["ACC_DESC"], "enemy names": ["ENEMY"],
    "tactics": ["ENEMY", "OTHER_CELL"], "relics": ["RELIC_NAME", "RELIC_DESC"], "bestiary": ["BESTIARY"],
    "spell names": ["SPELL_NAME"], "spell descriptions": ["SPELL_DESC"], "system help": ["HELP"],
    "name entry labels": ["OTHER_CELL"], "speaker names": ["ACTOR", "OTHER_CELL"], "status labels": ["STATUS"],
    "area names": ["SEL_AREA"],
}


def fnv(data, h=2166136261):
    for c in data:
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


def has_cyrillic(s):
    return any("Ѐ" <= c <= "ӿ" for c in s)


def need(path, what):
    if not path or not os.path.exists(path):
        sys.exit(f"missing {what}: {path or '(variable not set)'}")
    return path


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def save_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, ensure_ascii=False, indent=1, sort_keys=True)
        f.write("\n")


def load_json_opt(path):
    return load_json(path) if os.path.exists(path) else {}


def xbla_units():
    return load_json(need(os.path.join(XBLA, "lines_game.json"), "lines_game.json"))


def xbla_psp_units():
    return load_json(need(os.path.join(XBLA, "lines_psp.json"), "lines_psp.json"))


def block_of(uid):
    return uid.rsplit(":", 1)[0]


# ---------------------------------------------------------------- dialogue markup

TAG = re.compile(r"\{([^}]*)\}|\n|[^{\n]+")


def xbox_tokens(s):
    """('tx', text) / ('op', code, tag) tokens of an Xbox script string."""
    out = []
    for m in TAG.finditer(s):
        t = m.group(0)
        if t == "\n":
            out.append(("op", 1, ""))
        elif t.startswith("{"):
            parts = m.group(1).split()
            out.append(("op", int(parts[0], 16), m.group(1)))
        else:
            out.append(("tx", t))
    return out


def to_markup(s):
    """Xbox script string -> git markup: text, \\n, {sN} speed, {wN} wait, {} = the next native op.
    No addresses, sound ids or other Xbox bytes survive."""
    out = []
    for t in xbox_tokens(s):
        if t[0] == "tx":
            out.append(t[1].replace("{", "(").replace("}", ")"))
        elif t[1] == 1:
            out.append("\n")
        elif t[1] in (2, 3):
            arg = int(t[2].split()[1], 16)
            out.append("{%s%d}" % ("s" if t[1] == 2 else "w", arg))
        elif t[1] in XBOX_ONLY_OPS:
            continue
        else:
            out.append("{}")
    return "".join(out)


MARK = re.compile(r"\{([sw]?)(\d*)\}|\n|[^{\n]+")


def markup_tokens(s):
    out = []
    for m in MARK.finditer(s):
        t = m.group(0)
        if t == "\n":
            out.append(("op", 1))
        elif t.startswith("{"):
            out.append(("op", {"s": 2, "w": 3, "": NATIVE_OP}[m.group(1)], int(m.group(2) or 0)))
        else:
            out.append(("tx", t))
    return out


def markup_text(s):
    return "".join("\n" if t == ("op", 1) else t[1] if t[0] == "tx" else "" for t in markup_tokens(s))


# ---------------------------------------------------------------- extract

def cmd_extract(_args):
    units = xbla_units()
    ua, odd = {}, []
    for x in units:
        s = x.get("ua") or ""
        if not has_cyrillic(s):
            continue
        if x["kind"] == "script":
            ua[x["id"]] = to_markup(s)
        else:
            if "{" in s:
                odd.append(x["id"])
            ua[x["id"]] = s
    psp = {}
    for x in xbla_psp_units():
        if x["kind"] == "script" and x.get("psp_status") == "differs" and has_cyrillic(x.get("ua") or ""):
            m = to_markup(x["ua"])
            if m != ua.get(x["id"]):
                psp[x["id"]] = m
    code = {}
    src = os.path.join(need(PICO, "Picoripi plugin"), "ps1_code.py")
    current = None
    for line in open(src, encoding="utf-8"):
        d = re.match(r"^(\w+): Dict\[int, str\] = \{", line)
        if d:
            current = d.group(1)
            continue
        if line.startswith("}"):
            current = None
        m = re.match(r'^\s*0x([0-9A-Fa-f]{8}):\s*' + CSTR, line)
        if current and m and has_cyrillic(m.group(2)):
            code[("gothic:" if current == "GOTHIC_WORDS" else "code:") + m.group(1).upper()] = c_unescape(m.group(2))
    # Two single words of ps1_text.py: the name entry delete label and the save message (cmd_pack)
    text_src = open(os.path.join(PICO, "ps1_text.py"), encoding="utf-8").read()
    for name in ("NAME_ENTRY_DELETE", "SAVED"):
        m = re.search(r"^" + name + r" = \(0x([0-9A-Fa-f]{8}),(?:\s*\d+,)?\s*" + CSTR + r"\)", text_src, re.M)
        assert m, f"ps1_text.py: {name} not found"
        code["code:" + m.group(1).upper()] = c_unescape(m.group(2))
    save_json(os.path.join(UK, "ua.json"), ua)
    save_json(os.path.join(UK, "ua_psp.json"), psp)
    save_json(os.path.join(UK, "ua_code.json"), code)
    print(f"ua.json {len(ua)} lines, ua_psp.json {len(psp)} PSP-voice boxes, ua_code.json {len(code)} file-select lines")
    if odd:
        print("tags in non-script lines:", odd[:10])


# ---------------------------------------------------------------- native sources

def rust_table():
    src = open(os.path.join(REPO, "tools", "sotn_str", "src", "main.rs"), encoding="utf-8").read()
    body = src[src.index("const TABLE: [char; 256] = ["):]
    body = body[body.index("\n"):body.index("];")]
    chars = []
    for line in body.splitlines():
        chars += [c.replace("\\'", "'") for c in re.findall(r"'(\\'|[^'])'", line.split("//")[0])]
    if len(chars) != 256:
        sys.exit(f"sotn_str TABLE: {len(chars)} chars, expected 256")
    index = {}
    for i, c in enumerate(chars):
        index[c] = i  # last wins, as in UTF8_TO_INDEX
    return chars, index


TABLE, TABLE_INDEX = None, None


def cells_of(s, ua=False, menu=False):
    """8x8 cell codes of a string (None for a char the sheet lacks). menu: З is Z_CELL."""
    out = []
    for ch in s:
        if menu and ch in ("З", "№"):
            out.append(Z_CELL if ch == "З" else NO_CELL)
            continue
        if ua:
            ch = HOMOGLYPHS.get(ch, ch)
            if ch in UK_CODE:
                out.append(UK_CODE[ch])
                continue
        out.append(TABLE_INDEX.get(ch))
    return out


# sotn_str encodes plain strings with encoding_rs SHIFT_JIS (WHATWG); these code points differ from cp932.
WHATWG_TO_CP932 = {"−": "－", "〜": "～", "‖": "∥", "¢": "￠",
                   "£": "￡", "¬": "￢", "—": "―"}


def sjis(s):
    """Shift-JIS bytes as the native build makes them; raises on a char Shift-JIS lacks."""
    return "".join(WHATWG_TO_CP932.get(c, c) for c in s).encode("cp932")


def stock_bytes(font, text):
    if font == "cell":
        return bytes(c for c in cells_of(text) if c is not None) + b"\xff"
    if font == "actor":
        return b"".join(struct.pack("<H", c) for c in cells_of(text) if c is not None)
    if font == "alt":
        return text.encode("utf-8")
    return sjis(text)


def us_branch(src):
    """Keep the US PSX side of #if/#elif/#else blocks that test VERSION, REGION or TARGET."""
    out, stack = [], []  # stack of [taking, taken]

    def us(cond):
        if "!=" in cond:
            return any(k in cond for k in ("REGION_JP", "VER_JP", "TARGET_PSP"))
        return not any(k in cond for k in ("VER_USPROTO", "VER_EU", "REGION_JP", "VER_JP", "TARGET_PSP", "VER_HD"))

    for line in src.splitlines():
        s = line.strip()
        if s.startswith("#if"):
            t = us(s)
            stack.append([t, t])
        elif s.startswith("#elif") and stack:
            t = not stack[-1][1] and us(s)
            stack[-1] = [t, stack[-1][1] or t]
        elif s.startswith("#else") and stack:
            stack[-1] = [not stack[-1][1], True]
        elif s.startswith("#endif") and stack:
            stack.pop()
        elif all(t for t, _ in stack):
            out.append(line)
    return "\n".join(out)


CSTR = r'"((?:[^"\\\n]|\\.)*)"'


def c_unescape(s):
    return re.sub(r'\\(["\\nt])', lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), s)


def block_text(src, start):
    i = src.index(start)
    return src[i:src.index("\n};", i)]


def native_strings():
    """[(table, font, text, where)] of the native US sources."""
    out = []
    cfg = us_branch(open(os.path.join(REPO, "src", "config_us.h"), encoding="utf-8").read())
    for k, m in enumerate(re.finditer(r"_S\(" + CSTR + r"\)", block_text(cfg, "g_MenuStr[] = {"))):
        out.append(("MENU", "cell", c_unescape(m.group(1)), f"g_MenuStr#{k}"))
    for k, m in enumerate(re.finditer(r"\{" + CSTR + r",\s*_S\(" + CSTR + r"\),\s*" + CSTR, block_text(cfg, "g_SpellDefs[] = {"))):
        out.append(("SPELL_NAME", "sjis", c_unescape(m.group(1)), f"g_SpellDefs#{k}.name"))
        out.append(("SPELL_DESC", "sjis", c_unescape(m.group(3)), f"g_SpellDefs#{k}.desc"))
    for k, m in enumerate(re.finditer(r"\{" + CSTR + r",\s*" + CSTR, block_text(cfg, "g_RelicDefs[] = {"))):
        out.append(("RELIC_NAME", "sjis", c_unescape(m.group(1)), f"g_RelicDefs#{k}.name"))
        out.append(("RELIC_DESC", "sjis", c_unescape(m.group(2)), f"g_RelicDefs#{k}.desc"))
    import yaml
    for fname, name_t, desc_t in (("equipment", "EQUIP_NAME", "EQUIP_DESC"),
                                  ("accessories", "ACC_NAME", "ACC_DESC"), ("enemydefs", "ENEMY", None)):
        rows = yaml.safe_load(open(os.path.join(DATA, "assets", "dra", "us", fname + ".yaml"), encoding="utf-8"))
        for k, r in enumerate(rows):
            out.append((name_t, "cell", str(r.get("name") or ""), f"{fname}#{k}.name"))
            if desc_t:
                out.append((desc_t, "sjis", str(r.get("description") or ""), f"{fname}#{k}.desc"))
    menu = open(os.path.join(REPO, "src", "dra", "menu.c"), encoding="utf-8").read()
    help_block = block_text(menu, "D_800A2D48[] = {")
    help_block = help_block[:help_block.index("#elif")]
    for k, m in enumerate(re.finditer(CSTR, help_block)):
        out.append(("HELP", "sjis", c_unescape(m.group(1)), f"D_800A2D48#{k}"))
    for k, m in enumerate(re.finditer(r"_S2\(" + CSTR + r"\)", menu[menu.index("D_800A2D68[] = {"):])):
        if k < 2:
            out.append(("STATUS", "alt", unicodedata.normalize("NFKC", c_unescape(m.group(1))), f"D_800A2D68#{k}"))
    sel = us_branch(open(os.path.join(REPO, "src", "st", "sel", "select.c"), encoding="utf-8").read())
    for k, m in enumerate(re.finditer(r"\{_S\(" + CSTR + r"\),\s*_S\(" + CSTR + r"\)\}", block_text(sel, "stage_names[80] = {"))):
        out.append(("SEL_AREA", "cell", c_unescape(m.group(1)), f"stage_names#{k}.0"))
        out.append(("SEL_AREA", "cell", c_unescape(m.group(2)), f"stage_names#{k}.1"))
    shop = open(os.path.join(REPO, "src", "st", "lib", "e_shop.c"), encoding="utf-8").read()
    for k, m in enumerate(re.finditer(CSTR, block_text(shop, "D_us_80180FB4[] = {"))):
        out.append(("BESTIARY", "sjis", c_unescape(m.group(1)), f"lib/D_us_80180FB4#{k}"))
    # Every other _S and _SACTOR string of the US sources.
    for path in sorted(glob.glob(os.path.join(REPO, "src", "**", "*.[ch]"), recursive=True)):
        rel = os.path.relpath(path, REPO).replace(os.sep, "/")
        if re.search(r"_psp|/pc/|saturn|_jp|jp\.|config_us\.h", rel):
            continue
        text = us_branch(open(path, encoding="utf-8", errors="replace").read())
        for m in re.finditer(r"_SACTOR\(" + CSTR + r"\)", text):
            out.append(("ACTOR", "actor", c_unescape(m.group(1)), rel))
        if rel in ("src/st/sel/select.c",) or "_S(" in text:
            for m in re.finditer(r"\b_S\(" + CSTR + r"\)", text):
                out.append(("OTHER_CELL", "cell", c_unescape(m.group(1)), rel))
    return out


def norm(s):
    s = unicodedata.normalize("NFKC", s.replace("¨", '"'))
    s = re.sub(r"[‘’`]", "'", s)
    s = re.sub(r"[“”″¨]", '"', s)
    s = re.sub(r"[−–—―]", "-", s)
    s = re.sub(r'"+', '"', s)
    s = re.sub(r"\s+", " ", s)
    return s.strip()


def dialogue_files():
    return sorted(p for p in glob.glob(os.path.join(DATA, "assets", "**", "*.yaml"), recursive=True)
                  if "[TEXT," in open(p, encoding="utf-8").read())


def native_boxes():
    """{key: {'text', 'ops' (non-flow codes), 'file'}} for every native dialogue box."""
    import yaml
    out = {}
    for path in dialogue_files():
        rel = os.path.relpath(path, os.path.join(DATA, "assets")).replace(os.sep, "/")
        if rel.endswith("credits.yaml"):
            continue
        script = yaml.safe_load(open(path, encoding="utf-8"))["script"]
        box, text, ops = 0, [], []
        for item in script:
            name = item[0]
            if name == "TEXT":
                text.append(item[1])
                continue
            code = OPS.index(name) if name in OPS else None
            if code == 1:
                text.append("\n")
            elif code not in FLOW:
                ops.append(code if code is not None else name)
            if code in (0, 6):
                out[f"{rel}#{box}"] = {"text": "".join(text), "ops": ops, "file": rel}
                box, text, ops = box + 1, [], []
        if text or ops:
            out[f"{rel}#{box}"] = {"text": "".join(text), "ops": ops, "file": rel}
    return out


# ---------------------------------------------------------------- join

POSITIONAL = {"menu words": "MENU", "area names": "SEL_AREA"}
NO_INDEX = 0xFFFF


def where_index(where):
    """Table index of a positional native string: g_MenuStr#k -> k, stage_names#k.c -> 2k + c."""
    m = re.match(r"(?:g_MenuStr|stage_names)#(\d+)(?:\.(\d))?$", where)
    return NO_INDEX if not m else int(m.group(1)) * (2 if m.group(2) else 1) + int(m.group(2) or 0)


def xbox_text(s):
    return "".join("\n" if t[0] == "op" and t[1] == 1 else t[1] if t[0] == "tx" else "" for t in xbox_tokens(s))


def xbox_ops(s):
    return [t[1] for t in xbox_tokens(s) if t[0] == "op" and t[1] not in FLOW and t[1] not in XBOX_ONLY_OPS]


def join_sources():
    """Match every translated Xbox unit to a native string or dialogue box."""
    global TABLE, TABLE_INDEX
    TABLE, TABLE_INDEX = rust_table()
    ua = load_json(os.path.join(UK, "ua.json"))
    units = {x["id"]: x for x in xbla_units()}
    strings = native_strings()
    by_norm = {}
    for t, font, text, where in strings:
        by_norm.setdefault(norm(text), []).append((t, font, text, where))
    lines, stats, unmatched = [], {}, {}
    boxes = native_boxes()
    box_by_text = {}
    for key, b in boxes.items():
        box_by_text.setdefault(norm(b["text"]), []).append(key)
    # Positional tables: the same English word can sit in two rows with two translations, so pair the
    # Xbox list with the native list in order (difflib keeps the pairing right across gaps).
    placed, menu_len = {}, 0
    for blk, table in POSITIONAL.items():
        nat = [s for s in strings if s[0] == table and (s[2] or table == "MENU")]
        if table == "MENU":
            menu_len = len(nat)
        xs = sorted((x for x in units.values() if x["block"] == blk), key=lambda x: x["index"])
        sm = difflib.SequenceMatcher(None, [norm(x["en"]) for x in xs], [norm(s[2]) for s in nat], autojunk=False)
        for op, a1, a2, b1, b2 in sm.get_opcodes():
            for k in range(a2 - a1):
                xa, nb = xs[a1 + k], b1 + k
                # 'replace' runs of one size: the US build has a typo in one area word that the Xbox text fixed.
                if op == "equal" or (op == "replace" and a2 - a1 == b2 - b1 and difflib.SequenceMatcher(
                        None, norm(xa["en"]), norm(nat[nb][2])).ratio() >= 0.8):
                    placed[xa["id"]] = nat[nb]
    for uid, uatext in sorted(ua.items()):
        x = units.get(uid)
        if x is None:
            continue
        blk = x["block"]
        st = stats.setdefault("script" if x["kind"] == "script" else blk, {"units": 0, "exact": 0, "normalized": 0, "elsewhere": 0, "fuzzy": 0, "unmatched": 0, "eu_copy": 0})
        st["units"] += 1
        if uid in placed:
            t, font, text, where = placed[uid]
            st["exact" if text == x["en"] else "normalized" if norm(text) == norm(x["en"]) else "fuzzy"] += 1
            lines.append({"id": uid, "kind": x["kind"], "table": t, "font": font, "stock": text, "where": where,
                          "index": where_index(where)})
            continue
        if blk == "menu words" and x["index"] >= menu_len:
            st["eu_copy"] += 1  # the FR/ES/DE/IT copies of g_MenuStr: the US build has none
            continue
        if x["kind"] == "script":
            want = norm(xbox_text(x["en"]))
            keys = box_by_text.get(want, [])
            how = "exact"
            if not keys:
                close = difflib.get_close_matches(want, list(box_by_text), n=1, cutoff=0.85)
                if close:
                    keys, how = box_by_text[close[0]], "fuzzy"
            if not keys:
                st["unmatched"] += 1
                unmatched.setdefault("script", []).append(uid)
                continue
            st[how] += 1
            for key in keys:
                lines.append({"id": uid, "kind": "script", "key": key, "how": how,
                              "ops_ok": xbox_ops(x["en"]) == boxes[key]["ops"]})
            continue
        want = norm(x["en"])
        cands = by_norm.get(want, [])
        if not cands and want:
            low = want.lower()
            cands = [c for k, v in by_norm.items() if k.lower() == low for c in v]
        pref = [c for c in cands if c[0] in BLOCK_TABLES.get(blk, [])]
        if pref:
            exact = [c for c in pref if c[2] == x["en"]]
            st["exact" if exact else "normalized"] += 1
            use = exact or pref
        elif cands:
            st["elsewhere"] += 1
            use = cands
        else:
            st["unmatched"] += 1
            unmatched.setdefault(blk, []).append(uid)
            continue
        seen = set()
        for t, font, text, where in use:
            if (t, text) in seen:
                continue
            seen.add((t, text))
            lines.append({"id": uid, "kind": x["kind"], "table": t, "font": font, "stock": text, "where": where})
    # The yaml tables keep the Xbox order: a native row that no text matched (the US spelling or
    # wording differs, as in enemy names with a typo) takes the translated unit of its own row.
    done = {(l["table"], l["stock"]) for l in lines if l["kind"] != "script"}
    by_row = {(x["block"], x["index"]): x for x in units.values()}
    rows = 0
    for t, font, text, where in strings:
        m = re.match(r"\w+#(\d+)\.", where)
        x = by_row.get((ROW_BLOCKS.get(t), int(m.group(1)))) if m else None
        if x and text.strip() and (t, text) not in done and ua.get(x["id"]):
            done.add((t, text))
            lines.append({"id": x["id"], "kind": x["kind"], "table": t, "font": font, "stock": text, "where": where})
            rows += 1
    stats.setdefault("by row", {"units": 0, "exact": 0, "normalized": 0, "elsewhere": 0, "fuzzy": 0, "unmatched": 0, "eu_copy": 0})["fuzzy"] = rows
    return lines, stats, unmatched, boxes, units, ua


def join():
    """join_sources() as pack needs it, from uk/places.json: each line names its native string by
    table, place and the FNV-1a of its text, so the text itself comes from the US data."""
    global TABLE, TABLE_INDEX
    TABLE, TABLE_INDEX = rust_table()
    ua = load_json(os.path.join(UK, "ua.json"))
    native = {}
    for t, font, text, where in native_strings():
        native.setdefault((t, where, fnv(text.encode("utf-8"))), (font, text))
    boxes = native_boxes()
    lines, miss = [], []
    for p in load_json(PLACES)["lines"]:
        hit = boxes.get(p["key"]) if p["kind"] == "script" else native.get((p["table"], p["where"], p["stock"]))
        if hit is None:
            miss.append(p["id"])
        elif p["kind"] == "script":
            lines.append(dict(p))
        else:
            lines.append(dict(p, font=hit[0], stock=hit[1]))
    print(f"places.json: {len(lines)} lines found in the US data, {len(miss)} not: {miss[:8]}")
    return lines, None, None, boxes, None, ua


# The native yaml table -> the Xbox block in the same row order (join).
ROW_BLOCKS = {"ENEMY": "enemy names", "EQUIP_NAME": "item names", "EQUIP_DESC": "item descriptions",
              "ACC_NAME": "equip names", "ACC_DESC": "equip descriptions"}


def locate_scripts(unmatched, units):
    """For Xbox script blocks with no yaml box: the source files that hold their first English run."""
    blocks = {}
    for uid in unmatched.get("script", []):
        blocks.setdefault(block_of(uid), []).append(uid)
    srcs = {}
    for path in glob.glob(os.path.join(DATA, "src", "**", "*.[ch]"), recursive=True) + \
            glob.glob(os.path.join(DATA, "assets", "**", "*.yaml"), recursive=True):
        srcs[os.path.relpath(path, DATA).replace(os.sep, "/")] = open(path, encoding="utf-8", errors="replace").read()
    for path in glob.glob(os.path.join(DATA, "disks", "us", "**", "*.BIN"), recursive=True):
        srcs[os.path.relpath(path, DATA).replace(os.sep, "/")] = open(path, "rb").read().decode("latin-1")
    found = {}
    for blk, uids in sorted(blocks.items()):
        runs = [r for uid in uids for r in xbox_text(units[uid]["en"]).split("\n") if len(r.strip()) >= 8]
        hits = set()
        for r in runs[:6]:
            needle = r.strip()[:24]
            found_in = {p for p, t in srcs.items() if needle in t}
            hits = found_in if not hits else (hits & found_in or hits)
        found[blk] = {"boxes": len(uids), "files": sorted(hits)[:6]}
    return found


def cmd_join(_args):
    lines, stats, unmatched, boxes, units, ua = join_sources()
    print(f"{'category':22} units exact norm elsewh fuzzy unmatched eu_copy")
    for cat, s in sorted(stats.items()):
        print(f"{cat:22} {s['units']:5} {s['exact']:5} {s['normalized']:4} {s['elsewhere']:6} {s['fuzzy']:5} "
              f"{s['unmatched']:9} {s['eu_copy']:7}")
    tot = {k: sum(s[k] for s in stats.values()) for k in ("units", "exact", "normalized", "elsewhere", "fuzzy", "unmatched", "eu_copy")}
    print("total", tot)
    # Each stock key must be the bytes the US disc holds, or the runtime lookup never hits.
    bins = [open(p, "rb").read() for p in glob.glob(os.path.join(DATA, "disks", "us", "**", "*.BIN"), recursive=True)
            if not os.path.basename(p).startswith("F_")]
    miss, seen = {}, set()
    for l in lines:
        if l["kind"] == "script" or l["font"] == "alt" or (l["table"], l["stock"]) in seen:
            continue
        seen.add((l["table"], l["stock"]))
        b = stock_bytes(l["font"], l["stock"])
        b = b[:-1] if l["font"] == "cell" else b  # some _S strings share the FF of a longer one
        if not any(b in d for d in bins):
            miss.setdefault(l["table"], []).append(l["stock"][:24])
    print(f"stock keys checked on the US disc: {len(seen)} ({len(bins)} BIN files); not found:",
          {t: len(v) for t, v in miss.items()} or "none", [v[:3] for v in miss.values()][:4])
    scr = [l for l in lines if l["kind"] == "script"]
    files = {}
    for l in scr:
        files.setdefault(l["key"].split("#")[0], set()).add(block_of(l["id"]))
    print(f"dialogue: {len({l['id'] for l in scr})} boxes matched to {len({l['key'] for l in scr})} native boxes; "
          f"op sequence equal for {sum(l['ops_ok'] for l in scr)} of {len(scr)}")
    for f, b in sorted(files.items()):
        print(f"  {f}: {len(b)} Xbox block(s)")
    used = {l["key"].split("#")[0] for l in scr}
    print("native dialogue files with no Xbox match:", sorted({b['file'] for b in boxes.values()} - used))
    loc = locate_scripts(unmatched, units)
    for blk, v in loc.items():
        print(f"  unmatched {blk}: {v['boxes']} boxes, found in {v['files'] or 'nothing'}")
    for blk, ids in sorted(unmatched.items()):
        if blk != "script":
            print(f"  unmatched {blk}: {len(ids)} e.g. {ids[:4]}")
    # Native stock 8x8 strings (US branch) that use the cells we take over.
    hit = [(w, s) for t, f, s, w in native_strings() if f in ("cell", "actor") and any(
        c is not None and FIRST_UK_CELL <= c < FIRST_UK_CELL + len(UK_CODE) for c in cells_of(s))]
    print(f"stock 8x8 strings using cells 0x{FIRST_UK_CELL:02X}..0x{FIRST_UK_CELL + len(UK_CODE) - 1:02X}: {len(hit)}",
          sorted({w for w, _s in hit}))
    amb = {}
    for l in lines:
        if l["kind"] != "script":
            amb.setdefault((l["table"], l["stock"], l.get("index", NO_INDEX)), set()).add(ua[l["id"]])
    print("stock strings with two translations:", [(k[0], k[2], len(v)) for k, v in amb.items() if len(v) > 1])
    os.makedirs(OUT, exist_ok=True)
    save_json(os.path.join(OUT, "join.json"), {"stats": stats, "unmatched": unmatched, "scripts": loc})


# ---------------------------------------------------------------- encoders

def enc_cells(s, menu=False):
    """UA string -> 8x8 cell bytes + FF (MenuDrawStr adds the 00)."""
    cells = cells_of(s, ua=True, menu=menu)
    bad = [ch for ch, c in zip(s, cells) if c is None]
    return bytes(c for c in cells if c is not None) + b"\xff", bad


def enc_actor(s):
    bad = [ch for ch, c in zip(s, cells_of(s, ua=True)) if c is None]
    return b"".join(struct.pack("<H", c) for c in cells_of(s, ua=True) if c is not None) + b"\xff\xff", bad


def enc_bios(s):
    """UA string -> the bytes func_800F99B8 reads: ASCII letters and space as one byte, the rest Shift-JIS."""
    out, bad = bytearray(), []
    for ch in s:
        ch = {"І": "I", "і": "i"}.get(ch, ch)
        if ch.isascii() and (ch.isalpha() or ch == " "):
            out.append(ord(ch))
        elif ch == "@":
            out.append(0x40)  # the bestiary's line break
        elif ch in CUSTOM:
            out += struct.pack(">H", CUSTOM[ch])
        elif ch in '"”“¨':  # the Xbox text writes the stock ”” pair as ¨
            out += b"\x81\x68\x81\x68"  # the drawer skips two bytes after 81 68
        else:
            if "!" <= ch <= "~":
                ch = chr(ord(ch) + 0xFEE0)
            ch = BIOS_FALLBACK.get(ch, ch)
            try:
                out += ch.encode("cp932")
            except UnicodeEncodeError:
                bad.append(ch)
    return bytes(out) + b"\x00", bad


DFONT = None


def dialogue_serif():
    """The owner's source of the dialogue font: the Picoripi plugin's dialogue_serif.py (read as data,
    never imported) -> ORDER, PUNCT, {char: 11 rows of 8 bools}. cmd_vendor writes it to dfont.png."""
    import ast
    src = open(need(os.path.join(PICO, "dialogue_serif.py"), "dialogue_serif.py"), encoding="utf-8").read()
    val = {n.targets[0].id: ast.literal_eval(n.value) for n in ast.parse(src).body
           if isinstance(n, ast.Assign) and getattr(n.targets[0], "id", "") in ("ORDER", "PUNCT", "DESIGN")}
    return val["ORDER"], val["PUNCT"], {ch: [[p == "#" for p in r] for r in d] for ch, d in val["DESIGN"].items()}


def png_cells(name, cw, ch, n):
    """n cells of cw x ch from uk/<name> (16 cells a row) as rows of bools (palette index != 0)."""
    from PIL import Image
    im = Image.open(os.path.join(UK, name))
    w, px = im.size[0], list(im.getdata())
    return [[[px[((k // 16) * ch + y) * w + (k % 16) * cw + x] != 0 for x in range(cw)] for y in range(ch)]
            for k in range(n)]


def save_cells(name, cells, cw, ch):
    """The inverse of png_cells: a 1-bit palette PNG, 16 cells a row."""
    from PIL import Image
    im = Image.new("P", (16 * cw, -(-len(cells) // 16) * ch))
    im.putpalette([0, 0, 0, 255, 255, 255] + [0] * 762)
    for k, cell in enumerate(cells):
        for y, row in enumerate(cell):
            for x, on in enumerate(row):
                im.putpixel(((k % 16) * cw + x, (k // 16) * ch + y), 1 if on else 0)
    im.save(os.path.join(UK, name))


def dialogue_font():
    """The dialogue font the owner chose (2026-11-14), 8x11 cells: uk/dfont.png in the order of
    glyphs.json (cmd_vendor). Returns {char: byte}, {byte: 11 rows of 8 bools}."""
    global DFONT
    if DFONT is None:
        g = load_json(GLYPHS)["dfont"]
        chars = g["order"] + g["punct"]
        codes = {ch: 0x80 + i for i, ch in enumerate(g["order"])}
        codes.update({ch: 0xC7 + i for i, ch in enumerate(g["punct"])})
        cells = png_cells("dfont.png", 8, 11, len(chars))
        DFONT = (codes, {codes[ch]: cell for ch, cell in zip(chars, cells)})
    return DFONT


def menu_font():
    """The serif cells of the pause menu: ({8x8 cell code: 11 rows of 8 bools}, shared codes). The Cyrillic
    cells take the dialogue font's design, З its own Z_CELL; the Latin letters, digits, marks and the stat
    arrows take serif_latin.py. Those are the shared codes: en loads them too."""
    import ast
    src = open(os.path.join(HERE, "serif_latin.py"), encoding="utf-8").read()
    lat = {n.targets[0].id: ast.literal_eval(n.value) for n in ast.parse(src).body
           if isinstance(n, ast.Assign) and n.targets[0].id in ("SAME", "DESIGN", "CELL")}
    codes, rows = dialogue_font()
    out = {code: rows[codes[ch]] for ch, code in UK_CODE.items() if ch in codes}
    out[Z_CELL] = rows[codes["З"]]
    shared = set()
    for ch, design in lat["DESIGN"].items():
        c = lat["CELL"].get(ch) or TABLE_INDEX[ch]
        out[c] = [[p == "#" for p in r] for r in design]
        shared.add(c)
    for ch, cyr in lat["SAME"].items():
        out[TABLE_INDEX[ch]] = rows[codes[cyr]]
        shared.add(TABLE_INDEX[ch])
    # the Spells combo arrows (0xE0..0xE7) stay the stock cells under en
    assert not shared & set(range(0xE0, 0xE8)), sorted(shared & set(range(0xE0, 0xE8)))
    return out, shared


def enc_dialogue(markup, native_ops):
    """Markup -> pack tokens; checks the {} count against the native box.
    Letters and punctuation of the dialogue font are bytes 0x80.. (dfont.bin), the rest the 8x8 sheet."""
    out, bad, n = bytearray(), [], 0
    dcodes = dialogue_font()[0]
    for t in markup_tokens(markup):
        if t[0] == "tx":
            for ch, c in zip(t[1], cells_of(t[1], ua=True)):
                if ch in dcodes:
                    out.append(dcodes[ch])
                elif c is None:
                    bad.append(ch)
                else:
                    out.append(c + 0x20)
        elif t[1] == 1:
            out.append(1)
        elif t[1] in (2, 3):
            out += bytes((t[1], t[2]))
        else:
            out.append(NATIVE_OP)
            n += 1
    return bytes(out), bad, n == len(native_ops)


EN_FIX = {"‘": "'", "’": "'", "“": '"', "”": '"', "…": "...", "—": "-", "–": "-", " ": " "}


def wrap_markup(markup, width=DLG_LINE_CELLS):
    """Breaks each line longer than width at its last space that fits (the PSP lines are wider)."""
    out = []
    for line in markup.split("\n"):
        while len(markup_text(line)) > width:
            vis, cut = 0, None
            for m in MARK.finditer(line):
                if m.group(0).startswith("{"):
                    continue
                for k, ch in enumerate(m.group(0)):
                    if ch == " " and vis <= width:
                        cut = m.start() + k
                    vis += 1
            if cut is None:
                break
            out.append(line[:cut])
            line = line[cut + 1:]
        out.append(line)
    return "\n".join(out)


def enc_dialogue_en(markup, native_ops):
    """enc_dialogue for language = en: the text stays ASCII, as the stock 8x8 font draws it."""
    out, bad, n = bytearray(), [], 0
    for t in markup_tokens(wrap_markup(markup)):
        if t[0] == "tx":
            for ch in "".join(EN_FIX.get(c, c) for c in t[1]):
                if 0x20 <= ord(ch) < 0x7F:
                    out.append(ord(ch))
                else:
                    bad.append(ch)
        elif t[1] == 1:
            out.append(1)
        elif t[1] in (2, 3):
            out += bytes((t[1], t[2]))
        else:
            out.append(NATIVE_OP)
            n += 1
    return bytes(out), bad, n == len(native_ops)


# ---------------------------------------------------------------- BIOS glyph widths

KROM_RANGES = [(0x8140, 0x817E), (0x8180, 0x81AC), (0x81B8, 0x81BF), (0x81C8, 0x81CE), (0x81DA, 0x81E8),
               (0x81F0, 0x81F7), (0x81FC, 0x81FC), (0x824F, 0x8258), (0x8260, 0x8279), (0x8281, 0x829A),
               (0x829F, 0x82F1), (0x8340, 0x837E), (0x8380, 0x8396), (0x839F, 0x83B6), (0x83BF, 0x83D6),
               (0x8440, 0x8460), (0x8470, 0x847E), (0x8480, 0x8491), (0x849F, 0x84BE)]
KROM = None


def krom_rows(code):
    """15 x 16-bit rows of the native 12x16 source glyph (psyz Krom2RawAdd), or None."""
    global KROM
    if KROM is None:
        src = open(os.path.join(DATA, "tools", "psyz", "psyz", "src", "psyz", "krom_font.h"), encoding="utf-8").read()
        KROM = bytes(int(h, 16) for h in re.findall(r"0x([0-9A-Fa-f]{2})", src[src.index("{"):]))
    if 0x889F <= code <= 0x9872:
        return None  # kanji: not used by the translation
    index = 0
    for first, last in KROM_RANGES:
        if code < first:
            return None
        if code <= last:
            index += code - first
            g = KROM[index * 30:index * 30 + 30]
            return [g[2 * r] << 8 | g[2 * r + 1] for r in range(15)]
        index += last - first + 1
    return None


def squeeze(rows16):
    """func_80106A28: 16 source px -> 12 px; a px is lit when a source px it blends is lit."""
    out = []
    for v in rows16:
        b = [(v >> (15 - i)) & 1 for i in range(16)]
        px = []
        for g in range(4):
            s = b[4 * g:4 * g + 4]
            px += [s[0] or s[1], s[1] or s[2], s[2] or s[3]]
        out.append(px)
    return out


def advance(rows16):
    """func_800F99B8: bytes (2 px each) one glyph moves the pen."""
    px = squeeze(rows16) + [[0] * 12]
    byt = [[(r[2 * j] or r[2 * j + 1]) for j in range(6)] for r in px]
    hi = [[r[2 * j + 1] for j in range(6)] for r in px]
    while not any(r[0] for r in byt) and any(any(r) for r in byt):
        byt = [r[1:] + [0] for r in byt]
        hi = [r[1:] + [0] for r in hi]
    a = max([j for r in byt for j in range(6) if r[j]] or [0])
    if any(r[a] for r in hi):
        a += 1
    if a < 6:
        a += 1
    return a


SERIF = {}
BIOS = None


def bios_glyphs():
    """(private cells {char: 15 rows}, serif table {sjis: 15 rows}) from uk/bios12.png (cmd_vendor):
    16 x 15 cells, the serif letters of glyphs.json, then the private ones. A row: MSB = left pixel."""
    global BIOS
    if BIOS is None:
        g = load_json(GLYPHS)["bios12"]
        cells = png_cells("bios12.png", 16, 15, len(g["serif"]) + len(g["private"]))
        rows = [[sum(1 << (15 - x) for x, on in enumerate(r) if on) for r in c] for c in cells]
        serif = {struct.unpack(">H", ch.encode("cp932"))[0]: r for ch, r in zip(g["serif"], rows)}
        private = dict(zip(g["private"], rows[len(g["serif"]):]))
        # enc_bios writes І і as the ASCII I i, drawn as the full-width 0x8268 0x8289:
        # under uk they take the designed letters, not the stock BIOS ones
        serif[0x8268], serif[0x8289] = private["І"], private["і"]
        BIOS = (private, serif)
    return BIOS


def bios_glyphs_sources():
    """bios_glyphs() from the Picoripi plugin's bios_glyphs_uk.bin (cmd_vendor writes bios12.png)."""
    data = open(need(os.path.join(PICO, "bios_glyphs_uk.bin"), "bios_glyphs_uk.bin"), "rb").read()
    if len(data) != 66 * 30 + 8 * 36:
        sys.exit(f"bios_glyphs_uk.bin: {len(data)} bytes")
    codes = [c for a, b in ((0x8440, 0x8460), (0x8470, 0x847E), (0x8480, 0x8491)) for c in range(a, b + 1)]
    serif = {}
    drop = {struct.unpack(">H", ch.encode("cp932"))[0] for ch in DROP_SERIF}
    for k, code in enumerate(codes):
        if code not in drop:
            serif[code] = list(struct.unpack_from(">15H", data, k * 30))
    private = {}
    for k, ch in enumerate(PRIVATE):
        rows = struct.unpack_from(">18H", data, 66 * 30 + k * 36)
        private[ch] = list(rows[3:18])
    return private, serif


def bios_width(raw, serif=False):
    """Pen advance in px of encoded BIOS bytes (stops at NUL)."""
    private, table = bios_glyphs()
    custom = {CUSTOM[ch]: private[ch] for ch in CUSTOM}
    i, total = 0, 0
    while i < len(raw) and raw[i]:
        ch = raw[i]
        i += 1
        if 0x61 <= ch <= 0x7A or 0x41 <= ch <= 0x5A:
            code = ch + (0x8220 if ch >= 0x61 else 0x821F)
        elif ch == 0x20:
            total += 3
            continue
        else:
            code = ch << 8 | raw[i]
            i += 1
            if code == 0x8168:
                i += 2
            if code == 0x8140:
                total += 3
                continue
        rows = (table.get(code) if serif else None) or custom.get(code) or krom_rows(code) or [0] * 15
        total += advance(rows)
    return total * 2


# ---------------------------------------------------------------- width

def cmd_width(args):
    report = width_check()
    for r in report["over"][:args.get("show", 400)]:
        print(r)
    print("checked", report["checked"], "overflows", report["counts"])


def relic_name_px(uid):
    """Column capacity of a uk relic name (RELIC_* in src/dra/menu.c): the
    left column holds 132 texels, the right 120, relic 24 a row alone."""
    relic = int(uid.split(":")[1]) // 2
    return 176 if relic == 24 else 120 if relic & 1 else 132


def relic_name_cuts():
    """Relic names the menu would cut at the column edge."""
    return [o for o in width_check()["over"] if "RELIC_NAME" in o]


def width_check():
    lines, _, _, boxes, units, ua = join_sources()
    over, checked, counts = [], 0, {}
    widest = {}  # the longest stock string of each 8x8 table: the column the stock screen was laid out for
    for t, f, s, _w in native_strings():
        if f in ("cell", "actor"):
            widest[t] = max(widest.get(t, 0), len(s))

    def flag(kind, uid, msg):
        counts[kind] = counts.get(kind, 0) + 1
        over.append(f"{kind:5} {uid}: {msg}")

    done = set()
    for l in lines:
        uid = l["id"]
        if (uid, l.get("table")) in done:
            continue
        done.add((uid, l.get("table")))
        x, text = units[uid], ua[uid]
        checked += 1
        if l["kind"] == "script":
            en_lines = xbox_text(x["en"]).split("\n")
            for k, line in enumerate(markup_text(text).split("\n")):
                n = len(line)
                if n > DLG_LINE_CELLS:
                    flag("hard", uid, f"dialogue line {k} has {n} cells, the line holds {DLG_LINE_CELLS}")
                elif n > max(x.get("max_chars") or 0, max(len(e) for e in en_lines)):
                    flag("soft", uid, f"dialogue line {k} has {n} cells, stock box lines reach {max(len(e) for e in en_lines)}")
            continue
        font, stock = l["font"], l["stock"]
        text, raw, _bad = packed_text(l, text)  # measure what the pack writes
        if font == "cell" or font == "actor":
            limit = max(widest.get(l["table"], 0) if l["table"] != "OTHER_CELL" else len(stock), x.get("max_chars") or 0)
            if len(text) > limit:
                flag("soft", uid, f"{l['table']} {len(text)} cells > {limit} (widest stock in the table / Xbox limit)")
        elif font == "sjis":
            if l["table"] == "BESTIARY":
                parts = [p for p in text.split("@")]
                for p in parts:
                    w = bios_width(enc_bios(p)[0])
                    if w > 288:
                        flag("soft", uid, f"bestiary line {w} px > 288")
                continue
            limit = {"RELIC_NAME": relic_name_px(uid)}.get(l["table"], 288)
            w, ws = bios_width(raw), bios_width(raw, serif=True)
            # relic names are drawn with the serif menu font (func_800F99B8);
            # its width matches the in-game one within 4 px
            if (ws if l["table"] == "RELIC_NAME" else max(w, ws)) > limit:
                flag("hard", uid, f"{l['table']} {w} px (serif {ws}) > {limit}")
            blimit = {"RELIC_NAME": 63, "SPELL_NAME": 32}.get(l["table"])
            if blimit and len(raw) - 1 > blimit:
                flag("hard", uid, f"{l['table']} {len(raw) - 1} bytes > {blimit}")
    return {"over": over, "checked": checked, "counts": counts}


# ---------------------------------------------------------------- glyphs

def mod_records():
    """The 28 records of the Xbox 360 mod's ps1vram_uk.bin (SOTN_LANG_MEDIA)."""
    data = open(need(os.path.join(MEDIA, "ps1vram_uk.bin"), "ps1vram_uk.bin"), "rb").read()
    if data[:8] != b"SOTNPS1V" or struct.unpack_from("<I", data, 8)[0] != 1:
        sys.exit("ps1vram_uk.bin: not a version 1 SOTNPS1V file")
    n, at, out = struct.unpack_from("<I", data, 12)[0], 16, []
    for _ in range(n):
        x, y, w, h, first, whole = struct.unpack_from("<HHHHII", data, at)
        at += 16
        out.append({"x": x, "y": y, "w": w, "h": h, "first": first, "whole": whole, "body": data[at:at + w * h * 2]})
        at += w * h * 2
    names = ["copy", "delete", "entry", "change", "select", "sheet0", "sheet256", "gothic", "name_place",
             "hud300_976", "hud300_720", "hud324_976", "hud324_720", "bloodlines"] + \
            ["zone_" + z for z in ("NO0", "NO1", "LIB", "CAT", "NO2", "CHI", "DAI", "NO4", "ARE", "TOP", "NZ0", "NZ1", "DRE", "NO1_elevator")]
    if len(out) != len(names):
        sys.exit(f"ps1vram_uk.bin: {len(out)} records, expected {len(names)}")
    for r, name in zip(out, names):
        r["name"] = name
    return out


def texels_be(rows):
    """be_texels backwards: rows of texels -> big-endian halfwords."""
    return b"".join(struct.pack(">H", t[i] | t[i + 1] << 4 | t[i + 2] << 8 | t[i + 3] << 12)
                    for t in rows for i in range(0, len(t), 4))


def art_records(srcs):
    """The art.bin pictures in art_map.json order, as mod_records() has them ({name, x, y, w, h, body}),
    and the names it could not make. labels/<name>.png: the owner's whole picture (a 128-row label is
    the VRAM page from y 256). art/<name>.png: the changed texels over the stock rect of the US disc
    (index KEEP keeps the stock texel). STOCK_PIXELS: only from ps1vram_uk.bin, if SOTN_LANG_MEDIA has it."""
    from PIL import Image
    have_mod = bool(MEDIA) and os.path.isfile(os.path.join(MEDIA, "ps1vram_uk.bin"))
    mod = {r["name"]: r for r in mod_records()} if have_mod else {}
    out, missing = [], []
    for a in load_json(os.path.join(UK, "art_map.json")):
        name, (x, y, w, h) = a["name"], a["rect"]
        body = None
        if name in STOCK_PIXELS:
            body = mod[name]["body"] if name in mod else None
        elif os.path.isfile(os.path.join(UK, "labels", name + ".png")):
            im = Image.open(os.path.join(UK, "labels", name + ".png"))
            px, pw = list(im.getdata()), im.size[0]
            top = y - 256 if im.size[1] == 128 else 0
            body = texels_be([px[(top + r) * pw:(top + r + 1) * pw] for r in range(h)])
        else:
            rows = a["native"] and srcs.get(a["native"][0]) and vram_rect(srcs[a["native"][0]], x, y, w, h)
            if rows:
                d = list(Image.open(os.path.join(UK, "art", name + ".png")).getdata())
                body = texels_be([[s if d[r * 4 * w + c] == KEEP else d[r * 4 * w + c]
                                   for c, s in enumerate(t)] for r, t in enumerate(le_texels(rows))])
        if body is None:
            missing.append(name)
        else:
            out.append({"name": name, "x": x, "y": y, "w": w, "h": h, "body": body})
    return out, missing


def be_texels(body, w):
    """Rows of nibbles of big-endian halfword data, left texel = low nibble."""
    rows = []
    for r in range(len(body) // (w * 2)):
        t = []
        for i in range(w):
            v = body[(r * w + i) * 2] << 8 | body[(r * w + i) * 2 + 1]
            t += [(v >> (4 * k)) & 15 for k in range(4)]
        rows.append(t)
    return rows


def le_texels(rows_bytes):
    out = []
    for row in rows_bytes:
        t = []
        for b in row:
            t += [b & 15, b >> 4]
        out.append(t)
    return out


def sheet_cell(rows, code, first_code):
    cell = code - first_code
    return [rows[(cell // 16) * 8 + y][(cell % 16) * 8:(cell % 16) * 8 + 8] for y in range(8)]


def disc(name):
    for p in glob.glob(os.path.join(DATA, "disks", "us", "**", name), recursive=True):
        return open(p, "rb").read()
    return None


def tileset_vram(data, y0):
    """LoadStageTileset: 0x2000-byte blocks of 32x128 halfwords -> {(x, y): LE row bytes}."""
    vram = {}
    for i in range(len(data) // 0x2000):
        bx = 0x200 + (i // 4) * 0x40 + (i % 2) * 0x20
        by = y0 + (0x80 if i & 2 else 0)
        for r in range(128):
            vram[(bx, by + r)] = data[i * 0x2000 + r * 64:i * 0x2000 + r * 64 + 64]
    return vram


def vram_rect(vram, x, y, w, h):
    """LE bytes of each row of a rect, or None if the source does not cover it."""
    rows = []
    for r in range(h):
        row = b""
        for hx in range(x, x + w):
            bx = hx - (hx % 32)
            line = vram.get((bx, y + r))
            if line is None:
                return None
            row += line[(hx - bx) * 2:(hx - bx) * 2 + 2]
        rows.append(row)
    return rows


def swap16(b):
    return bytes(b[i ^ 1] for i in range(len(b)))


def fgame_sheet():
    data = disc("F_GAME.BIN")
    rows = vram_rect(tileset_vram(data, 0x100), 896, 256, 32, 128)
    return le_texels(rows)


def cmd_glyphs(_args):
    recs = {r["name"]: r for r in mod_records()}
    s0, s1 = be_texels(recs["sheet0"]["body"], 32), be_texels(recs["sheet256"]["body"], 32)
    if s0 != s1:
        print("warning: the two sheet copies differ")
    stock = fgame_sheet()
    stock_cells = {c: sheet_cell(stock, c, 0) for c in range(256)}
    ink_values = sorted({v for r in stock for v in r})
    png_rows = [[0] * 128 for _ in range(128)]
    clashes, twins_drawn, cmap = [], [], {}
    for k, ch in enumerate(UA_CELLS):
        art = sheet_cell(s1, SEL_FIRST + k, SEL_FIRST)
        if ch in HOMOGLYPHS:
            if any(any(r) for r in art):
                twins_drawn.append(ch)
            continue
        mask = [[v != 0 for v in r] for r in art]
        for c, sc in stock_cells.items():
            if any(any(r) for r in sc) and [[v != 0 for v in r] for r in sc] == mask:
                clashes.append((ch, hex(c)))
        code = UK_CODE[ch]
        for y in range(8):
            png_rows[(code >> 4) * 8 + y][(code & 15) * 8:(code & 15) * 8 + 8] = art[y]
        cmap[ch] = f"0x{code:02X}"
    from PIL import Image
    im = Image.new("P", (128, 128))
    im.putpalette([v for n in range(16) for v in (n * 17,) * 3] + [0] * (768 - 48))
    im.putdata([v for r in png_rows for v in r])
    os.makedirs(UK, exist_ok=True)
    im.save(os.path.join(UK, "font8.png"))
    save_json(os.path.join(UK, "font8_map.json"), {
        "cells": cmap, "latin_twins": HOMOGLYPHS,
        "rule": "menu and _S strings: byte = cell code; dialogue scripts: byte = cell code + 0x20",
        "sheet": "F_GAME.BIN block 24 = VRAM 896,256 (4bpp, tpage 0x1E); cell c at x 896+(c&15)*2, y 256+(c>>4)*8",
        "source": "ps1vram_uk.bin record sheet256 (mod codes 0x60 + index in the mod cell order)"})
    # 12x16 glyphs.
    private, serif = bios_glyphs()
    checked = "not checked"
    try:
        sys.path.insert(0, PICO)
        import bios_font  # noqa
        bad = [ch for ch in PRIVATE if [bios_font.widen(r) for r in bios_font.private_design(ch)][-15:] != private[ch]]
        checked = "matches bios_font.private_design" if not bad else f"differs from bios_font for {bad}"
    except Exception as e:  # the plugin module is optional
        checked = f"not checked ({type(e).__name__})"
    with open(os.path.join(UK, "bios_glyphs_uk.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write("// SPDX-License-Identifier: AGPL-3.0-or-later\n")
        f.write("// Ukrainian 12x16 letters with no Shift-JIS code, for g_SjisPages / g_CustomGlyphs\n")
        f.write("// (src/dra/66590.c). Generated by tools/lang/sotn_lang.py glyphs from the Xbox 360 mod's\n")
        f.write("// bios_glyphs_uk.bin (the owner's design). 15 rows x 2 bytes, MSB = left pixel, 16 columns\n")
        f.write("// that func_80106A28 squeezes to 12. The text encoder writes these private codes.\n")
        f.write("#define UK_SJIS_FIRST 0x%04X\n" % min(CUSTOM.values()))
        f.write("// g_SjisPages entries\n#define UK_SJIS_PAGES %s\n" % ", ".join(
            f"0x{CUSTOM[ch]:04X} /* {ch} */" for ch in CUSTOM))
        f.write("// g_CustomGlyphs entries, in the same order\n#define UK_CUSTOM_GLYPHS \\\n")
        rows = []
        for ch in CUSTOM:
            b = b"".join(struct.pack(">H", r) for r in private[ch])
            rows.append("    {%s}" % ", ".join(f"0x{v:02X}" for v in b))
        f.write(", \\\n".join(rows) + "\n")
    # The mod's own source design: ps1_vram.SEL_DRAWN (letters with marks), else ps1_text.DRAWN.
    drawn = "not checked"
    try:
        sys.path.insert(0, os.path.dirname(os.path.dirname(PICO)))
        from plugins.castlevania_sotn_xbla import ps1_text, ps1_vram  # noqa
        design = {**ps1_text.DRAWN, **ps1_vram.SEL_DRAWN}
        cmp = {ch: [[v != 0 for v in r] for r in sheet_cell(s1, SEL_FIRST + k, SEL_FIRST)] ==
               [[c == "#" for c in r] for r in design[ch]]
               for k, ch in enumerate(UA_CELLS) if ch in design and ch not in HOMOGLYPHS}
        drawn = (f"{sum(cmp.values())} of {len(cmp)} equal ps1_vram.SEL_DRAWN / ps1_text.DRAWN, "
                 f"differ: {[c for c, ok in cmp.items() if not ok] or 'none'}")
    except Exception as e:  # the plugin module is optional
        drawn = f"not checked ({type(e).__name__}: {e})"
    print("cells vs DRAWN:", drawn)
    print(f"font8.png: {len(cmap)} cells at 0x{FIRST_UK_CELL:02X}..0x{FIRST_UK_CELL + len(cmap) - 1:02X}; "
          f"stock sheet nibbles {ink_values}; cells equal to a stock cell: {clashes or 'none'}; "
          f"Latin-twin slots with ink: {twins_drawn or 'none'}")
    print(f"bios_glyphs_uk.h: {len(CUSTOM)} private glyphs ({checked}); serif table {len(serif)} glyphs, dropped {DROP_SERIF}")


# ---------------------------------------------------------------- art

STAGE_PICS = ("assets/st/*/stage_title*.png", "assets/st/*/*gfx_stage_name*.png", "assets/st/no1/D_*.png",
              "assets/st/no4/D_*.png", "assets/st/nz1/D_*.png")


def png_vram(path, x0=64, y0=256):
    from PIL import Image
    im = Image.open(path)
    if im.mode != "P" or im.size != (128, 128):
        return None
    w, h = im.size
    px = list(im.getdata())
    vram = {}
    for r in range(h):
        row = bytes(px[r * w + 2 * i] & 15 | (px[r * w + 2 * i + 1] & 15) << 4 for i in range(w // 2))
        for b in range(0, w // 4, 32):
            vram[(x0 + b, y0 + r)] = row[b * 2:b * 2 + 64].ljust(64, b"\0")
    return vram


def art_sources():
    srcs = []
    for name, y0 in (("F_SEL.BIN", 0), ("F_GAME.BIN", 0x100), ("F_GAME2.BIN", 0x100), ("F_TITLE0.BIN", 0),
                     ("F_TITLE1.BIN", 0x100), ("F_PROLO0.BIN", 0), ("F_PROLO1.BIN", 0x100),
                     ("F_GO.BIN", 0), ("F_END.BIN", 0)):
        data = disc(name)
        if data:
            for y in sorted({y0, 0, 0x100}):
                srcs.append((f"{name} @y{y}", tileset_vram(data, y)))
    for pat in STAGE_PICS:
        for p in sorted(glob.glob(os.path.join(DATA, pat))):
            v = png_vram(p)
            if v:
                srcs.append((os.path.relpath(p, DATA).replace(os.sep, "/") + " @64,256", v))
    return srcs


# Labels that keep stock pixels (the HUD plates, the name place, and the file-select labels with the stock
# frame and ornament): their PNG stays in the Picoripi folder and is not copied into git.
STOCK_PIXELS = {"hud300_976", "hud300_720", "hud324_976", "hud324_720", "name_place",
                "change", "copy", "delete", "entry", "select"}
# The y the game really loads each file at (LoadStageTileset callers in src/dra and src/sel).
# A hash hit at this base proves the placement; any other hit proves the content only.
REAL_BASE = {"F_SEL.BIN": 0, "F_GAME.BIN": 0x100, "F_GAME2.BIN": 0x100, "F_TITLE0.BIN": 0, "F_TITLE1.BIN": 0x100}


def proof_level(hits):
    if any(h == f"{n} @y{y}" for h in hits for n, y in REAL_BASE.items()):
        return "placement"
    return "content" if hits else False


CARD_SS = 8  # the card text is drawn this many times larger, then box-averaged (anti-aliasing)


def card_line(font, size, text, w, h, base, centre, track=0, word=0):
    """A w x h "L" image of `text` in the TrueType file `font` at `size` px, old-style digits, its baseline
    at row `base`, its ink box centred on column `centre` (both in 1/CARD_SS px). track: px added after
    each char, word: px added after each space (the chars are then set one by one, without kerning)."""
    from PIL import Image, ImageDraw, ImageFont
    s = CARD_SS
    big = Image.new("L", (w * s * 2, h * s))
    f, d, x = ImageFont.truetype(font, round(size * s)), ImageDraw.Draw(big), size * s
    for part in (text if track or word else [text]):
        d.text((round(x), round(base * s)), part, 255, f, anchor="ls", features=["onum"])
        x += f.getlength(part, features=["onum"]) + (track + word * (part == " ")) * s
    x0, _, x1, _ = big.getbbox()
    if x1 - x0 > w * s:
        sys.exit(f"card line {text!r}: {(x1 - x0) / s:.1f} px wide, the rect holds {w}")
    out = Image.new("L", (w * s, h * s))
    out.paste(big.crop((x0, 0, x1, h * s)), (round(centre * s - (x1 - x0) / 2), 0))
    return out.resize((w, h), Image.BOX)


def draw_card(t, p):
    """A card plate: the whole rect redrawn; each of p["lines"] ([text, size, base, centre, track, word,
    font]) in tools/lang/fonts/(the line's font or p["font"]), each texel the nibble whose p["ramp"] grey
    is nearest to its coverage."""
    h, w = len(t), len(t[0])
    cov = [0] * (w * h)
    for text, size, base, centre, track, *rest in p["lines"]:
        word = rest[0] if rest else 0
        font = rest[1] if len(rest) > 1 else p["font"]
        img = card_line(os.path.join(HERE, "fonts", font), size, text, w, h, base, centre, track, word)
        cov = list(map(max, cov, img.getdata()))
    for y in range(h):
        t[y][:] = [min(range(16), key=lambda n: abs(p["ramp"][n] - v)) for v in cov[y * w:(y + 1) * w]]


def plate_records(srcs):
    """uk/plates.py: each stock plate rect of F_SEL.BIN (or of p["src"]) with its word cleared and the
    Ukrainian word drawn, or (p["lines"]) a whole card redrawn in a font. Records as art.bin wants them (LE texels
    in "le"), one for each 32-halfword upload block the rect crosses; none without the disc."""
    import ast
    src = open(os.path.join(UK, "plates.py"), encoding="utf-8").read()
    plates = next(ast.literal_eval(n.value) for n in ast.parse(src).body if isinstance(n, ast.Assign))
    out = []
    for name, p in plates.items():
        p = dict(plates[p["like"]], **p) if "like" in p else p
        x, y, w, h = p["rect"]
        sname = p.get("src", "F_SEL.BIN @y0")
        vram = srcs.get(sname)
        rows = vram and vram_rect(vram, x, y, w, h)
        if not rows:
            print(f"plate {name}: {sname} not found, the plate stays stock")
            continue
        t = le_texels(rows)
        if "lines" in p:
            draw_card(t, p)
            cuts = [x] + [b for b in range(x - x % 32 + 32, x + w, 32)] + [x + w]
            for k, (a, b) in enumerate(zip(cuts, cuts[1:])):
                part = [r[(a - x) * 2:(b - x) * 2] for r in rows]
                out.append({"name": f"{name}_{k}", "x": a, "y": y, "w": b - a, "h": h,
                            "first": fnv(part[0]), "whole": fnv(b"".join(part)),
                            "le": b"".join(bytes(r[i] | r[i + 1] << 4 for i in range((a - x) * 4, (b - x) * 4, 2))
                                           for r in t)})
            continue
        (c0, c1), (r0, r1), (ax, ay) = p["cols"], p["rows"], p["at"]
        if ax < c0 or ax + max(map(len, p["word"])) - 1 > c1:
            sys.exit(f"plate {name}: the word is wider than the stock word's cols {c0}..{c1}")
        ink = {(r, c) for r in range(r0, r1 + 1) for c in range(c0, c1 + 1) if t[r][c] in p["stock_ink"]}
        for r, c in sorted(ink):
            t[r][c] = t[r][min((abs(k - c), k) for k in range(c0, c1 + 1) if (r, k) not in ink)[1]]
        for dy, line in enumerate(p["word"]):
            for dx, ch in enumerate(line):
                if ch == "#":
                    t[ay + dy][ax + dx] = p["ink"][dy]
        out.append({"name": name, "x": x, "y": y, "w": w, "h": h, "first": fnv(rows[0]),
                    "whole": fnv(b"".join(rows)),
                    "le": b"".join(bytes(r[i] | r[i + 1] << 4 for i in range(0, w * 4, 2)) for r in t)})
    return out


def local_records():
    """uk/local/art.bin, git-ignored: the owner's pictures made from stock art (the uk title logo,
    PRESS START, Now Loading, the title background, the game-over picture). Same records as art.bin; they go into the pack as they are. None if it is missing."""
    p = os.path.join(UK, "local", "art.bin")
    if not os.path.isfile(p):
        print("art: no uk/local/art.bin: the title logo, PRESS START, Now Loading, the title background and the game-over picture stay stock")
        return []
    data = open(p, "rb").read()
    if data[:8] != b"SOTNART1":
        sys.exit("uk/local/art.bin: not a SOTNART1 file")
    out, o = [], 12
    for _ in range(struct.unpack_from("<I", data, 8)[0]):
        x, y, w, h, first, whole, _proven, nl = struct.unpack_from("<HHHHIIBB", data, o)
        name = data[o + 18:o + 18 + nl].decode()
        o += 18 + nl
        out.append({"name": name, "x": x, "y": y, "w": w, "h": h, "first": first, "whole": whole,
                    "le": data[o:o + w * h * 2]})
        o += w * h * 2
    return out


def cmd_art(_args):
    recs = mod_records()
    srcs = art_sources()
    amap, labels_out = [], os.path.join(UK, "labels")
    os.makedirs(labels_out, exist_ok=True)
    from PIL import Image
    for r in recs:
        hits = []
        if r["first"] or r["whole"]:
            for sname, vram in srcs:
                rows = vram_rect(vram, r["x"], r["y"], r["w"], r["h"])
                if rows and fnv(swap16(rows[0])) == r["first"] and fnv(swap16(b"".join(rows))) == r["whole"]:
                    hits.append(sname)
        label = os.path.join(PICO, "labels", r["name"] + ".png")
        same = None
        if os.path.exists(label):
            im = Image.open(label)
            px = list(im.getdata())
            full = be_texels(r["body"], r["w"])
            w = im.size[0]
            rows = [px[i * w:(i + 1) * w] for i in range(im.size[1])]
            same = any(rows[t:t + r["h"]] == full for t in range(len(rows) - r["h"] + 1))
            if r["name"] not in STOCK_PIXELS:
                shutil.copyfile(label, os.path.join(labels_out, r["name"] + ".png"))
        amap.append({"name": r["name"], "rect": [r["x"], r["y"], r["w"], r["h"]],
                     "native": hits, "proven": proof_level(hits),
                     "how": "fnv1a32 of the stock rect (big-endian halfwords) equals the record hash" if hits else
                     ("no stock hash: the port writes it on demand" if not (r["first"] or r["whole"]) else
                      "no native source matched the record hash"),
                     "label_png": (r["name"] + ".png") if same else None, "label_matches_record": same,
                     "stock_pixels": r["name"] in STOCK_PIXELS,
                     "git": "font8.png" if r["name"].startswith("sheet") else
                     (None if r["name"] in STOCK_PIXELS or not same else "labels/" + r["name"] + ".png")})
    save_json(os.path.join(UK, "art_map.json"), amap)
    for a in amap:
        print(f"{a['name']:20} {a['rect']} proven={a['proven']} {a['native'][:2]} label={a['label_matches_record']} git={a['git']}")
    print("proven", sum(bool(a["proven"]) for a in amap), "of", len(amap), "; placement",
          sum(a["proven"] == "placement" for a in amap), "content only",
          [a["name"] for a in amap if a["proven"] == "content"])


# ---------------------------------------------------------------- pack

# ---------------------------------------------------------------- stage 2: rebuilt scripts, file select, C pack

FLOW_NAMES = ("LINE_BREAK", "SET_SPEED", "SET_WAIT")
OP_ALIAS = {"STOP_EVENTS": 18, "SCRIPT_UNKNOWN_22": 22}


def script_word(x):
    return bytes(((x & 0xFF000) >> 12, (x & 0xFF00) >> 8, (x & 0xFF0) >> 4, x & 0xFF))


def script_half(x):
    return bytes(((x & 0xFF0) >> 4, x & 0xFF))


def enc_item(it):
    """One yaml script item -> (bytes as include/cutscene.h makes them, [(byte offset, PSX target)])."""
    name, args = it[0], list(it[1:])
    if name == "TEXT":
        return args[0].encode("ascii"), []
    if name == "BYTE":
        return bytes((args[0] & 0xFF,)), []
    code = OP_ALIAS.get(name, OPS.index(name) if name in OPS else None)
    if code is None:
        sys.exit(f"script op {name}: unknown")
    out, rel = bytearray((code,)), []
    if name in ("SET_EVENTS", "SCRIPT_UNKNOWN_15"):
        rel.append((1, args[0]))
        out += script_word(args[0])
    elif name == "SCRIPT_SWITCH":  # the variable, then one word per case
        out += script_word(args[0])
        for k, a in enumerate(args[1:]):
            rel.append((5 + 4 * k, a))
            out += script_word(a)
    elif name == "LOAD_PORTRAIT":
        rel.append((1, args[0]))
        out += script_word(args[0]) + bytes((args[1] & 0xFF,))
    elif name in ("PLAY_SOUND", "SCRIPT_UNKNOWN_20"):
        out += script_half(args[0])
    else:
        out += bytes(a & 0xFF for a in args)
    return bytes(out), rel


def yaml_boxes(rel):
    """The items of a dialogue yaml cut into boxes, as native_boxes() cuts them."""
    import yaml
    script = yaml.safe_load(open(os.path.join(DATA, "assets", rel), encoding="utf-8"))["script"]
    boxes, cur = [], []
    for it in script:
        cur.append(it)
        if it[0] in ("END_CUTSCENE", "NEXT_DIALOG"):
            boxes.append(cur)
            cur = []
    if cur:
        boxes.append(cur)
    return boxes


def build_script(rel, recs):
    """Stock and rebuilt bytes of one yaml. recs: {box key: pack box data}. Returns the record or None."""
    stock, new, relocs, amap, used = bytearray(), bytearray(), [], [], 0
    for b, items in enumerate(yaml_boxes(rel)):
        encs = [enc_item(it) for it in items]
        data = recs.get(f"{rel}#{b}")
        if data is None:
            for by, rl in encs:
                amap.append((len(stock), len(new)))
                relocs += [(len(new) + off, tgt) for off, tgt in rl]
                stock += by
                new += by
            continue
        used += 1
        nonflow = [k for k, it in enumerate(items) if it[0] != "TEXT" and it[0] not in FLOW_NAMES]
        start, op_new, i, j = len(new), [], 0, 0
        while i < len(data):
            c = data[i]
            if c == NATIVE_OP:
                by, rl = encs[nonflow[j]]
                op_new.append(len(new))
                relocs += [(len(new) + off, tgt) for off, tgt in rl]
                new += by
                j += 1
                i += 1
            elif c in (2, 3):
                new += data[i:i + 2]
                i += 2
            else:
                new.append(c)
                i += 1
        assert j == len(nonflow), f"{rel}#{b}: {j} of {len(nonflow)} native ops"
        last, n = start, 0
        for k, (by, _rl) in enumerate(encs):
            if n < len(nonflow) and nonflow[n] == k:
                amap.append((len(stock), op_new[n]))
                last = op_new[n] + len(by)
                n += 1
            else:
                amap.append((len(stock), last))  # text and flow ops: after the previous native op
            stock += by
    amap.append((len(stock), len(new)))
    return {"name": rel, "stock": bytes(stock), "new": bytes(new), "rel": relocs, "map": amap, "boxes": used}


def write_scripts(path, recs, rels):
    """A script table: a record for every dialogue yaml of rels, so that a range of several scripts
    is copied whole; a yaml without boxes of recs keeps its stock bytes."""
    out, n, info = bytearray(), 0, []
    for rel in rels:
        r = build_script(rel, recs)
        n += 1
        name = r["name"].encode()
        pad = lambda b: b + bytes(-len(b) % 4)
        out += struct.pack("<6I", fnv(r["stock"]), len(r["stock"]), len(r["new"]), len(r["rel"]), len(r["map"]), len(name))
        out += pad(name) + pad(r["new"])
        out += b"".join(struct.pack("<II", p, t) for p, t in r["rel"])
        out += b"".join(struct.pack("<II", o, w) for o, w in r["map"])
        info.append(f"{rel} {r['boxes']} boxes {len(r['stock'])}->{len(r['new'])}")
    with open(path, "wb") as f:
        f.write(b"SOTNSCR1" + struct.pack("<I", n) + out)
    return info


def code_lines_sources():
    """File-select messages (ua_code.json) -> [(key, native stock, ua)]: Xbox English (ps1_code.py comments)
    matched to the sel string literals. Returns (pairs, misses)."""
    ua = load_json(os.path.join(UK, "ua_code.json"))
    src = open(need(os.path.join(PICO, "ps1_code.py"), "ps1_code.py"), encoding="utf-8").read()
    en = {"code:" + m.group(1).upper(): m.group(3).strip()
          for m in re.finditer(r"^\s*0x([0-9A-Fa-f]{8}):\s*" + CSTR + r",\s*#\s*(.*)$", src, re.M)}
    sel = us_branch(open(os.path.join(REPO, "src", "st", "sel", "select.c"), encoding="utf-8").read())
    nat = sorted({c_unescape(m.group(1)) for m in re.finditer(r"(?<!_S\()(?<!_S2\()" + CSTR, sel)})
    by_norm = {norm(s).lower(): s for s in nat}
    pairs, misses = [], []
    for key, text in sorted(ua.items()):
        if not key.startswith("code:") or key not in en:
            continue
        want = norm(en[key].replace("Memory Unit", "Memory Card")).lower()
        hit = by_norm.get(want)
        if hit is None:
            close = difflib.get_close_matches(want, list(by_norm), n=1, cutoff=0.8)
            hit = by_norm[close[0]] if close else None
        if hit is None:
            misses.append((key, en[key]))
        else:
            pairs.append((key, hit, text))
    return pairs, misses


def sel_hash(s):
    try:
        return fnv(sjis(s))
    except UnicodeEncodeError:
        return None


def code_lines():
    """File-select messages: [(FNV-1a of the sel literal's Shift-JIS, ua)] by places.json "code"
    (cmd_vendor), each checked against the sel literals. Returns (pairs, misses)."""
    ua = load_json(os.path.join(UK, "ua_code.json"))
    sel = us_branch(open(os.path.join(REPO, "src", "st", "sel", "select.c"), encoding="utf-8").read())
    have = {sel_hash(c_unescape(m.group(1))) for m in re.finditer(r"(?<!_S\()(?<!_S2\()" + CSTR, sel)}
    pairs, misses = [], []
    for key, h in load_json(PLACES)["code"]:
        if h in have and key in ua:
            pairs.append((h, ua[key]))
        else:
            misses.append(key)
    return pairs, misses


PACK_FILES = ("text.bin", "font8.bin", "dfont.bin", "mfont.bin", "bios.bin", "art.bin", "script.bin", "script_psp.bin")
PACK_OPTIONAL = ("script_jp.bin", "script_jp_en.bin", "script_psp_en.bin")  # packed when pack made them


def write_c(path, folder):
    """The runtime pack as one C array: "SOTNPACK" u32 n, n x {char name[16]; u32 off; u32 len}, data.
    name: the first 16 bytes of the file name (Lang_File compares 16 bytes)."""
    names = list(PACK_FILES) + [n for n in PACK_OPTIONAL if folder and os.path.exists(os.path.join(folder, n))]
    blobs = [] if folder is None else [(n, open(os.path.join(folder, n), "rb").read()) for n in names]
    keys = [n.encode()[:16] for n, _b in blobs]
    assert len(set(keys)) == len(keys), f"two pack names share their first 16 bytes: {keys}"
    head = b"SOTNPACK" + struct.pack("<I", len(blobs))
    off = len(head) + 24 * len(blobs)
    data = bytearray()
    for n, b in blobs:
        head += n.encode()[:16].ljust(16, b"\0") + struct.pack("<II", off + len(data), len(b))
        data += b + bytes(-len(b) % 4)
    blob = head + data if blobs else b""
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("// Generated by tools/lang/sotn_lang.py pack. Do not edit, do not commit.\n")
        f.write("const unsigned int g_LangPackUkSize = %d;\n" % len(blob))
        f.write("const unsigned char g_LangPackUk[%d] __attribute__((aligned(4))) = {\n" % max(1, len(blob)))
        for i in range(0, len(blob), 24):
            f.write(",".join(str(v) for v in blob[i:i + 24]) + ",\n")
        f.write("};\n")


def packed_text(l, text):
    """The exact text and bytes the pack writes for one non-script line (keeps the stock leading spaces)."""
    lead = len(l["stock"]) - len(l["stock"].lstrip(" "))
    text = " " * lead + text.lstrip(" ") if lead else text
    enc = {"cell": enc_cells, "actor": enc_actor, "sjis": enc_bios}.get(l["font"])
    if enc is enc_cells and l.get("table") in MENU_TABLES:
        enc = lambda s: enc_cells(s, menu=True)  # noqa: E731
    data, bad = enc(text) if enc else (text.encode("utf-8") + b"\0", [])
    return text, data, bad


def cmd_pack(_args):
    lines, _, _, boxes, units, ua = join()
    os.makedirs(OUT, exist_ok=True)
    bad_chars = {}
    # text.bin
    entries = {}
    for l in lines:
        if l["kind"] == "script":
            continue
        _text, data, bad = packed_text(l, ua[l["id"]])
        for ch in bad:
            bad_chars[ch] = bad_chars.get(ch, 0) + 1
        key = (TABLES.index(l["table"]), fnv(stock_bytes(l["font"], l["stock"])), l.get("index", NO_INDEX))
        old = entries.get(key)
        flags = 1 if old and (old[2] != data or old[1]) else 0  # 1: two UA texts share one key
        entries[key] = (FONT[l["font"]], flags, old[2] if old else data)
    # File-select messages: matched to the sel literals by the Xbox English
    sel_pairs, sel_miss = code_lines()
    for h, text in sel_pairs:
        data, bad = enc_bios(text)
        for ch in bad:
            bad_chars[ch] = bad_chars.get(ch, 0) + 1
        entries.setdefault((TABLES.index("OTHER_SJIS"), h, NO_INDEX), (FONT["sjis"], 0, data))
    print(f"file select: {len(sel_pairs)} lines matched, {len(sel_miss)} not:", sel_miss)
    # Two words of ps1_text.py (cmd_extract puts them in ua_code.json by their Xbox address).
    code = load_json(os.path.join(UK, "ua_code.json"))
    # The name entry draws the file-select cancel label, which deletes a letter there: row 1 of
    # that cell (Lang_NameDelete), so the file select keeps its own word.
    cancel = [l for l in lines if l["id"] == "name entry labels:2"]
    extra = set()
    if cancel and "code:82211D44" in code:
        key = (TABLES.index("OTHER_CELL"), fnv(stock_bytes("cell", cancel[0]["stock"])), 1)
        entries[key] = (FONT["cell"], 0, enc_cells(code["code:82211D44"])[0])
        extra.add(key)
    # The save room message: its literal in the US branch of src/dra/62DEC.c (Lang_Sjis).
    dra = us_branch(open(os.path.join(REPO, "src", "dra", "62DEC.c"), encoding="utf-8").read())
    m = re.search(r"D_80137E4C == 6\) \{\s*func_800F9D88\(LANG_SJIS\(" + CSTR, dra)
    if m and "code:821FEC80" in code:
        entries[(TABLES.index("OTHER_SJIS"), fnv(sjis(c_unescape(m.group(1)))), NO_INDEX)] = (
            FONT["sjis"], 0, enc_bios(code["code:821FEC80"])[0])
    # PC: the file-select card of a PSP Maria save shows the sel literal "Maria". The Xbox file
    # select has no such word, so it takes her speaker name.
    sel_src = us_branch(open(os.path.join(REPO, "src", "st", "sel", "select.c"), encoding="utf-8").read())
    if '"Maria"' in sel_src and "speaker names:4" in ua:
        data, bad = enc_bios(ua["speaker names:4"])
        assert not bad, f"Maria: no file-select glyph for {bad}"
        entries[(TABLES.index("OTHER_SJIS"), fnv(sjis("Maria")), NO_INDEX)] = (FONT["sjis"], 0, data)
    # ua_menu.json: g_MenuStr rows the Xbox text lacks (the Saturn familiars) or words the owner
    # changed for the port ("row": "text"). A hyphen or space lets the familiar list split a long name.
    rows = {where_index(w): s for t, _f, s, w in native_strings() if t == "MENU"}
    for row, text in load_json(os.path.join(UK, "ua_menu.json")).items():
        data, bad = enc_cells(text, menu=True)
        assert not bad and int(row) in rows, f"ua_menu.json row {row}: {bad}"
        key = (TABLES.index("MENU"), fnv(stock_bytes("cell", rows[int(row)])), int(row))
        entries[key] = (FONT["cell"], 0, data)
    # ua_zones.json: the area names of the port's own screens by stage id (hash 0), menu cells
    for sid, text in load_json(os.path.join(UK, "ua_zones.json")).items():
        if len(sid) != 2:
            continue
        data, bad = enc_cells(text, menu=True)
        assert not bad, f"ua_zones.json {sid}: {bad}"
        key = (TABLES.index("ZONE"), 0, int(sid, 16))
        entries[key] = (FONT["cell"], 0, data)
        extra.add(key)
    # ua_port.json: words of the port's own screens by index (hash 0), menu cells
    for idx, text in load_json(os.path.join(UK, "ua_port.json")).items():
        data, bad = enc_cells(text, menu=True)
        assert not bad, f"ua_port.json {idx}: {bad}"
        key = (TABLES.index("PORT"), 0, int(idx))
        entries[key] = (FONT["cell"], 0, data)
        extra.add(key)
    # The status page words in sentence case (Label in src/dra/menu.c): PORT 100 + the g_MenuStr row,
    # from the same text as the MENU row (ua_menu.json wins)
    menu_uk = {l["index"]: ua[l["id"]] for l in lines if l.get("table") == "MENU" and l.get("index", NO_INDEX) != NO_INDEX}
    menu_uk.update({int(r): t for r, t in load_json(os.path.join(UK, "ua_menu.json")).items()})
    for row in STATUS_ROWS:
        text = menu_uk[row]
        data, bad = enc_cells(text[:1].upper() + text[1:].lower(), menu=True)
        assert not bad, f"status word {row}: {bad}"
        key = (TABLES.index("PORT"), 0, 100 + row)
        entries[key] = (FONT["cell"], 0, data)
        extra.add(key)
    print(f"name entry delete: {len(extra)}, save message: {1 if m else 0}")
    positional = {TABLES.index(t) for t in POSITIONAL.values()}
    stray = [k for k in entries if k not in extra and (k[0] in positional) != (k[2] != NO_INDEX)]
    assert not stray, f"row index missing or misplaced: {stray[:5]}"
    blob, ents = bytearray(), bytearray()
    for (t, h, ix), (font, flags, data) in sorted(entries.items()):
        ents += struct.pack("<HBBIIHH", t, font, flags, h, len(blob), len(data), ix)
        blob += data
    head = 16
    with open(os.path.join(OUT, "text.bin"), "wb") as f:
        f.write(b"SOTNLANG" + struct.pack("<II", 2, len(entries)))
        f.write(ents)
        f.write(blob)
    # dialogue*.bin and the uk script tables (FORMAT.md): ps1 script.bin, psp script_psp.bin
    # (ua_psp.json over ua.json), jp script_jp.bin (ua_jp.json over ua.json; ua.json is from the
    # Japanese script)
    psp = load_json(os.path.join(UK, "ua_psp.json"))
    rels = sorted({b["file"] for b in boxes.values()})
    counts = {}
    for fname, over in (("dialogue.bin", {}), ("dialogue_psp.bin", psp),
                        ("dialogue_jp.bin", load_json_opt(os.path.join(UK, "ua_jp.json")))):
        recs, nbad = {}, 0
        for l in lines:
            if l["kind"] != "script":
                continue
            markup = over.get(l["id"], ua[l["id"]])
            data, bad, ok = enc_dialogue(markup, boxes[l["key"]]["ops"])
            for ch in bad:
                bad_chars[ch] = bad_chars.get(ch, 0) + 1
            if not (ok and l["ops_ok"]):
                nbad += 1
                continue
            recs[l["key"]] = data
        blob, ents, names = bytearray(), bytearray(), bytearray()
        for key, data in sorted(recs.items()):
            kb = key.encode("utf-8") + b"\0"
            ents += struct.pack("<IIII", fnv(key.encode("utf-8")), len(names), len(blob), len(data))
            names += kb
            blob += data
        with open(os.path.join(OUT, fname), "wb") as f:
            f.write(b"SOTNDLG1" + struct.pack("<III", len(recs), 20 + len(ents), 20 + len(ents) + len(names)))
            f.write(ents + names + blob)
        counts[fname] = (len(recs), nbad)
        sname = fname.replace("dialogue", "script")
        info = write_scripts(os.path.join(OUT, sname), recs, rels)
        print(f"{sname}: {len(info)} scripts")
        for s in info:
            print("   ", s)
    # The en script tables: jp script_jp_en.bin (en/en_jp.json: {ua.json id or box key: markup}),
    # psp script_psp_en.bin (the PSP English boxes of the owner's image, disc_text.py). Without
    # them en keeps the stock text for that voice.
    by_id = {l["id"]: l["key"] for l in lines if l["kind"] == "script"}
    disc = load_json_opt(DISC_TEXT)
    for sname, src in (("script_jp_en.bin", load_json_opt(os.path.join(EN, "en_jp.json"))),
                       ("script_psp_en.bin", disc.get("psp_en", {}))):
        path = os.path.join(OUT, sname)
        if os.path.exists(path):
            os.remove(path)
        recs, nbad, wrapped = {}, 0, 0
        for k, markup in sorted(src.items()):
            key = k if k in boxes else by_id.get(k)
            if key not in boxes:
                nbad += 1
                continue
            data, bad, ok = enc_dialogue_en(markup, boxes[key]["ops"])
            for ch in bad:
                bad_chars[ch] = bad_chars.get(ch, 0) + 1
            if bad or not ok:
                nbad += 1
                continue
            wrapped += wrap_markup(markup) != markup
            recs[key] = data
        counts[sname] = (len(recs), nbad)
        if recs:
            info = write_scripts(path, recs, rels)
            print(f"{sname}: {len(recs)} boxes ({wrapped} with lines broken at {DLG_LINE_CELLS} cells),"
                  f" {nbad} left stock, {len(info)} scripts")
        else:
            print(f"{sname}: no boxes: en keeps the stock text for this voice")
    # font8.bin
    from PIL import Image
    im = Image.open(os.path.join(UK, "font8.png"))
    px = list(im.getdata())
    cells = bytearray()
    for code in sorted(UK_CODE.values()):
        cells.append(code)
        for y in range(8):
            row = px[((code >> 4) * 8 + y) * 128 + (code & 15) * 8:][:8]
            cells += bytes(row[2 * i] | row[2 * i + 1] << 4 for i in range(4))
    with open(os.path.join(OUT, "font8.bin"), "wb") as f:
        f.write(b"SOTNFNT8" + struct.pack("<I", len(UK_CODE)) + cells)
    # dfont.bin: the dialogue font, 8x11 cells, ink 6 like font8.png
    glyph_rows = dialogue_font()[1]
    with open(os.path.join(OUT, "dfont.bin"), "wb") as f:
        f.write(b"SOTNDFN1" + struct.pack("<I", len(glyph_rows)))
        for code, rows in sorted(glyph_rows.items()):
            assert len(rows) == 11 and all(len(r) == 8 for r in rows), code
            f.write(bytes((code, 0, 0, 0)) + bytes((6 if r[2 * i] else 0) | (0x60 if r[2 * i + 1] else 0)
                                                   for r in rows for i in range(4)))
    # mfont.bin: the pause menu's serif cells, same records keyed by the 8x8 cell code;
    # byte 1 = 1: a serif_latin.py cell, which en loads too; bytes 2, 3: the 12x16 code
    # (enc_bios) of the uk letter or mark the cell draws, 0 for none (the Librarian's
    # description line draws its 12x16 text with these cells)
    mrows, shared = menu_font()
    msjis = {}
    for ch, code in [*UK_CODE.items(), ("З", Z_CELL), *((cyr, TABLE_INDEX[lat]) for cyr, lat in HOMOGLYPHS.items() if cyr != "З")]:
        b = enc_bios(ch)[0][:-1]
        if len(b) == 2 and code in mrows:
            msjis[code] = b
    with open(os.path.join(OUT, "mfont.bin"), "wb") as f:
        f.write(b"SOTNMFN1" + struct.pack("<I", len(mrows)))
        for code, rows in sorted(mrows.items()):
            assert len(rows) == 11 and all(len(r) == 8 for r in rows), code
            f.write(bytes((code, 1 if code in shared else 0)) + msjis.get(code, b"\0\0")
                    + bytes((6 if r[2 * i] else 0) | (0x60 if r[2 * i + 1] else 0)
                                                   for r in rows for i in range(4)))
    # bios.bin
    private, serif = bios_glyphs()
    gl = [(CUSTOM[ch], 0, private[ch]) for ch in CUSTOM] + [(c, 1, rows) for c, rows in sorted(serif.items())]
    with open(os.path.join(OUT, "bios.bin"), "wb") as f:
        f.write(b"SOTNBIOS" + struct.pack("<I", len(gl)))
        for code, flags, rows in gl:
            f.write(struct.pack("<HH", code, flags) + b"".join(struct.pack(">H", r) for r in rows))
    # art.bin
    amap = {a["name"]: a for a in load_json(os.path.join(UK, "art_map.json"))}
    srcs = dict(art_sources())
    recs, art_missing = art_records(srcs)
    if art_missing:
        print(f"art: {len(art_missing)} pictures left out (they stay English): {art_missing}")
        if any(n in STOCK_PIXELS for n in art_missing):
            print("     the labels with stock pixels come from the Xbox 360 mod's ps1vram_uk.bin:"
                  " put it in games/ (build.py) or set SOTN_LANG_MEDIA to its folder")
    plates = plate_records(srcs) + local_records()
    with open(os.path.join(OUT, "art.bin"), "wb") as f:
        f.write(b"SOTNART1" + struct.pack("<I", len(recs) + len(plates)))
        for r in recs:
            a = amap[r["name"]]
            first = whole = 0
            if a["proven"]:
                rows = vram_rect(srcs[a["native"][0]], r["x"], r["y"], r["w"], r["h"])
                first, whole = fnv(rows[0]), fnv(b"".join(rows))
            name = r["name"].encode()
            f.write(struct.pack("<HHHHIIBB", r["x"], r["y"], r["w"], r["h"], first, whole,
                                1 if a["proven"] else 0, len(name)) + name + swap16(r["body"]))
        for r in plates:
            name = r["name"].encode()
            f.write(struct.pack("<HHHHIIBB", r["x"], r["y"], r["w"], r["h"], r["first"], r["whole"], 1,
                                len(name)) + name + r["le"])
    print(f"text.bin {len(entries)} entries ({sum(1 for e in entries.values() if e[1])} ambiguous); "
          f"dialogue {counts}; font8.bin {len(UK_CODE)} cells; bios.bin {len(gl)} glyphs; art.bin {len(recs) + len(plates)} records")
    print("chars with no glyph:", bad_chars or "none")
    for n in sorted(os.listdir(OUT)):
        print(f"  {n} {os.path.getsize(os.path.join(OUT, n))}")


# ---------------------------------------------------------------- vendor

def cmd_vendor(_args):
    """The owner's sources -> the files pack reads in git: places.json (where each translated line sits
    in the US data, no English text), glyphs.json + dfont.png + bios12.png (the owner's fonts) and
    art/<name>.png (the changed texels of each picture that has a stock rect and no label)."""
    from PIL import Image
    lines = join_sources()[0]
    keep = []
    for l in lines:
        if l["kind"] == "script":
            keep.append({"id": l["id"], "kind": "script", "key": l["key"], "ops_ok": l["ops_ok"]})
            continue
        p = {"id": l["id"], "kind": l["kind"], "table": l["table"], "where": l["where"],
             "stock": fnv(l["stock"].encode("utf-8"))}
        if "index" in l:
            p["index"] = l["index"]
        keep.append(p)
    triples, misses = code_lines_sources()
    code = [[key, fnv(sjis(stock))] for key, stock, _text in triples]
    with open(PLACES, "w", encoding="utf-8", newline="\n") as f:
        f.write('{"lines": [\n' + ",\n".join(json.dumps(p, ensure_ascii=False, sort_keys=True) for p in keep)
                + '\n],\n"code": [\n' + ",\n".join(json.dumps(c) for c in code) + "\n]}\n")
    order, punct, design = dialogue_serif()
    save_cells("dfont.png", [design[ch] for ch in order + punct], 8, 11)
    private, serif = bios_glyphs_sources()
    serif_chars = "".join(bytes.fromhex(f"{c:04X}").decode("cp932") for c in sorted(serif)
                          if c not in (0x8268, 0x8289))
    cells = [serif[struct.unpack(">H", ch.encode("cp932"))[0]] for ch in serif_chars] + [private[ch] for ch in PRIVATE]
    save_cells("bios12.png", [[[bool(v >> (15 - x) & 1) for x in range(16)] for v in c] for c in cells], 16, 15)
    save_json(GLYPHS, {"dfont": {"order": order, "punct": punct},
                       "bios12": {"serif": serif_chars, "private": PRIVATE}})
    srcs = dict(art_sources())
    os.makedirs(os.path.join(UK, "art"), exist_ok=True)
    mod = {r["name"]: r for r in mod_records()}
    deltas = []
    for a in load_json(os.path.join(UK, "art_map.json")):
        name, (x, y, w, h) = a["name"], a["rect"]
        if name in STOCK_PIXELS or os.path.isfile(os.path.join(UK, "labels", name + ".png")) or not a["native"]:
            continue
        stock = le_texels(vram_rect(srcs[a["native"][0]], x, y, w, h))
        new = be_texels(mod[name]["body"], w)
        im = Image.new("P", (4 * w, h))
        im.putpalette([v for n in range(16) for v in (n * 17,) * 3] + [255, 0, 255] + [0] * (768 - 51))
        im.putdata([n if n != s else KEEP for rn, rs in zip(new, stock) for n, s in zip(rn, rs)])
        im.save(os.path.join(UK, "art", name + ".png"))
        deltas.append(name)
    print(f"places.json: {len(keep)} lines, {len(code)} file-select lines ({len(misses)} not matched); "
          f"dfont.png {len(order + punct)} cells; bios12.png {len(cells)} cells; art/: {deltas}")


# ---------------------------------------------------------------- guard

def cmd_guard(_args):
    """The stock strings: the Xbox text (SOTN_LANG_XBLA) when it is there, and the US, PSP English and
    Japanese boxes of disc_text.py when that file is there."""
    needles = set()

    def add(s):
        for run in re.split(r"\{[^}]*\}|\n|@", s or ""):
            run = run.strip()
            if len(run) >= 8 and sum(c.isalpha() for c in run) >= 4 and not has_cyrillic(run):
                needles.add(run)
    if XBLA and os.path.isdir(XBLA):
        for x in xbla_units():
            for field in ("en", "ja", "de", "fr", "it", "es"):
                add(x.get(field))
        for x in xbla_psp_units():
            for field in ("psp", "en_stock"):
                add(x.get(field))
    disc = load_json_opt(DISC_TEXT)
    for field in ("stock", "psp_en", "jp"):
        for s in disc.get(field, {}).values():
            add(s)
    if not needles:
        sys.exit("guard: no stock strings: set SOTN_LANG_XBLA or run tools/lang/disc_text.py")
    files = subprocess.run(["git", "-C", REPO, "diff", "--cached", "--name-only"], capture_output=True, text=True).stdout.split()
    files += [os.path.relpath(p, REPO) for p in glob.glob(os.path.join(HERE, "**", "*"), recursive=True) if os.path.isfile(p)]
    bad = []
    for rel in sorted(set(files)):
        path = os.path.join(REPO, rel)
        if not os.path.isfile(path) or "__pycache__" in rel:
            continue
        raw = open(path, "rb").read()
        if raw.startswith(b"\x89PNG"):
            continue
        text = raw.decode("utf-8", errors="replace")
        if "{0C " in text and not path.endswith("sotn_lang.py"):  # this file names the pattern itself
            bad.append((rel, "{0C "))
        hits = [n for n in needles if n in text]
        if hits:
            bad.append((rel, hits[:3]))
    bad += [("relic name cut", o) for o in relic_name_cuts()]
    for b in bad:
        print("GUARD:", b)
    print(f"guard: {len(set(files))} files, {len(needles)} stock strings, {len(bad)} problems")
    sys.exit(1 if bad else 0)


def cmd_selftest(_args):
    assert to_markup("{0C 82 D5 57 84}{02 04}Б\n{03 1C}{19}{06}") == "{}{s4}Б\n{w28}{}"
    assert enc_cells("АБ")[0] == bytes((0x21, 0x66, 0xFF))
    assert enc_dialogue("Б\n{s4}{}", [12]) == (bytes((0x81, 1, 2, 4, NATIVE_OP)), [], True)  # dfont byte
    assert enc_dialogue_en("A’…\n{s4}{}", [12]) == (b"A'...\x01\x02\x04" + bytes((NATIVE_OP,)), [], True)
    assert enc_dialogue_en("Б{}", [12])[1] == ["Б"]
    assert wrap_markup("aaaa bb{}b cc", 5) == "aaaa\nbb{}b\ncc" and wrap_markup("abcdefgh", 4) == "abcdefgh"
    assert enc_bios("Ґa і")[0] == b"\x85\x40a i\x00"
    assert enc_bios("1”")[0] == b"\x82\x50\x81\x68\x81\x68\x00"
    assert advance([0] * 15) == 1 and advance([0xFFFF] * 15) == 6
    assert sjis("−") == b"\x81\x7c" and enc_bios("a¨")[0] == b"a\x81\x68\x81\x68\x00"
    assert packed_text({"stock": " X", "font": "sjis"}, "Б")[0] == " Б"
    assert proof_level(["F_GAME.BIN @y256"]) == "placement" and proof_level(["F_GAME.BIN @y0"]) == "content"
    assert not proof_level([])
    assert len(struct.pack("<HBBIIHH", 0, 0, 0, 0, 0, 0, NO_INDEX)) == 16
    assert enc_cells("З", menu=True)[0] == bytes((Z_CELL, 0xFF)) != enc_cells("З")[0]
    # a plate: the stock ink (5) of the name plate's row 1 goes, the word's ink comes
    fake = {(bx, y): b"\x77" * 64 for bx in (768, 800, 864) for y in range(128)}
    fake[(768, 90)] = b"\x75" * 64
    plates = {r["name"]: r for r in plate_records({"F_SEL.BIN @y0": fake})}
    assert len(plates) == 3 and plates["name_plate"]["first"] == fnv(b"\x77" * 12)
    row1 = le_texels([plates["name_plate"]["le"][12:24]])[0]
    assert [c for c in range(3, 21) if row1[c] == 5] == [5 + i for i, ch in enumerate("#.#...#.#..###") if ch == "#"]
    print("selftest ok")


def main():
    cmds = {"extract": cmd_extract, "glyphs": cmd_glyphs, "join": cmd_join, "width": cmd_width,
            "art": cmd_art, "pack": cmd_pack, "vendor": cmd_vendor, "guard": cmd_guard, "selftest": cmd_selftest}
    if len(sys.argv) < 2 or sys.argv[1] not in cmds:
        sys.exit(__doc__)
    c_out = sys.argv[sys.argv.index("--c") + 1] if "--c" in sys.argv else None
    global TABLE, TABLE_INDEX
    TABLE, TABLE_INDEX = rust_table()
    cmds[sys.argv[1]]({})
    if c_out and sys.argv[1] == "pack":
        write_c(c_out, OUT)


if __name__ == "__main__":
    main()
