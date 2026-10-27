[日本語版はこちら](./README.ja.md)

# luminaserpent

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

- [Play on WebMSX (MSX turboR)](https://webmsx.org/?MACHINE=MSXTRJ&DISKA_URL=https://raw.githubusercontent.com/renatus-xxxx/luminaserpent/main/dist/LUMINA.DSK&FAST_BOOT)
- [LUMINA.DSK](dist/LUMINA.DSK) — bootable 720 KB disk image (starts automatically)
- [LUMINA.COM](dist/LUMINA.COM) — the MSX-DOS program on its own

Current build (SCREEN 8 edition): `LUMINA.COM` 50,282 bytes,
MD5 `1e28b405fded17ce102b5ae0c2f01cfb`.

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
- **SCREEN 8 (GRAPHIC 7) graphics in 256 colours (GRB332)** —
  both Earths (title and in-game) are 256-colour renders with
  dithered shading, streamed from data files on the disk
  (`TPL8.DAT` / `GPL8.DAT`) because the .COM sits at the
  MSX-DOS memory ceiling. The backdrop is the "Deep Field":
  a dense two-layer procedural star field over a smooth
  vertical gradient.
- **Sprite colours on GRAPHIC 7** — in this mode the VDP reads
  sprite colour codes through its fixed built-in table rather
  than the programmable palette, so sprite palette indices are
  mapped to the nearest fixed-table colours to preserve the
  colour scheme of the earlier SCREEN 5 build.
- **Fast WARNING restore** — when the boss WARNING panel is
  erased, the Earth behind it is restored from a backup copy
  held in VRAM (about 0.06 s) instead of being re-read from
  disk (about 4.3 s).
- **Split screen** — a line-interrupt keeps the HUD (score and
  RESONANCE gauge) and the equipment band fixed while the
  playfield in between does its work.
- **PSG music and effects**, with separate scenes for the
  title, the stage and the boss.

## Building

```
make -f Makefile.linux        # SCREEN 8 edition (default)
make -f Makefile.linux dsk    # + the bootable disk image
```

Requires z88dk (classic library, `+msx` target). The default
build defines `-DNOSCROLL -DSCR8 -DSCR8LIN` and produces
`LUMINA.COM`; `tools/make_dsk.py` packs it into the bootable
disk image using a template MSX-DOS disk, adding the SCREEN 8
Earth data files (`data/tpl8.dat`, `data/gpl8.dat`). The
legacy SCREEN 5 build (v1.8 feature set) is still available as
`make -f Makefile.linux scr5`.

## Notes

- Requires MSX2 or higher (SCREEN 8, i.e. V9938 or later).
  MSX1 is not supported.
- Sound is PSG only.
