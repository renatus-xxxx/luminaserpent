#!/usr/bin/env python3
"""Build a Tablacus DISK ROM Lite cartridge image (.ROM) for
LUMINA SERPENT from the patched DISKROM64K.BIN and a game disk.

Usage: make_rom_lite.py DISKROM64K.BIN TEMPLATE.DSK LUMINA.COM \
           OUT.ROM [DATA ...]

How it works
------------
The ROM is the 8 KB DISK ROM Lite system (ASCII 8K bank) followed
by a 360 KB game disk:

    cat DISKROM64K.BIN GAME360.DSK > LUMINA-LITE.ROM   (376,832 B)

The game disk is a 360 KB (1DD) FAT12 image in dos2formsx layout.
Its boot sector is inherited from the dos2formsx template vendored
in tools/tablacus/ (DOS2 mode), with only the BPB geometry fields
rewritten for 360 KB (TotSec16=720, Media=F8, Heads=1; FATSz16
stays 3).  DISK ROM Lite reads the BPB to determine the geometry,
so a self-consistent 360 KB image is recognised as-is.  COMMAND.COM
and DOS.SYS are extracted from the template, then LUMINA.COM, the
data files and an AUTOEXEC.BAT containing "LUMINA" are added in
that order with the FAT12 add-or-replace below (adapted from the
romlab verification tooling).

At 376,832 bytes the image fits a 512 KB (4 Mbit) cartridge such
as the MSX Simple Mega Flash (ASCII 8K), so one and the same image
serves the WebMSX play link and real-hardware burning.  The written
file is padded with 0xFF (the flash erased state) up to exactly
524,288 bytes, ready for flashing.  The .dsk distribution is
unaffected and stays 720 KB (2DD).

The DISKROM64K.BIN must be the locally patched build: stock
v0.7.4.4 hangs when COMMAND loads a .COM of 11,303 bytes or more
(the LDIR_PAGE1 termination bug - see tools/tablacus/README.md
and tools/ROM-NOTES.md).  This tool warns if the system binary
does not match the known patched MD5.

Cartridge type for emulation: ASCII 8K.  This exact layout was
verified end to end in openMSX (real-BIOS FS-A1GT, ASCII8):
power-on auto-boot through title into the demo, up to the boss
fight.
"""
import hashlib
import os
import struct
import sys

PATCHED_BIN_MD5 = "7743f77fa0d635611e413432158315f7"
AUTOEXEC = b"LUMINA\r\n"

# 360 KB (1DD) geometry written into the inherited boot sector.
DISK_BYTES = 368640
DISK_TOTSEC = 720
DISK_MEDIA = 0xF8
DISK_HEADS = 1
DISK_SPF = 3

# Output is padded with 0xFF (flash erased state) up to this size,
# the same cartridge-size alignment tools/make_rom.py applies.
CART_BYTES = 524288


