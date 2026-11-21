#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""HD texture packer: PNG (the editable source) -> .hdt (GPU blocks, packed).

usage: hdtex_pack.py SRC DST [--max-scale N] [--jobs N]
  SRC, DST: folders; every <name>.png of SRC (and of its sub folders) becomes
  DST/<same path>.hdt. The game prefers a .hdt over a .png of the same key.
  --max-scale N: no picture larger than N times its VRAM rect (0 = keep).

.hdt (little endian): "PSYZHDT1", u32 format (1 BC1, 3 BC3), u16 w, u16 h,
u16 levels, u16 codec (0 raw, 1 LZ4 block), u32 raw size, u32 packed size,
then the packed data: the mip levels, largest first, 4x4 blocks row by row.
Colours are premultiplied by alpha (the game draws premultiplied), mips are
box filtered from the premultiplied picture. BC1 when the alpha is only 0 or
255 (transparent texels are index 3 of a 3-colour block), else BC3.
Needs numpy, Pillow and the lz4 module (python3-lz4).
"""
import os, re, struct, sys
import numpy as np
from PIL import Image

KEY = re.compile(r"__(\d+)_(\d+)_(\d+)x(\d+)_(\d+)_[0-9a-fA-F]{8}(_c[0-9a-fA-F]{8})?\.png$")


def premultiply(a):
    a = a.astype(np.uint32)
    rgb = (a[..., :3] * a[..., 3:4] + 127) // 255
    return np.concatenate([rgb, a[..., 3:4]], axis=-1).astype(np.uint8)


def mips(img):
    """img: HxWx4 uint8 premultiplied -> the levels down to 1x1 (box filter)"""
    out = [img]
    while img.shape[0] > 1 or img.shape[1] > 1:
        h, w = img.shape[:2]
        f = img.astype(np.uint32)
        if h > 1:
            f = f[: h // 2 * 2]
            f = f[0::2] + f[1::2]
        else:
            f = f * 2
        if w > 1:
            f = f[:, : w // 2 * 2]
            f = f[:, 0::2] + f[:, 1::2]
        else:
            f = f * 2
        img = ((f + 2) // 4).astype(np.uint8)
        out.append(img)
    return out


def blocks(img):
    """HxWx4 -> (N, 16, 4) float, edge texels repeat to whole 4x4 blocks"""
    h, w = img.shape[:2]
    ph, pw = -h % 4, -w % 4
    if ph or pw:
        img = np.pad(img, ((0, ph), (0, pw), (0, 0)), mode="edge")
    H, W = img.shape[:2]
    b = img.reshape(H // 4, 4, W // 4, 4, 4).transpose(0, 2, 1, 3, 4)
    return b.reshape(-1, 16, 4).astype(np.float32)


def to565(c):
    c = np.clip(np.rint(c), 0, 255).astype(np.int32)
    return ((c[..., 0] * 31 + 127) // 255 << 11) | ((c[..., 1] * 63 + 127) // 255 << 5) | ((c[..., 2] * 31 + 127) // 255)


def from565(v):
    r = (v >> 11) & 31
    g = (v >> 5) & 63
    b = v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.float32)


def bc1_colors(px, opaque_mode, use):
    """px (N,16,3); use (N,16) bool: texels the fit sees.
    opaque_mode (N,) bool: 4-colour block (c0 > c1); else 3-colour + transparent.
    Returns (c0, c1, indices (N,16))."""
    w = use.astype(np.float32)[..., None]
    n = np.maximum(w.sum(1), 1)
    mean = (px * w).sum(1) / n
    d = (px - mean[:, None]) * w
    cov = np.einsum("nki,nkj->nij", d, d)
    axis = np.ones((px.shape[0], 3), np.float32)
    for _ in range(8):  # power iteration: the principal axis
        axis = np.einsum("nij,nj->ni", cov, axis)
        axis /= np.maximum(np.linalg.norm(axis, axis=1, keepdims=True), 1e-6)
    t = np.einsum("nki,ni->nk", px - mean[:, None], axis)
    big = 1e9
    tmin = np.where(use, t, big).min(1)
    tmax = np.where(use, t, -big).max(1)
    tmin = np.where(tmin > tmax, 0, tmin)
    tmax = np.maximum(tmax, tmin)
    best = None
    # the first guess, then least squares endpoints for the picked indices
    e0 = mean + tmax[:, None] * axis
    e1 = mean + tmin[:, None] * axis
    for it in range(3):
        c0, c1 = to565(e0), to565(e1)
        if True:  # order the endpoints for the block mode
            swap = np.where(opaque_mode, c0 < c1, c0 > c1)
            c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
            # a 4-colour block needs c0 > c1: equal endpoints, one colour
            eq = opaque_mode & (c0 == c1)
        p0, p1 = from565(c0), from565(c1)
        four = opaque_mode & ~eq
        pal = np.stack([p0, p1,
                        np.where(four[:, None], (2 * p0 + p1) / 3, (p0 + p1) / 2),
                        np.where(four[:, None], (p0 + 2 * p1) / 3, 0)], 1)  # (N,4,3)
        dist = ((px[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)  # (N,16,4)
        dist[:, :, 3] = np.where(four[:, None], dist[:, :, 3], big)  # 3: transparent
        idx = dist.argmin(-1)
        idx = np.where(use, idx, np.where(four[:, None], idx, 3))
        idx = np.where(eq[:, None], 0, idx)
        err = np.where(use, np.take_along_axis(dist, idx[..., None], -1)[..., 0], 0).sum(1)
        if best is None:
            best = [c0, c1, idx, err]
        else:
            better = err < best[3]
            for k, v in enumerate((c0, c1, idx, err)):
                best[k] = np.where(better[:, None] if v.ndim == 2 else better, v, best[k])
        if it == 2:
            break
        # least squares: px = a * e0 + b * e1 with the weights of the indices
        wt = np.where(four[:, None], np.array([1, 0, 2 / 3, 1 / 3])[idx], np.array([1, 0, .5, 0])[idx])
        wt = np.where(use, wt, 0)
        ww = np.where(use, 1, 0)
        a2 = (wt * wt).sum(1); b2 = ((ww - wt) * (ww - wt)).sum(1); ab = (wt * (ww - wt)).sum(1)
        det = a2 * b2 - ab * ab
        ok = np.abs(det) > 1e-6
        ax = (px * wt[..., None]).sum(1); bx = (px * (ww - wt)[..., None]).sum(1)
        sdet = np.where(ok, det, 1)[:, None]
        n0 = (ax * b2[:, None] - bx * ab[:, None]) / sdet
        n1 = (bx * a2[:, None] - ax * ab[:, None]) / sdet
        # c0 holds the index-0 colour after the swap: refit from the ordered pair
        e0 = np.where(ok[:, None], n0, from565(best[0]))
        e1 = np.where(ok[:, None], n1, from565(best[1]))
    return best[0], best[1], best[2]


def pack_bc1(b):
    a = b[..., 3]
    use = a >= 128
    opaque = use.all(1)
    c0, c1, idx = bc1_colors(b[..., :3], opaque, use)
    bits = (idx.astype(np.uint64) << (2 * np.arange(16, dtype=np.uint64))).sum(1)
    out = np.zeros((b.shape[0], 8), np.uint8)
    out[:, 0] = c0 & 255; out[:, 1] = c0 >> 8; out[:, 2] = c1 & 255; out[:, 3] = c1 >> 8
    for k in range(4):
        out[:, 4 + k] = (bits >> np.uint64(8 * k)) & np.uint64(255)
    return out.tobytes()


def pack_bc3(b):
    a = b[..., 3]
    a0 = a.max(1); a1 = a.min(1)
    a0i = np.rint(a0).astype(np.int32); a1i = np.rint(a1).astype(np.int32)
    k = np.arange(8)
    w0 = np.array([7, 0, 6, 5, 4, 3, 2, 1]) / 7.0
    pal = a0i[:, None] * w0 + a1i[:, None] * (1 - w0)  # 8-level mode (a0 > a1)
    pal = np.floor(pal + 0.5)
    ai = np.abs(a[:, :, None] - pal[:, None, :]).argmin(-1)
    ai = np.where((a0i == a1i)[:, None], 0, ai)
    abits = (ai.astype(np.uint64) << (3 * np.arange(16, dtype=np.uint64))).sum(1)
    use = np.ones(a.shape, bool)
    c0, c1, idx = bc1_colors(b[..., :3], np.ones(a.shape[0], bool), use)
    bits = (idx.astype(np.uint64) << (2 * np.arange(16, dtype=np.uint64))).sum(1)
    out = np.zeros((b.shape[0], 16), np.uint8)
    out[:, 0] = a0i; out[:, 1] = a1i
    for j in range(6):
        out[:, 2 + j] = (abits >> np.uint64(8 * j)) & np.uint64(255)
    out[:, 8] = c0 & 255; out[:, 9] = c0 >> 8; out[:, 10] = c1 & 255; out[:, 11] = c1 >> 8
    for j in range(4):
        out[:, 12 + j] = (bits >> np.uint64(8 * j)) & np.uint64(255)
    return out.tobytes()


def decode(fmt, data, w, h):
    """BC1/BC3 blocks -> HxWx4 uint8 (for the quality check)"""
    bs = 8 if fmt == 1 else 16
    n = ((w + 3) // 4) * ((h + 3) // 4)
    d = np.frombuffer(data[: n * bs], np.uint8).reshape(n, bs).astype(np.uint64)
    c = d[:, bs - 8:]
    c0 = (c[:, 0] | c[:, 1] << 8).astype(np.int64); c1 = (c[:, 2] | c[:, 3] << 8).astype(np.int64)
    bits = c[:, 4] | c[:, 5] << 8 | c[:, 6] << 16 | c[:, 7] << 24
    idx = ((bits[:, None] >> (2 * np.arange(16, dtype=np.uint64))) & 3).astype(np.int64)
    p0, p1 = from565(c0), from565(c1)
    four = (c0 > c1) | (fmt == 3)
    pal = np.stack([p0, p1,
                    np.where(four[:, None], (2 * p0 + p1) / 3, (p0 + p1) / 2),
                    np.where(four[:, None], (p0 + 2 * p1) / 3, 0)], 1)
    rgb = np.take_along_axis(pal, idx[..., None].repeat(3, -1), 1)
    if fmt == 1:
        alpha = np.where(~four[:, None] & (idx == 3), 0, 255).astype(np.float32)
    else:
        a0 = d[:, 0].astype(np.float32); a1 = d[:, 1].astype(np.float32)
        ab = sum(d[:, 2 + j] << np.uint64(8 * j) for j in range(6))
        ai = ((ab[:, None] >> (3 * np.arange(16, dtype=np.uint64))) & 7).astype(np.int64)
        w0 = np.array([7, 0, 6, 5, 4, 3, 2, 1]) / 7.0
        w6 = np.array([5, 0, 4, 3, 2, 1, 0, 0]) / 5.0
        big = (a0 > a1)[:, None]
        pal8 = np.floor(a0[:, None] * w0 + a1[:, None] * (1 - w0) + 0.5)
        pal6 = np.floor(a0[:, None] * w6 + a1[:, None] * (1 - w6) + 0.5)
        pal6[:, 1] = a1; pal6[:, 6] = 0; pal6[:, 7] = 255
        pal = np.where(big, pal8, pal6)
        alpha = np.take_along_axis(pal, ai, 1)
    px = np.concatenate([rgb, alpha[..., None]], -1)
    bw, bh = (w + 3) // 4, (h + 3) // 4
    img = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return np.clip(np.rint(img), 0, 255).astype(np.uint8)[:h, :w]


def encode(img, max_scale=0, rect=None, fmt=None):
    """img: PIL image. Returns (fmt, w, h, levels, raw bytes)."""
    img = img.convert("RGBA")
    if max_scale and rect:
        tw, th = rect
        w, h = img.size
        f = min(1.0, max_scale * tw / w, max_scale * th / h)
        if f < 1.0:
            img = img.resize((max(1, round(w * f)), max(1, round(h * f))), Image.LANCZOS)
    a = premultiply(np.asarray(img))
    if fmt is None:
        al = a[..., 3]
        fmt = 1 if np.all((al == 0) | (al == 255)) else 3
    raw = b"".join((pack_bc1 if fmt == 1 else pack_bc3)(blocks(m)) for m in mips(a))
    return fmt, a.shape[1], a.shape[0], len(mips_sizes(a.shape[1], a.shape[0])), raw


def mips_sizes(w, h):
    s = [(w, h)]
    while w > 1 or h > 1:
        w, h = max(1, w // 2), max(1, h // 2)
        s.append((w, h))
    return s


def write_hdt(path, fmt, w, h, levels, raw):
    import lz4.block
    packed = lz4.block.compress(raw, mode="high_compression", compression=12, store_size=False)
    codec = 1
    if len(packed) >= len(raw):
        packed, codec = raw, 0
    with open(path, "wb") as f:
        f.write(b"PSYZHDT1" + struct.pack("<IHHHHII", fmt, w, h, levels, codec, len(raw), len(packed)))
        f.write(packed)
    return len(packed) + 28


def rect_texels(name):
    m = KEY.search(name)
    if not m:
        return None
    w, h, bpp = int(m.group(3)), int(m.group(4)), int(m.group(5))
    return w * (16 // bpp), h


def convert(job):
    src, dst, max_scale = job
    fmt, w, h, levels, raw = encode(Image.open(src), max_scale, rect_texels(os.path.basename(src)))
    n = write_hdt(dst, fmt, w, h, levels, raw)
    return src, dst, fmt, w, h, os.path.getsize(src), n


def main(argv):
    args, max_scale, jobs = [], 0, os.cpu_count() or 1
    it = iter(argv)
    for a in it:
        if a == "--max-scale":
            max_scale = float(next(it))
        elif a == "--jobs":
            jobs = int(next(it))
        else:
            args.append(a)
    if len(args) != 2:
        sys.exit(__doc__)
    src, dst = args
    work = []
    for root, _, files in os.walk(src):
        for fn in sorted(files):
            if not KEY.search(fn):
                continue
            out = os.path.join(dst, os.path.relpath(root, src), fn[:-4] + ".hdt")
            os.makedirs(os.path.dirname(out), exist_ok=True)
            work.append((os.path.join(root, fn), out, max_scale))
    from multiprocessing import Pool
    tin = tout = 0
    with Pool(jobs) as p:
        for s, d, fmt, w, h, a, b in p.imap(convert, work):
            tin += a; tout += b
            print("%s: BC%d %dx%d, %d KB -> %d KB" % (os.path.relpath(d, dst), fmt, w, h, a >> 10, b >> 10))
    print("%d files: %d KB -> %d KB" % (len(work), tin >> 10, tout >> 10))


def selftest():
    rng = np.random.default_rng(1)
    g = np.zeros((13, 10, 4), np.uint8)
    g[..., 0] = np.linspace(0, 255, 10)[None]; g[..., 1] = 128; g[..., 2] = rng.integers(0, 255, (13, 10))
    for alpha, fmt in ((255, 1), (None, 3)):
        g[..., 3] = 255 if alpha else np.linspace(0, 255, 13)[:, None]
        if fmt == 1:
            g[:3, :3, 3] = 0
        f, w, h, lv, raw = encode(Image.fromarray(g))
        assert (f, w, h, lv) == (fmt, 10, 13, 4), (f, w, h, lv)
        back = decode(f, raw, w, h).astype(int)
        ref = premultiply(g).astype(int)
        assert np.abs(back[..., 3] - ref[..., 3]).max() <= 20, "alpha"
        assert np.abs(back[..., :3] - ref[..., :3]).mean() < 24, "colour"
    print("selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    else:
        main(sys.argv[1:])
