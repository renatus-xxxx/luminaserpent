#!/usr/bin/env python3
"""Deep Field background spec for the SCREEN 8 build (candidate A,
user-selected 2026-10-27) and generator for src/deepfield.h.

The SCR8 draw_slice() used to paint four flat horizontal bands, a
checker-dithered purple nebula band and one hashed star per 16x16
cell. Deep Field replaces that (SCR8 only; SCREEN 5 is untouched):

- background: a smooth vertical gradient (deep blue-black) whose
  GRB332 steps are hidden by a 4x4 Bayer dither. The gradient is a
  function of screen y only and the dither phase of (x&3, y&3), so
  the whole background of any 8-dot slice is one precomputed table
  (df_bg, 168 rows x 4 phases) - the runtime renders that table
  once into a spare-VRAM strip and HMMM-copies it per slice.
- stars: two hashed layers in world coordinates (same 16-bit
  cell_hash as the game), a faint 8x8-cell layer and a mid/bright
  16x16-cell layer with plus sparkles on the brightest tier.
  ~600 stars per 256x168 view (was ~174). Colours carry a colour
  temperature mix (white/ice/blue/warm); the tables below are the
  GRB332 bytes the candidate mock used.
- the purple nebula is gone.

This module is the single source of the spec: the C table in
src/deepfield.h is generated here, and tools/gen_title_art.py
imports bg_byte() for the Earth rects' backdrop so the baked
.dat corners match the slice background byte for byte.

Integer spec (shared by the table and the backdrop):
  gradient: piecewise-linear between GRAD stops, per channel
    v = (c0*(y1-y) + c1*(y-y0) + (y1-y0)//2) // (y1-y0)
  quantize with Bayer value b in 0..15 (strength 0.65 as in the
  candidate mock):
    r7 = (R*448  + (b-8)*663  + 8160)  // 16320   (clamp 0..7)
    g7 = (G*448  + (b-8)*663  + 8160)  // 16320   (clamp 0..7)
    b3 = (B*1920 + (b-8)*4641 + 81600) // 163200  (clamp 0..3)
  (exact rational forms of round(V*7/255 + (b/16-1/2)*0.65) and
   round(V*3/255 + (b/16-1/2)*0.65*0.7), rounded half up.)
Usage: python3 tools/gen_deepfield.py   (writes src/deepfield.h)
"""
import os

PLAY_Y0, PLAY_Y1 = 16, 183

BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6],
          [3, 11, 1, 9], [15, 7, 13, 5]]

# gradient stops: (screen y, (r, g, b) 0..255) - candidate A's GRAD_A
GRAD = [(16, (2, 6, 27)), (80, (3, 8, 33)),
        (140, (2, 6, 25)), (183, (2, 5, 22))]

# star colour tables by kind (0 white, 1 ice, 2 blue, 3 warm),
# tier 0 faint / 1 mid / 2 bright - candidate mock's star_byte()
STAR0 = [0x69, 0x6A, 0x46, 0x6D]
STAR1 = [0xB6, 0xAF, 0x8B, 0x95]
STAR2 = [0xFF, 0xCF, 0xAF, 0xBD]


def cell_hash(cx, cy):
    """The game's 16-bit hash (z88dk unsigned int arithmetic)."""
    h = (cx * 40503 + cy * 22697) & 0xFFFF
    h ^= (h << 3) & 0xFFFF
    h ^= h >> 5
    return h