class Fat12Disk:
    """Minimal FAT12 add-or-replace on an MSX disk image."""

    def __init__(self, img: bytearray):
        self.img = img
        self.bps = struct.unpack_from("<H", img, 11)[0]
        self.spc = img[13]
        self.res = struct.unpack_from("<H", img, 14)[0]
        self.nf = img[16]
        self.root_entries = struct.unpack_from("<H", img, 17)[0]
        self.spf = struct.unpack_from("<H", img, 22)[0]
        self.fat_size = self.spf * self.bps
        self.fat_offs = [(self.res + i * self.spf) * self.bps
                         for i in range(self.nf)]
        self.root_off = (self.res + self.nf * self.spf) * self.bps
        self.data_off = self.root_off + self.root_entries * 32
        self.cs = self.spc * self.bps
        self.fat = bytearray(img[self.fat_offs[0]:
                                 self.fat_offs[0] + self.fat_size])

    def fget(self, c):
        o = c + c // 2
        if c % 2 == 0:
            return self.fat[o] | ((self.fat[o + 1] & 0x0F) << 8)
        return (self.fat[o] >> 4) | (self.fat[o + 1] << 4)

    def fset(self, c, v):
        o = c + c // 2
        if c % 2 == 0:
            self.fat[o] = v & 0xFF
            self.fat[o + 1] = (self.fat[o + 1] & 0xF0) | ((v >> 8) & 0x0F)
        else:
            self.fat[o] = (self.fat[o] & 0x0F) | ((v << 4) & 0xF0)
            self.fat[o + 1] = (v >> 4) & 0xFF

    def chain(self, s):
        out = []
        c = s
        while 2 <= c < 0xFF8:
            out.append(c)
            c = self.fget(c)
        return out

    def find(self, raw):
        for i in range(self.root_entries):
            o = self.root_off + i * 32
            if self.img[o] == 0x00:
                break
            if self.img[o] == 0xE5:
                continue
            if bytes(self.img[o:o + 11]) == raw:
                return o
        return None

    def extract(self, name83):
        base, ext = name83.upper().split(".")
        raw = base.ljust(8).encode() + ext.ljust(3).encode()
        slot = self.find(raw)
        assert slot is not None, name83
        start = struct.unpack_from("<H", self.img, slot + 26)[0]
        size = struct.unpack_from("<I", self.img, slot + 28)[0]
        out = bytearray()
        for c in self.chain(start):
            s = self.data_off + (c - 2) * self.cs
            out += self.img[s:s + self.cs]
        return bytes(out[:size])

    def add(self, name83, data):
        base, ext = name83.upper().split(".")
        raw = base.ljust(8).encode() + ext.ljust(3).encode()
        slot = self.find(raw)
        if slot is not None:
            old = struct.unpack_from("<H", self.img, slot + 26)[0]
            for c in self.chain(old):
                self.fset(c, 0)
        else:
            for i in range(self.root_entries):
                o = self.root_off + i * 32
                if self.img[o] in (0x00, 0xE5):
                    slot = o
                    break
        need = max(1, (len(data) + self.cs - 1) // self.cs)
        free = [c for c in range(2, 2 + (len(self.img) - self.data_off)
                                 // self.cs) if self.fget(c) == 0]
        assert len(free) >= need, name83
        alloc = free[:need]
        for i, c in enumerate(alloc):
            self.fset(c, alloc[i + 1] if i + 1 < need else 0xFFF)
            s = self.data_off + (c - 2) * self.cs
            self.img[s:s + self.cs] = \
                data[i * self.cs:(i + 1) * self.cs].ljust(self.cs, b"\x00")
        self.img[slot:slot + 32] = b"\x00" * 32
        self.img[slot:slot + 11] = raw
        self.img[slot + 11] = 0x20
        struct.pack_into("<H", self.img, slot + 26, alloc[0])
        struct.pack_into("<I", self.img, slot + 28, len(data))
        print("added", name83, len(data))

    def finish(self):
        for fo in self.fat_offs:
            self.img[fo:fo + self.fat_size] = self.fat
        return bytes(self.img)


def build_game_disk(template_img, com, data_files):
    """Assemble the 360 KB game disk from the 720 KB template."""
    tpl = Fat12Disk(bytearray(template_img))
    command_com = tpl.extract("COMMAND.COM")
    dos_sys = tpl.extract("DOS.SYS")

    img = bytearray(DISK_BYTES)
    img[0:512] = template_img[0:512]       # boot sector (dos2formsx)
    struct.pack_into("<H", img, 19, DISK_TOTSEC)   # BPB TotSec16
    img[21] = DISK_MEDIA                           # BPB Media
    struct.pack_into("<H", img, 26, DISK_HEADS)    # BPB Heads
    struct.pack_into("<H", img, 22, DISK_SPF)      # BPB FATSz16
    bps = struct.unpack_from("<H", img, 11)[0]
    res = struct.unpack_from("<H", img, 14)[0]
    for i in range(img[16]):               # FAT media descriptor
        fo = (res + i * DISK_SPF) * bps
        img[fo] = DISK_MEDIA
        img[fo + 1] = 0xFF
        img[fo + 2] = 0xFF

    disk = Fat12Disk(img)
    if not (disk.bps == 512 and disk.spc == 2
            and disk.root_entries == 112 and disk.spf == DISK_SPF):
        raise ValueError("unexpected 360 KB geometry: bps=%d spc=%d "
                         "root=%d spf=%d" % (disk.bps, disk.spc,
                                             disk.root_entries,
                                             disk.spf))
    disk.add("COMMAND.COM", command_com)
    disk.add("DOS.SYS", dos_sys)
    for path in [com] + list(data_files):
        disk.add(os.path.basename(path).upper(),
                 open(path, "rb").read())
    disk.add("AUTOEXEC.BAT", AUTOEXEC)
    return disk.finish()


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 1
    sys_bin, template, com, out = sys.argv[1:5]
    data_files = sys.argv[5:]

    romsys = open(sys_bin, "rb").read()
    if len(romsys) != 8192:
        print("error: DISKROM64K.BIN must be 8192 bytes", file=sys.stderr)
        return 1
    md5 = hashlib.md5(romsys).hexdigest()
    if md5 != PATCHED_BIN_MD5:
        print("warning: system binary MD5 %s != patched build %s"
              % (md5, PATCHED_BIN_MD5), file=sys.stderr)
        print("warning: a stock v0.7.4.4 binary cannot load "
              "LUMINA.COM (11,303-byte bug)", file=sys.stderr)

    template_img = open(template, "rb").read()
    if len(template_img) != 737280:
        print("error: template must be a 737280-byte DSK", file=sys.stderr)
        return 1
    game_dsk = build_game_disk(template_img, com, data_files)
    if len(game_dsk) != DISK_BYTES:
        print("error: game disk is %d bytes, expected %d"
              % (len(game_dsk), DISK_BYTES), file=sys.stderr)
        return 1

    rom = romsys + game_dsk
    if len(rom) < CART_BYTES:
        rom += b"\xff" * (CART_BYTES - len(rom))
    with open(out, "wb") as fh:
        fh.write(rom)
    print("game disk MD5", hashlib.md5(game_dsk).hexdigest())
    print("wrote", out, len(rom), "bytes, MD5",
          hashlib.md5(rom).hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main())
