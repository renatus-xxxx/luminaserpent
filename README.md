[日本語版はこちら](./README.ja.md)

# luminaserpent-msx

**LUMINA SERPENT** is an original side-scrolling shooter for
**MSX2 / MSX2+ / MSX turboR**, written in C with z88dk and shipped
as a bootable MSX-DOS disk image. It is the MSX counterpart of the
X68000 game of the same name — same design, re-implemented for
the V9938/V9958.

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

## Play / Download

- [Play on WebMSX (MSX2+)](https://webmsx.org/?MACHINE=MSX2PJ&DISKA_URL=https://raw.githubusercontent.com/renatus-xxxx/luminaserpent-msx/main/dist/LUMINA.DSK&FAST_BOOT)
- [LUMINA.DSK](dist/LUMINA.DSK) — bootable 720 KB disk image (starts automatically)
- [LUMINA.COM](dist/LUMINA.COM) — the MSX-DOS program on its own

Current build (v1.8): `LUMINA.COM` 49,922 bytes,
MD5 `2b697ee2656054d2d0ebbf5ef1772a88`.

## Controls

| input | action |
|---|---|
| Cursor keys / joystick | move |
| SPACE / trigger | shot (hold for the serpent laser) |
| ESC (in game) | back to title |
| no input on title (~5 s) | attract demo |

## About the implementation

- **One binary for every machine** — the program detects
  MSX2 / MSX2+ / turboR at startup and switches to the R800
  only on turboR. No BASIC or disk setup needed: the disk
  boots straight into the game.
- **Split screen** — a line-interrupt keeps the HUD (score and
  RESONANCE gauge) and the equipment band fixed while the
  playfield in between does its work.
- **Nothing is ever dropped** — the game tracks up to 80
  logical sprites and windows them onto the 32 hardware
  planes in rotation, so in a crowded boss fight everything
  is drawn and everything flickers fairly, instead of the
  last-registered sprites silently vanishing.
- **PSG music and effects**, with separate scenes for the
  title, the stage and the boss.
- Demo benchmark: about 48 fps on MSX turboR.

## Building

```
make -f Makefile.linux
```

Requires z88dk (classic library, `+msx` target). The build
produces `LUMINA.COM`; `tools/make_dsk.py` packs it into the
bootable disk image using a template MSX-DOS disk.

## Notes

- Requires MSX2 or higher (SCREEN 5). MSX1 is not supported.
- Sound is PSG only.
