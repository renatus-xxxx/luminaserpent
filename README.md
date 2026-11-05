[日本語版はこちら](./README.ja.md)

# luminaserpent

**LUMINA SERPENT** is an original side-scrolling shooter for
**MSX2 / MSX2+ / MSX turboR**, written in C with z88dk and shipped
as a bootable MSX-DOS disk image. It is the MSX version of the
identically titled X68000 work, reimplementing the same game
design.

![LUMINA SERPENT preview — title, the Earth stage and the boss fight](preview.gif)

Your ship drags a **serpent laser** behind it: the beam is drawn
from a history of your past positions, so it trails and bends as
you move. The more it curves, the faster the **RESONANCE** gauge
fills — fill it completely and the beam becomes a devastating
**PRISM BURST**. Fallen enemies drop equipment that cycles
through the original set — **THRUST / COMET / DUAL / PRISM /
ECHO / HALO** — and at the end of the stage the
**ABYSS PRISM LEVIATHAN** waits in front of the Earth, its core
shielded by four orbiting shards.

The background scrolls on MSX turboR only. On MSX2+ and
MSX2 the background stays fixed, and the game itself is
identical.

## Play / Download

- [Play on WebMSX (ROM, MSX turboR)](https://webmsx.org/?MACHINE=MSXTRJ&P=NODISK&CARTRIDGE1_URL=https://raw.githubusercontent.com/renatus-xxxx/luminaserpent/main/dist/LUMINA-LITE.ROM)
- [Play on WebMSX (Disk, MSX turboR)](https://webmsx.org/?MACHINE=MSXTRJ&DISKA_URL=https://raw.githubusercontent.com/renatus-xxxx/luminaserpent/main/dist/LUMINA.DSK&FAST_BOOT)
- [LUMINA.DSK](dist/LUMINA.DSK) — bootable 720 KB disk image (starts automatically)
- [LUMINA.COM](dist/LUMINA.COM) — the MSX-DOS program on its own

Current build :`LUMINA.COM` 49,463 bytes,
MD5 `5d3200d958d4f1605f0759b79b2f1681`;
`LUMINA.DSK` MD5 `a154abe2001776b90a253a965e1667de`.

## Controls

| input | action |
|---|---|
| Cursor keys / joystick | move |
| SPACE / trigger | shot (hold for the serpent laser) |
| ESC (in game) | back to title |
| no input on title (~5 s) | attract demo |

## Building

```
make -f Makefile.linux        # default
make -f Makefile.linux dsk    # + the bootable disk image
```

Requires z88dk (classic library, `+msx` target). The default
build produces `LUMINA.COM`; `tools/make_dsk.py` packs it
into the bootable disk image using a template MSX-DOS disk,
adding the data files (`data/tpl8.dat`, `data/gpl8.dat`,
`data/gplf8.dat`, `data/logo.dat`, `data/spark.dat`).

## Notes

- Requires MSX2 or higher (SCREEN 8, i.e. V9938 or later).
  MSX1 is not supported.
- On MSX turboR the background scrolls in hardware.
  On MSX2+ and MSX2 the background stays fixed
  (the game itself is identical).
- Sound is PSG only.

## Development

This project is built and verified with assistance from Muse, Meta's personal AI agent — including build automation, emulator testing, and documentation.

If you're curious what a personal agent can do, try Muse here: https://muse.ai/join — enter my invite code **RARMB6** under Settings > Redeem within 48 hours of joining, and we'll both get bonus tokens.
