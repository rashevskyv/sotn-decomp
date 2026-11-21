#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Converts the Saturn-only sounds and the boss track of the Saturn stages.

Reads the owner's Saturn files (0.BIN, SD/*.CRT, SD/*.PCM) and writes, for
each stage, into the data folder (not into git):
  ST/<STAGE>/sfx/<ID>.wav    the stage-package sounds, 44100 Hz stereo,
                             rendered by tools/saturn/sound-player (saturn-sfx)
  ST/STAGE15/music/535.wav   the boss track SD2B.PCM, 44100 Hz stereo
  ST/STAGE15/music/loops.txt its loop points (the music/loops.txt format)
src/pc/saturn_sfx.c and src/pc/music.c play them. A missing file plays the
PS1 stand-in. A missing disc file is logged and skipped: exit code 0.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

LOAD = 0x06004080  # 0.BIN load address
SOUND_FILES, SOUND_FILE_COUNT, SOUND_FILE_SIZE = 0x3D20C, 57, 17
SFX_CONFIGS, SFX_FIRST = 0x3D5D5, 0x600
STAGE_FILES, STAGE_FILE_COUNT, STAGE_FILE_SIZE = 0x43798, 100, 24
STAGE_BANK = 2  # the sequence bank that the stage package holds

# Saturn ids the ported stage code plays (src/st/stage15, src/st/stage16)
STAGES = {
    "STAGE15": ("STAGE_15.PRG", [0x8F7, 0x8F8, 0x8FA, 0x8FB, 0x8FC, 0x8FD,
                                 0x8FE, 0x8FF, 0x90B, 0x90C, 0x90D, 0x90E,
                                 0x90F, 0x911, 0x912, 0x913]),
    "STAGE16": ("STAGE_16.PRG", [0x8FE, 0x8FF, 0x900, 0x901, 0x903]),
}
# music id (src/pc/saturn_sfx.c SATURN_BOSS_MUSIC) -> stage, Saturn file
MUSIC = {0x535: ("STAGE15", "SD2B.PCM")}

SECONDS = 4.0  # render length; the silent tail is cut
QUIET = 8  # |sample| at or below this counts as silence
TAIL = 441  # 10 ms kept after the last loud sample


def log(text):
    print(f"[saturn-audio] {text}", flush=True)


def be32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def sound_files(game):
    out = []
    for i in range(SOUND_FILE_COUNT):
        rec = game[SOUND_FILES + i * SOUND_FILE_SIZE:][:SOUND_FILE_SIZE]
        out.append(rec[:16].split(b"\0")[0].decode("latin-1"))
    return out


def stage_package(game, overlay):
    """The CRT the game loads with this stage overlay (0.BIN stage table)."""
    files = sound_files(game)
    for i in range(STAGE_FILE_COUNT):
        rec = STAGE_FILES + i * STAGE_FILE_SIZE
        name_at, command = be32(game, rec) - LOAD, be32(game, rec + 0x0C)
        if not 0 <= name_at < len(game) or not 0xB00 <= command < 0xB00 + len(files):
            continue
        if game[name_at:game.index(b"\0", name_at)].decode("latin-1") == overlay:
            return files[command - 0xB00]
    return None


def sfx_bank(game, sfx):
    return game[SFX_CONFIGS + (sfx - SFX_FIRST) * 5]


