#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build the voice packs of the native port from the owner's PSP image.

    python3 -I extract_voice.py --iso DXC.iso \
        --jp manifest_voice_jp.json --psp manifest_voice_pspen.json --out DIR

Writes DIR/voice/jp/<ID>.pcm and DIR/voice/psp/<ID>.pcm: raw s16le, mono,
37800 Hz, no header. <ID> is the PS1 US sound id in 3 upper hex digits
(358.pcm). The game reads the length from the file size.

The manifests map each PS1 US id to an .at3 clip in the image (byte offset,
size, SHA-256). They hold game file names and hashes, so they stay outside
the repository. The image is only read. Needs ffmpeg with the atrac3p decoder.
Copy DIR/voice to sdmc:/switch/sotn-native/voice on the SD card.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

RATE = 37800
BYTES_PER_S = RATE * 2


def load_clips(path):
    clips = json.load(open(path, encoding="utf-8"))["clips"]
    ids = [int(c["ps1_us_id"], 16) for c in clips]
    if len(ids) != len(set(ids)):
        sys.exit(f"{path}: an id is in the manifest more than once")
    for i in ids:
        if not 0x358 <= i <= 0x534:
            sys.exit(f"{path}: id {i:#x} is not a voice id")
    return clips


def extract(iso, clip, out_dir, ffmpeg, tmp):
    name = f"{int(clip['ps1_us_id'], 16):03X}"
    with open(iso, "rb") as f:
        f.seek(clip["offset"])
        data = f.read(clip["size"])
    if hashlib.sha256(data).hexdigest() != clip["sha256"]:
        return name, f"SHA-256 differs ({clip['source_file']})"
    at3 = os.path.join(tmp, name + ".at3")
    with open(at3, "wb") as f:
        f.write(data)
    dst = os.path.join(out_dir, name + ".pcm")
    cmd = [ffmpeg, "-v", "error", "-y", "-i", at3, "-ac", "1", "-ar", str(RATE)]
    if clip.get("gain_db"):
        cmd += ["-af", f"volume={clip['gain_db']}dB"]
    cmd += ["-f", "s16le", "-acodec", "pcm_s16le", dst]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        return name, "ffmpeg: " + r.stderr.strip()[-200:]
    size = os.path.getsize(dst)
    ms = size * 1000 // BYTES_PER_S
    if size % 2 or abs(ms - clip["duration_ms"]) > 100:
        return name, f"length {ms} ms, manifest {clip['duration_ms']} ms"
    return name, None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--iso", required=True, help="PSP image (ISO), read only")
    ap.add_argument("--jp", required=True, help="manifest_voice_jp.json")
    ap.add_argument("--psp", required=True, help="manifest_voice_pspen.json")
    ap.add_argument("--out", required=True, help="output folder")
    ap.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    a = ap.parse_args()
    if not a.ffmpeg:
        sys.exit("ffmpeg not found")

    bad = 0
    for set_name, manifest in (("jp", a.jp), ("psp", a.psp)):
        clips = load_clips(manifest)
        out_dir = os.path.join(a.out, "voice", set_name)
        os.makedirs(out_dir, exist_ok=True)
        with tempfile.TemporaryDirectory() as tmp, \
                concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
            jobs = [pool.submit(extract, a.iso, c, out_dir, a.ffmpeg, tmp)
                    for c in clips]
            errors = [j.result() for j in jobs]
        errors = [(n, e) for n, e in errors if e]
        for n, e in errors:
            print(f"{set_name}/{n}.pcm: {e}")
        total = sum(os.path.getsize(os.path.join(out_dir, f))
                    for f in os.listdir(out_dir) if f.endswith(".pcm"))
        print(f"{set_name}: {len(clips) - len(errors)} of {len(clips)} clips ok,"
              f" {total} bytes, {total / BYTES_PER_S:.1f} s -> {out_dir}")
        bad += len(errors)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