def grad_at(y):
    """(r, g, b) integers 0..255 of the vertical gradient at y."""
    for k in range(len(GRAD) - 1):
        y0, c0 = GRAD[k]
        y1, c1 = GRAD[k + 1]
        if y0 <= y <= y1:
            d = y1 - y0
            return tuple((c0[i] * (y1 - y) + c1[i] * (y - y0)
                          + (d >> 1)) // d for i in range(3))
    return GRAD[-1][1]


def _clamp(v, hi):
    return 0 if v < 0 else (hi if v > hi else v)


def bg_byte(x, y):
    """Deep Field background byte (gradient + Bayer) at screen
    (x, y). No stars - this is also the Earth rects' backdrop."""
    r, g, bch = grad_at(y)
    b = BAYER4[y & 3][x & 3]
    r7 = _clamp((r * 448 + (b - 8) * 663 + 8160) // 16320, 7)
    g7 = _clamp((g * 448 + (b - 8) * 663 + 8160) // 16320, 7)
    b3 = _clamp((bch * 1920 + (b - 8) * 4641 + 81600) // 163200, 3)
    return (g7 << 5) | (r7 << 2) | b3


def bg_table():
    """df_bg rows: for each play row, the byte per x&3 phase."""
    return [[bg_byte(j, y) for j in range(4)]
            for y in range(PLAY_Y0, PLAY_Y1 + 1)]


def star_pixels(wx):
    """All Deep Field star pixels of the 8-dot column at world x
    wx, as (world_x, y, byte) in drawing order (faint layer first,
    then mid/bright - the mock's order, so later pixels win).
    Mirrors the C code in draw_slice() one to one."""
    out = []
    # faint layer: 8x8 world cells (y cells are 7 px tall, cy 0..23)
    for cx in range(wx >> 3, ((wx + 7) >> 3) + 1):
        for cy in range(24):
            h = cell_hash(cx + 61, cy + 7)
            sx = cx * 8 + 1 + (h % 6)
            if sx < wx or sx >= wx + 8:
                continue
            sy = PLAY_Y0 + cy * 7 + ((h >> 4) % 5) - 2
            if sy < PLAY_Y0 or sy > PLAY_Y1:
                continue
            if (h % 100) > 62:
                continue
            sel = (h >> 6) % 100
            kind = 0 if sel < 45 else 1 if sel < 75 else 2 if sel < 92 else 3
            out.append((sx, sy, STAR0[kind]))
    # mid/bright layer: 16x16 world cells
    for cx in range(wx >> 4, ((wx + 7) >> 4) + 1):
        for cy in range(12):
            h = cell_hash(cx + 1031, cy + 40)
            sx = cx * 16 + 1 + (h % 13)
            sy = cy * 16 + 2 + ((h >> 4) & 7) + 8
            if sy < PLAY_Y0 or sy > PLAY_Y1:
                continue
            if (h % 100) > 85:
                continue
            sel = (h >> 6) % 100
            kind = 1 if sel < 35 else 0 if sel < 70 else 2 if sel < 90 else 3
            bright = 2 if (h % 23) == 0 else 1
            wdt = 2 if (h & 0x200) else 1
            b = STAR2[kind] if bright == 2 else STAR1[kind]
            pts = [(sx, sy, b)]
            if wdt == 2:
                pts.append((sx + 1, sy, b))
            if bright == 2:
                s1 = STAR1[kind]
                pts.append((sx - 1, sy, s1))
                pts.append((sx + wdt, sy, s1))
                if sy - 1 >= PLAY_Y0:
                    pts.append((sx, sy - 1, s1))
                if sy + 1 <= PLAY_Y1:
                    pts.append((sx, sy + 1, s1))
            for px, py, pb in pts:
                if wx <= px < wx + 8:
                    out.append((px, py, pb))
    return out


def render_background(world_x0=0, w=256):
    """Full play-area background (gradient + stars) for the world
    window starting at world_x0, as a (168, w) uint8 array of
    GRB332 bytes - the reference the game's VRAM is checked
    against."""
    import numpy as np
    img = np.zeros((PLAY_Y1 - PLAY_Y0 + 1, w), np.uint8)
    for i in range(w):
        for y in range(PLAY_Y0, PLAY_Y1 + 1):
            img[y - PLAY_Y0, i] = bg_byte(world_x0 + i, y)
    for lx in range(0, w, 8):
        for sx, sy, b in star_pixels(world_x0 + lx):
            img[sy - PLAY_Y0, sx - world_x0] = b
    return img


def main():
    tab = bg_table()
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, "..", "src", "deepfield.h")
    with open(path, "w") as f:
        f.write("/* generated by tools/gen_deepfield.py - do not edit.\n")
        f.write("   Deep Field background table for the SCR8 build:\n")
        f.write("   df_bg[row][x&3] = GRB332 byte of the gradient+Bayer\n")
        f.write("   background at play row (PLAY_Y0 + row). */\n")
        f.write("#ifndef DEEPFIELD_H\n#define DEEPFIELD_H\n\n")
        f.write("static const unsigned char df_bg[168][4] = {\n")
        for r, row in enumerate(tab):
            f.write("  {%s},%s\n" % (
                ",".join("0x%02x" % v for v in row),
                "  /* y=%d */" % (PLAY_Y0 + r) if r % 8 == 0 else ""))
        f.write("};\n\n#endif\n")
    print("wrote", path)


if __name__ == "__main__":
    main()
