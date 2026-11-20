#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build sotn-native from the disc images in games/.

    python3 build.py [--pc-only | --switch-only] [--clean] [--install-deps]

Put your disc images in games/ (any file names). The result:
  out/sd/  copy its contents to the root of the Switch SD card
  out/pc/  the PC (Linux) build; start it with out/pc/run.sh (or run.bat on Windows)
Log: out/build.log. A second run skips the steps that are done.
Runs on Linux or WSL 2 (Debian or Ubuntu).
"""
import argparse
import glob
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import urllib.request
import zlib

ROOT = os.path.dirname(os.path.abspath(__file__))
GAMES = os.path.join(ROOT, "games")
DISKS = os.path.join(ROOT, "disks")
OUT = os.path.join(ROOT, "out")
WORK = os.path.join(ROOT, "build", "auto")  # stamps, SDL-switch, pc, switch
LOG = os.path.join(OUT, "build.log")
SD_DIR = os.path.join(OUT, "sd", "switch", "sotn-native")
DKP = os.environ.get("DEVKITPRO", "/opt/devkitpro")
SDL_PATCH = "tools/sdl-switch/0001-switch-SetWindowSize-uses-the-pending-size.patch"
UK_ART = "ps1vram_uk.bin"  # the Xbox 360 uk mod's labels with stock pixels (optional)
PSP_IMAGES = []  # every PSP image of The Dracula X Chronicles in games/, any release
# the clip lists of tools/voicepack/extract_voice.py (optional, not in git: game file names, hashes)
VOICE_MANIFESTS = ("manifest_voice_jp.json", "manifest_voice_pspen.json")
# the PSP files the build reads (Maria, her voice, the familiar names)
PSP_FILES = [f"PSP_GAME/USRDIR/res/ps/{f}" for f in
             ("PSPBIN/dra.bin", "PSPBIN/maria.bin", "PSPBIN/cen.bin", "snd_e/SE/sd_mar2.spk")]
# version: (disc id, name, what it adds)
WANT = {
    "us": ("SLUS-00067", "Castlevania: Symphony of the Night, PlayStation, USA", "the game (required)"),
    "pspeu": ("ULES-00841", "Castlevania: The Dracula X Chronicles, PSP, Europe", "Maria, PSP familiar names"),
    "saturn": ("T-9527G", "Akumajou Dracula X: Gekka no Yasoukyoku, Sega Saturn, Japan",
               "Saturn stages, character select art"),
}
KIND = {"us": "PlayStation", "pspeu": "PSP", "saturn": "Sega Saturn"}
# the cue sheet that switch.c writes on every boot: music from data/SD/XA_STR1
XA_CUE = 'FILE "data/SD/XA_STR1" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n'
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


class Failed(Exception):
    pass


# ------------------------------------------------------------------ disc images

class Disc:
    """The ISO 9660 files of a .bin (2352-byte sectors), .iso or .cso image."""

    def __init__(self, path):
        self.f = open(path, "rb")
        head = self.f.read(24)
        self.size, self.skip, self.cso = 2048, 0, None
        if head[:4] == b"CISO":
            total, block = struct.unpack_from("<QI", head, 8)
            if block != 2048:
                raise OSError("CSO block size is not 2048")
            n = -(-total // block)
            self.cso = struct.unpack(f"<{n + 1}I", self.f.read(4 * (n + 1))), head[21]
        elif head[:12] == SYNC:
            self.size, self.skip = 2352, 24 if head[15] == 2 else 16

    def sector(self, lba):
        if self.cso:
            index, align = self.cso
            a, b = index[lba] & 0x7FFFFFFF, index[lba + 1] & 0x7FFFFFFF
            self.f.seek(a << align)
            data = self.f.read((b - a) << align)
            return data[:2048] if index[lba] & 0x80000000 else zlib.decompress(data, -15)
        self.f.seek(lba * self.size + self.skip)
        return self.f.read(2048)

    def read(self, lba, length):
        return b"".join(self.sector(lba + i) for i in range(-(-length // 2048)))[:length]

    def ls(self, lba, length):
        """{name: (lba, length, is_dir)} of one directory."""
        data, i, out = self.read(lba, length), 0, {}
        while i < len(data):
            if data[i] == 0:  # records do not cross sectors
                i = (i // 2048 + 1) * 2048
                continue
            name = data[i + 33:i + 33 + data[i + 32]]
            if name not in (b"\0", b"\1"):
                key = name.decode("latin-1").split(";")[0].rstrip(".")
                out[key] = (struct.unpack_from("<I", data, i + 2)[0],
                            struct.unpack_from("<I", data, i + 10)[0], bool(data[i + 25] & 2))
            i += data[i]
        return out

    def root(self):
        pvd = self.sector(16)
        if pvd[1:6] != b"CD001":
            return {}
        return self.ls(struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0])

    def find(self, path):
        entry, folder = None, self.root()
        for part in path.split("/"):
            entry = {k.lower(): v for k, v in folder.items()}.get(part.lower())
            if not entry:
                raise Failed(f"{path} is not on the disc")
            if entry[2]:
                folder = self.ls(entry[0], entry[1])
        return entry

    def copy(self, entry, dest):
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        lba, length, _ = entry
        with open(dest, "wb") as f:
            for i in range(-(-length // 2048)):
                f.write(self.sector(lba + i)[:length - i * 2048])

    def copy_all(self, dest, folder=None):
        for name, entry in (self.root() if folder is None else folder).items():
            if entry[2]:
                self.copy_all(os.path.join(dest, name), self.ls(entry[0], entry[1]))
            else:
                self.copy(entry, os.path.join(dest, name))


def identify(path):
    """(version, disc id) of an image, or (None, None)."""
    try:
        disc = Disc(path)
        first = disc.sector(0)
        if first.startswith(b"SEGA SEGASATURN"):
            return "saturn", first[0x20:0x2A].decode("latin-1").strip()
        root = disc.root()
        if "UMD_DATA.BIN" in root:
            return "pspeu", disc.read(*root["UMD_DATA.BIN"][:2]).split(b"|")[0].decode("latin-1")
        if "SYSTEM.CNF" in root:
            m = re.search(rb"([A-Z]{4})_(\d{3})\.(\d\d)", disc.read(*root["SYSTEM.CNF"][:2]))
            return "us", m and "%s-%s%s" % tuple(g.decode() for g in m.groups())
    except (OSError, struct.error, IndexError, zlib.error, Failed):
        pass
    return None, None


def scan_games():
    found = {}
    for path in sorted(glob.glob(os.path.join(GAMES, "**", "*"), recursive=True)):
        name, ext = os.path.relpath(path, ROOT), os.path.splitext(path)[1].lower()
        if os.path.isdir(path) or ext in (".cue", ".txt", ".md", ".sha1", ".md5"):
            continue
        if os.path.basename(path).lower() == UK_ART:
            say(f"  {name}: the Ukrainian labels of the Xbox 360 mod")
            continue
        if ext in (".chd", ".pbp", ".7z", ".zip", ".rar", ".ecm", ".cdi", ".mdf", ".nrg"):
            say(f"  {name}: unpack or convert it to .bin/.cue or .iso first. Skipped.")
            if ext == ".chd":
                say("    CHD: sudo apt install mame-tools; chdman extractcd -i <file>.chd -o <file>.cue")
            continue
        version, disc_id = identify(path)
        if version == "pspeu":  # any PSP release: its cutscene text and voice clips (disc_text)
            PSP_IMAGES.append(path)
        if version is None:
            if ext != ".bin":  # a .bin without files is an audio track: say nothing
                say(f"  {name}: not a disc image this build knows. Skipped.")
            continue
        want, title, _ = WANT[version]
        if disc_id != want:
            say(f"  {name}: {KIND[version]} disc {disc_id or '(no id)'}. "
                f"Needed: {title} ({want}). Skipped"
                + (" for Maria; its cutscene text and voices are used." if version == "pspeu" else "."))
        elif version in found:
            say(f"  {name}: a second {want} image. Skipped.")
        else:
            say(f"  {name}: {want}, {title}")
            found[version] = path
    return found


# ------------------------------------------------------------------ helpers

def say(text):
    print(text, flush=True)
    with open(LOG, "a") as log:
        log.write(text + "\n")


def run(cmd, show=False, **kw):
    with open(LOG, "a") as log:
        log.write("$ " + " ".join(str(c) for c in cmd) + "\n")
        log.flush()
        out = None if show else log
        r = subprocess.run(cmd, cwd=kw.pop("cwd", ROOT), stdout=out, stderr=out,
                           stdin=None if show else subprocess.DEVNULL, **kw)
    if r.returncode:
        raise Failed(f"exit code {r.returncode}: {' '.join(str(c) for c in cmd)}")


def stamp_ok(name, key):
    try:
        return open(os.path.join(WORK, "stamps", name)).read() == key
    except OSError:
        return False


def stamp(name, key):
    os.makedirs(os.path.join(WORK, "stamps"), exist_ok=True)
    with open(os.path.join(WORK, "stamps", name), "w") as f:
        f.write(key)


def link_tree(src, dst):
    """Copy a folder as hard links (no extra disk space), over the files in dst."""
    for base, _, files in os.walk(src):
        out = os.path.join(dst, os.path.relpath(base, src))
        os.makedirs(out, exist_ok=True)
        for name in files:
            target = os.path.join(out, name)
            if os.path.lexists(target):
                os.remove(target)
            try:
                os.link(os.path.join(base, name), target)
            except OSError:
                shutil.copy2(os.path.join(base, name), target)


def write(path, text, mode=0o644):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        f.write(text)
    os.chmod(path, mode)


def setup_path():
    """cargo, go and devkitPro are often not on the PATH of a non-login shell."""
    extra = [os.path.expanduser("~/.cargo/bin")]
    gos = sorted(glob.glob("/usr/lib/go-1.*/bin"), key=lambda p: int(re.search(r"1\.(\d+)", p)[1]))
    extra += gos[-1:] + ["/usr/local/go/bin", os.path.join(DKP, "tools", "bin")]
    os.environ["PATH"] = os.pathsep.join(extra + [os.environ.get("PATH", "")])
    if os.path.isdir(DKP):
        os.environ.setdefault("DEVKITPRO", DKP)
    # a clone on a Windows drive belongs to another user for git: trust it
    os.environ.update(GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="safe.directory", GIT_CONFIG_VALUE_0="*")


def version_of(cmd, pattern):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True).stdout
        m = re.search(pattern, out)
        return tuple(int(x) for x in m.groups()) if m else None
    except OSError:
        return None


# ------------------------------------------------------------------ steps

def check_tools(args, pc, switch):
    """[(what is missing, apt package)], [extra install commands]"""
    apt, extra = [], []
    for cmd, pkg in (("git", "git"), ("make", "make"), ("gcc", "gcc"), ("g++", "g++"),
                     ("ninja", "ninja-build"), ("xxd", "xxd"), ("pkg-config", "pkg-config")):
        if not shutil.which(cmd):
            apt.append((cmd, pkg))
    if (version_of(["cmake", "--version"], r"(\d+)\.(\d+)") or (0,)) < (3, 28):
        apt.append(("cmake 3.28 or later", "cmake"))
    if (version_of(["go", "version"], r"go1\.(\d+)") or (0,)) < (24,):
        apt.append(("go 1.24 or later", "golang-1.24-go"))
    if not version_of(["cargo", "--version"], r"cargo (\d+)"):
        if not shutil.which("rustup"):
            apt.append(("cargo (Rust)", "rustup"))
        extra.append(["rustup", "default", "stable"])
    try:
        import numpy, yaml  # noqa: F401
        from PIL import features
        if not (features.check("freetype2") and features.check("raqm")):
            raise ImportError
    except ImportError:
        apt += [("Python Pillow with FreeType and raqm, PyYAML, numpy", p)
                for p in ("python3-pil", "python3-yaml", "python3-numpy", "libraqm0")]
    if pc:  # the X11, OpenGL and audio headers that SDL3 needs
        for header, pkg in (("X11/Xlib.h", "libx11-dev"), ("X11/extensions/Xext.h", "libxext-dev"),
                            ("X11/extensions/Xrandr.h", "libxrandr-dev"),
                            ("X11/Xcursor/Xcursor.h", "libxcursor-dev"),
                            ("X11/extensions/XInput2.h", "libxi-dev"),
                            ("X11/extensions/scrnsaver.h", "libxss-dev"), ("GL/gl.h", "libgl-dev"),
                            ("EGL/egl.h", "libegl-dev"), ("wayland-client.h", "libwayland-dev"),
                            ("xkbcommon/xkbcommon.h", "libxkbcommon-dev"),
                            ("alsa/asoundlib.h", "libasound2-dev"), ("pulse/pulseaudio.h", "libpulse-dev")):
            if not os.path.exists("/usr/include/" + header):
                apt.append((header, pkg))
    dkp = []
    if switch:
        for path, pkg in (("cmake/Switch.cmake", "switch-dev"), ("libnx/lib/libnx.a", "switch-dev"),
                          ("portlibs/switch/include/EGL/egl.h", "switch-mesa"),
                          ("portlibs/switch/lib/libdrm_nouveau.a", "switch-libdrm_nouveau")):
            if not os.path.exists(os.path.join(DKP, path)) and pkg not in dkp:
                dkp.append(pkg)
    if not (apt or dkp or extra):
        return
    missing = sorted({w for w, _ in apt} | set(dkp) | ({"Rust toolchain"} if extra else set()))
    say("Missing tools: " + ", ".join(missing))
    cmds = []
    if apt:
        cmds += [["sudo", "apt-get", "update"],
                 ["sudo", "apt-get", "install", "-y"] + sorted({p for _, p in apt})]
    cmds += extra
    if dkp and not shutil.which("dkp-pacman"):
        cmds.append(["devkitPro pacman: see https://devkitpro.org/wiki/devkitPro_pacman"])
    if dkp:
        cmds.append(["sudo", "dkp-pacman", "-S", "--needed", "--noconfirm"] + dkp)
    if not args.install_deps:
        say("Install them with:")
        for c in cmds:
            say("  " + " ".join(c))
        say("Or run: python3 build.py --install-deps")
        if dkp:
            say("No Switch tools? python3 build.py --pc-only builds only the PC version.")
        sys.exit(1)
    for c in cmds:
        if c[0].startswith("devkitPro"):  # the official installer of devkitPro pacman (Debian/Ubuntu)
            script = os.path.join(WORK, "install-devkitpro-pacman")
            os.makedirs(WORK, exist_ok=True)
            urllib.request.urlretrieve("https://apt.devkitpro.org/install-devkitpro-pacman", script)
            run(["sudo", "bash", script], show=True)
        else:
            run(c, show=True)
    setup_path()


def check_clone():
    if not os.path.isdir(os.path.join(ROOT, ".git")):
        raise Failed("this folder is not a git clone (a ZIP download has no submodules)")
    with open(os.path.join(ROOT, "sotn.sh"), "rb") as f:
        if b"\r\n" in f.read():
            raise Failed("this clone has Windows line ends. Clone it again with: "
                         "git clone -c core.autocrlf=false --recurse-submodules -b switch "
                         "https://github.com/rashevskyv/sotn-native.git")
    run(["git", "submodule", "update", "--init", "--recursive"])


def extract_discs(found):
    for version, path in found.items():
        st = os.stat(path)
        key = f"{os.path.realpath(path)} {st.st_size} {int(st.st_mtime)}"
        if stamp_ok("disc-" + version, key):
            continue
        dest = os.path.join(DISKS, version)
        shutil.rmtree(dest, ignore_errors=True)
        say(f"  extracting {os.path.basename(path)} -> disks/{version}")
        if version == "us":  # sotn-disk keeps the raw XA and STR sectors
            run(["make", "bin/sotn-disk"])
            run([os.path.join(ROOT, "bin", "sotn-disk"), "extract", path, dest])
        elif version == "pspeu":
            disc = Disc(path)
            for rel in PSP_FILES:
                disc.copy(disc.find(rel), os.path.join(dest, rel))
        else:
            Disc(path).copy_all(dest)
        stamp("disc-" + version, key)
    if not os.path.exists(os.path.join(DISKS, "us", "DRA.BIN")):
        raise Failed("no PlayStation disc. Put the .bin (and .cue) of "
                     f"{WANT['us'][1]} ({WANT['us'][0]}) into games/")


def extract_psp_text():
    """The PSP image -> disks/text/disc_text.json (the English PSP and Japanese cutscene text for
    the script tables, tools/lang/disc_text.py) and, with the two manifests in games/, disks/voice/
    (the JP and PSP voice clips). Without them the voice sets jp and psp keep the US voice and the
    English PSP text stays stock."""
    if not PSP_IMAGES:
        say("   no PSP image: no English PSP text, no JP and PSP voice clips")
        return
    path = PSP_IMAGES[0]
    st = os.stat(path)
    key = f"{os.path.realpath(path)} {st.st_size} {int(st.st_mtime)}"
    text = os.path.join(DISKS, "text", "disc_text.json")
    if not (stamp_ok("disc-text", key) and os.path.exists(text)):
        run([sys.executable, "tools/lang/disc_text.py", "--psp", path, "--out", text])
        stamp("disc-text", key)
    manifests = [os.path.join(GAMES, m) for m in VOICE_MANIFESTS]
    if not all(os.path.exists(m) for m in manifests):
        say(f"   no {' and '.join(VOICE_MANIFESTS)} in games/: no JP and PSP voice clips")
        return
    if stamp_ok("voice", key) and os.path.isdir(os.path.join(DISKS, "voice", "voice")):
        return
    try:
        run([sys.executable, "-I", "tools/voicepack/extract_voice.py", "--iso", path,
             "--jp", manifests[0], "--psp", manifests[1], "--out", os.path.join(DISKS, "voice")])
        stamp("voice", key)
    except Failed as e:  # ffmpeg without atrac3p, a clip that differs: the build goes on
        say(f"   voice clips: {e} (see {LOG}); the voice sets jp and psp keep the US voice")


def extract_assets():
    try:  # the assets follow the PS1 disc
        key = open(os.path.join(WORK, "stamps", "disc-us")).read()
    except OSError:
        key = "disks/us extracted by hand"
    if stamp_ok("assets", key) and os.path.isdir(os.path.join(ROOT, "assets")):
        return
    run(["make", "extract", "VERSION=us"])  # assets/
    run(["make", "build_assets", "VERSION=us"])  # src/**/gen/
    stamp("assets", key)


def sdl_switch():
    if os.environ.get("PSYZ_SDL_DIR"):
        return os.environ["PSYZ_SDL_DIR"]
    sdl = os.path.join(WORK, "SDL-switch")
    patch = open(os.path.join(ROOT, SDL_PATCH)).read()
    if stamp_ok("sdl-switch", patch) and os.path.isdir(sdl):
        return sdl
    shutil.rmtree(sdl, ignore_errors=True)
    run(["git", "clone", "-q", "--depth", "1", "-b", "switch-sdl-3.4",
         "https://github.com/devkitPro/SDL.git", sdl])
    run(["git", "-C", sdl, "apply", os.path.join(ROOT, SDL_PATCH)])
    stamp("sdl-switch", patch)
    return sdl


def cmake(name, options):
    build = os.path.join(WORK, name)
    key = "\n".join(options)
    if not (stamp_ok("cmake-" + name, key) and os.path.exists(os.path.join(build, "build.ninja"))):
        run(["cmake", "-S", ROOT, "-B", build, "-G", "Ninja"] + options)
        stamp("cmake-" + name, key)
    run(["cmake", "--build", build])
    return build


def assemble(build, dest, program, saturn):
    shutil.rmtree(os.path.join(dest, "data"), ignore_errors=True)
    link_tree(os.path.join(build, "data"), os.path.join(dest, "data"))
    if saturn:  # the stitched Saturn rooms replace their PS1 files
        link_tree(os.path.join(build, "saturn_stitches", "disks", "us", "ST"),
                  os.path.join(dest, "data", "ST"))
    shutil.copy2(os.path.join(build, program), os.path.join(dest, program))
    write(os.path.join(dest, "sotn-xa.cue"), XA_CUE)
    if os.path.isdir(os.path.join(DISKS, "voice", "voice")):
        link_tree(os.path.join(DISKS, "voice", "voice"), os.path.join(dest, "voice"))


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--pc-only", action="store_true", help="build only the PC version")
    p.add_argument("--switch-only", action="store_true", help="build only the Switch NRO")
    p.add_argument("--clean", action="store_true", help="delete out/ and build/auto/ first")
    p.add_argument("--install-deps", action="store_true", help="install missing tools with sudo")
    args = p.parse_args()
    if args.pc_only and args.switch_only:
        p.error("--pc-only and --switch-only exclude each other")
    pc, switch = not args.switch_only, not args.pc_only
    if args.clean:
        shutil.rmtree(OUT, ignore_errors=True)
        shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(GAMES, exist_ok=True)
    open(LOG, "w").close()
    setup_path()

    current, start = "tools", time.time()

    def step(name, fn, *a):
        nonlocal current
        current, t = name, time.time()
        say(f"== {name}")
        result = fn(*a)
        say(f"   {name}: {time.time() - t:.0f} s")
        return result

    try:
        step("tools", check_tools, args, pc, switch)
        step("submodules", check_clone)
        say("== discs in games/")
        found = scan_games()
        step("extract discs", extract_discs, found)
        step("extract assets", extract_assets)
        step("extract PSP text and voice", extract_psp_text)
        saturn = all(os.path.exists(os.path.join(DISKS, "saturn", f)) for f in ("0.BIN", "GAME.PRG"))
        maria = all(os.path.exists(os.path.join(DISKS, "pspeu", f)) for f in PSP_FILES)
        for version, flag in (("pspeu", maria), ("saturn", saturn)):
            if not flag:
                say(f"   no {WANT[version][0]} disc: built without {WANT[version][2]}")
        uk_art = sorted(glob.glob(os.path.join(GAMES, "**", UK_ART), recursive=True))
        if not uk_art:
            say(f"   no games/{UK_ART}: 10 Ukrainian labels (file select, name plate, HUD) stay English")
        on = {True: "ON", False: "OFF"}
        common = ["-DENABLE_ASAN=OFF", "-DPSYZ_RENDERER=sdl3_gl",
                  f"-DENABLE_SATURN_STAGES={on[saturn]}", f"-DUSE_PLAYER_MARIA={on[maria]}"]
        common.append("-DSOTN_LANG_MEDIA=" + (os.path.dirname(uk_art[0]) if uk_art else ""))
        if pc:
            build = step("build pc", cmake, "pc", common + ["-DPSYZ_STATIC_MODULES=ON", "-DSOTN_PC_DATA_DIR=data"])
            dest = os.path.join(OUT, "pc")
            step("assemble out/pc", assemble, build, dest, "sotn", saturn)
            write(os.path.join(dest, "run.sh"), '#!/bin/sh\n# settings: sotn.ini, saves: bu00/ and bu10/ in this folder\n'
                  'cd "$(dirname "$0")" && exec ./sotn --disk sotn-xa.cue --ini sotn.ini "$@"\n', 0o755)
            if os.environ.get("WSL_DISTRO_NAME"):
                write(os.path.join(dest, "run.bat"), "@echo off\r\nrem Starts the PC build in WSL; WSLg shows its window.\r\n"
                      f'wsl -d {os.environ["WSL_DISTRO_NAME"]} --cd "%~dp0." ./run.sh %*\r\n')
        if switch:
            sdl = step("SDL for Switch", sdl_switch)
            build = step("build switch", cmake, "switch", common + [
                f"-DCMAKE_TOOLCHAIN_FILE={DKP}/cmake/Switch.cmake", "-DCMAKE_BUILD_TYPE=Release",
                f"-DPSYZ_SDL_DIR={sdl}"])
            step("assemble out/sd", assemble, build, SD_DIR, "sotn.nro", saturn)
    except (Failed, OSError, subprocess.SubprocessError) as e:
        with open(LOG, "a") as log:
            log.write(f"FAILED at step '{current}': {e}\n")
        print(f"FAILED at step '{current}'. The log: {LOG}", flush=True)
        sys.exit(1)
    say(f"Done in {time.time() - start:.0f} s.")
    if switch:
        say(f"  Switch: copy the contents of {os.path.join(OUT, 'sd')} to the root of the SD card.")
    if pc:
        say(f"  PC: {os.path.join(OUT, 'pc', 'run.sh')}")


if __name__ == "__main__":
    main()
