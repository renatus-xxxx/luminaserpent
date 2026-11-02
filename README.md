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

The background scrolls on MSX2+ / MSX turboR (V9958). On
MSX2 (V9938) the background stays fixed, and the game itself
is identical.

## Play / Download

- [Play on WebMSX (MSX turboR)](https://webmsx.org/?MACHINE=MSXTRJ&DISKA_URL=https://raw.githubusercontent.com/renatus-xxxx/luminaserpent/main/dist/LUMINA.DSK&FAST_BOOT)
- [LUMINA.DSK](dist/LUMINA.DSK) — bootable 720 KB disk image (starts automatically)
- [LUMINA.COM](dist/LUMINA.COM) — the MSX-DOS program on its own

Current build :`LUMINA.COM` 50,658 bytes,
MD5 `dc7a4c9fbd259d0c12f5efda36f9aff3`;
`LUMINA.DSK` MD5 `92ce3e48f7db2ef757e45ce7b4320999`.

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
- On V9958 (MSX2+ / turboR) the background scrolls in
  hardware. On V9938 (MSX2) the background stays fixed
  (the game itself is identical).
- Sound is PSG only.
