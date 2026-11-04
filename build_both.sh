#!/bin/sh
# Build both deliverables of LUMINA SERPENT (MSX) in one go:
#   build-linux/LUMINA.DSK - 720 KB floppy image (day-to-day checks)
#   build-linux/LUMINA.ROM - cartridge image for MSX Simple Mega Flash
#                            (ASCII 8K mapper, Tablacus DISK ROM Lite)
# The same two Python tools (tools/make_dsk.py, tools/make_rom.py)
# also run on Windows; only the zcc invocation differs there.
set -e
cd "$(dirname "$0")"
make -f Makefile.linux both
ls -l build-linux/LUMINA.DSK build-linux/LUMINA.ROM
md5sum build-linux/LUMINA.DSK build-linux/LUMINA.ROM