def trim(frames, channels):
    """Cuts the silent tail of s16 frames (bytes); None when all silent."""
    samples = struct.unpack(f"<{len(frames) // 2}h", frames)
    last = -1
    for i in range(len(samples) - 1, -1, -1):
        if abs(samples[i]) > QUIET:
            last = i // channels
            break
    if last < 0:
        return None
    end = min(len(samples) // channels, last + 1 + TAIL)
    return frames[:end * channels * 2]


def write_wav(path, frames, channels, rate=44100):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with wave.open(tmp, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(frames)
    os.replace(tmp, path)


def fresh(output, *inputs):
    return os.path.exists(output) and all(
        os.path.getmtime(output) >= os.path.getmtime(i) for i in inputs)


def convert_sfx(args, game, stage, overlay, ids, tmp):
    crt = stage_package(game, overlay)
    if crt is None:
        log(f"{stage}: {overlay} is not in the 0.BIN stage table: skipped")
        return 0
    crt_path = os.path.join(args.disc, "SD", crt)
    common = os.path.join(args.disc, "SD", "SD_JO10.CRT")
    for need in (crt_path, common):
        if not os.path.exists(need):
            log(f"{stage}: {need} missing: its Saturn sounds are not "
                "converted, the game plays the PS1 stand-ins")
            return 0
    done = 0
    for sfx in ids:
        out = os.path.join(args.data, "ST", stage, "sfx", f"{sfx:03X}.wav")
        if sfx_bank(game, sfx) != STAGE_BANK:
            log(f"{stage} {sfx:03X}: not a stage-package sound: skipped")
            continue
        if fresh(out, crt_path, args.player):
            done += 1
            continue
        raw = os.path.join(tmp, f"{stage}_{sfx:03X}.wav")
        run = subprocess.run(
            [args.player, "render", "--game-bin", os.path.join(args.disc, "0.BIN"),
             "--crt", crt_path, "--sfx", hex(sfx), "--seconds", str(SECONDS),
             "--output", raw], capture_output=True, text=True)
        if run.returncode not in (0, 3) or not os.path.exists(raw):
            log(f"{stage} {sfx:03X}: saturn-sfx failed ({run.returncode}): "
                f"{run.stderr.strip()}")
            continue
        with wave.open(raw, "rb") as w:
            channels, frames = w.getnchannels(), w.readframes(w.getnframes())
        frames = trim(frames, channels)
        if frames is None:
            log(f"{stage} {sfx:03X}: {crt} renders silence: no file")
            continue
        write_wav(out, frames, channels)
        done += 1
        log(f"{stage} {sfx:03X}: {crt} -> {out} "
            f"({len(frames) // (2 * channels) / 44100:.2f} s)")
    return done


def convert_music(args, music, stage, source, tmp):
    src = os.path.join(args.disc, "SD", source)
    out_dir = os.path.join(args.data, "ST", stage, "music")
    out = os.path.join(out_dir, f"{music:03X}.wav")
    if not os.path.exists(src):
        log(f"{stage}: {src} missing: the boss fight keeps the stage track")
        return 0
    if fresh(out, src):
        return 1
    work = os.path.join(tmp, source)
    run = subprocess.run(
        [args.cargo, "run", "--release", "--quiet", "--manifest-path",
         args.assets, "--", "audio", "extract", "stereo", src, work],
        capture_output=True, text=True)
    if run.returncode:
        log(f"{stage}: {source} decode failed: {run.stderr.strip()}")
        return 0
    with open(os.path.join(work, "manifest.json")) as f:
        manifest = json.load(f)
    frames = manifest["samples"] // 2
    loop = (manifest.get("header") or {}).get("loop_point")
    os.makedirs(out_dir, exist_ok=True)
    shutil.move(os.path.join(work, "audio.wav"), out + ".tmp")
    os.replace(out + ".tmp", out)
    # music.c: "ID start loop_start loop_end", frames at 44100 Hz
    with open(os.path.join(out_dir, "loops.txt"), "w") as f:
        if loop is not None and loop < frames:
            f.write(f"{music:03X} 0 {loop} {frames}\n")
    log(f"{stage}: {source} -> {out} ({frames / 44100:.1f} s, loop from "
        f"{'none' if loop is None else f'{loop / 44100:.2f} s'})")
    return 1


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--disc", required=True, help="Saturn files: 0.BIN, SD/")
    p.add_argument("--data", required=True, help="the game's data folder")
    p.add_argument("--player", required=True, help="saturn-sfx binary")
    p.add_argument("--cargo", default="cargo")
    p.add_argument("--assets", required=True,
                   help="tools/saturn/assets/Cargo.toml")
    args = p.parse_args()
    game_bin = os.path.join(args.disc, "0.BIN")
    if not os.path.exists(game_bin):
        log(f"{game_bin} missing: no Saturn sounds or boss track converted, "
            "the game plays the PS1 stand-ins")
        return 0
    if not os.path.exists(args.player):
        log(f"{args.player} missing: build saturn-sfx (cmake "
            "-DSATURN_STAGE_AUDIO=ON, git submodule update --init "
            "--recursive tools/saturn/sound-player)")
        return 0
    with open(game_bin, "rb") as f:
        game = f.read()
    sfx = music = 0
    with tempfile.TemporaryDirectory() as tmp:
        for stage, (overlay, ids) in STAGES.items():
            sfx += convert_sfx(args, game, stage, overlay, ids, tmp)
        for music_id, (stage, source) in MUSIC.items():
            music += convert_music(args, music_id, stage, source, tmp)
    total = sum(len(ids) for _, ids in STAGES.values())
    log(f"{sfx}/{total} sounds, {music}/{len(MUSIC)} boss tracks in {args.data}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
