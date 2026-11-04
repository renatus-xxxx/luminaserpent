# Tablacus DISK ROM Lite (locally patched build)

Vendored system software for the `make rom-lite` cartridge build.
See `tools/ROM-NOTES.md` for the full investigation log.

## Origin

- Tablacus DISK ROM Lite v0.7.4.4 (GitHub release, 2026-09-08)
  by Gaku — https://github.com/tablacus/TablacusDiskRomLite
- License: MIT (see `LICENSE` in this directory,
  Copyright (c) 2023 Gaku). The original copyright notice is kept
  unchanged.
- Only the 64 KB version (`DISKROM64K.BIN`, ASCII 8K bank) is
  vendored: LUMINA SERPENT is an MSX-DOS .COM program, so the ROM
  must boot Ese-DOS (dosformsx) from the disk image, which requires
  the 64 KB version.

## The bug and the local patch

Stock v0.7.4.4 cannot finish a file read whose destination reaches
page 1 (4000h): the termination check of `LDIR_PAGE1` in FILE.ASM
is written inside a `#if exists` nested in another `#if exists`,
and the AILZ80ASM assembler does not expand the nested form, so
both branches are missing from the release binary. Under the
pseudo DOS, COMMAND stages a program at 13DAh, so any .COM of
11,303 bytes or more hangs on load (11,302 bytes works).

The patch touches FILE.ASM only: the termination code is defined
once, outside the nest, as `LDIR_PAGE1_FIN`, and the two call
sites jump to it with `JP Z`. See `FILE.ASM.patch`.

- Upstream pull request (same change):
  https://github.com/tablacus/TablacusDiskRomLite/pull/3
  (branch `fix/ldir-page1-termination`, commit `2b2954a`)
- This vendored copy exists so the ROM can be built locally even
  if the pull request is not merged upstream.

## Files

| File | MD5 | Notes |
| --- | --- | --- |
| `DISKROM64K.BIN` | `7743f77fa0d635611e413432158315f7` | **Patched** build (8 KB) |
| (stock v0.7.4.4 `DISKROM64K.BIN`) | `c66f43fca66a2b916ef05fc9580871bf` | Not vendored; listed to tell the two apart |
| `DOS2FORMSX.DSK` | `a0f47b3f7675d3223dd79ef633387660` | Game-disk template (dosformsx, DOS2 mode) already containing COMMAND.COM / DOS.SYS |
| `FILE.ASM.patch` | — | The source fix (v0.7.4.4 → patched) |
| `LICENSE` | — | Original MIT license |
| `VERSION.txt` | — | Version / cartridge notes |

The patched binary was verified in openMSX (C-BIOS MSX2+ and a
real-BIOS FS-A1GT): the 11,302 / 11,303-byte boundary probes both
run, LUMINA.COM (49,463 B) starts directly from COMMAND, and the
full attract loop (title → boss → title) completes.

## 日本語要約

DISK ROM Lite v0.7.4.4 には、読み込み先がページ1 (4000h) に達する
ファイル転送が終了できない不具合があり、擬似DOSでは 11,303 バイト
以上の .COM が起動できません。ここに置いた `DISKROM64K.BIN` は、
FILE.ASM の終了処理 (LDIR_PAGE1_FIN) だけを直した自前パッチ版で、
同じ修正は上流へ PR #3 として提出済みです。PR が取り込まれない
場合でも ROM を自前でビルドできるよう、同梱しています。
