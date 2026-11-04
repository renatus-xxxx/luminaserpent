#!/usr/bin/env python3
"""Build an LSX-Dodgers cartridge image (.ROM) for LUMINA SERPENT
from the bootable floppy image (.DSK).

Usage: make_rom.py LDMSX.BIN GO.COM LUMINA.DSK LUMINA.ROM [--max-rom N]

How it works
------------
The ROM is the 16 KB LSX-Dodgers standalone system (LDMSX.BIN,
ASCII 8K bank) followed by the disk image, exactly as in the
upstream `copy /b LDMSX.BIN + ROM.DSK LDMSX.ROM` recipe
(LSX-Dodgers v1.62c, MIT License, Copyright (c) 1995 Gaku).

Two adjustments are made to the disk image on the way in:

  * GO.COM (a 304-byte self-relocating loader, sources in
    tools/lsx/) is added, and AUTOEXEC.BAT is rewritten to "GO".
    The LSX command processor cannot start a ~49 KB program from
    AUTOEXEC.BAT (it fails with "Error!"), although the same
    program loads fine when typed at the prompt; GO.COM loads
    LUMINA.COM itself through BDOS FCB reads and jumps to it.
    Its payload runs at C300h, above the game image end (C227h)
    and below the LSX kernel base (BDOS entry is CB06h).
  * The floppy image is 720 KB, which would need a 1 MB
    cartridge, but the files on it occupy only the first ~120 KB:
    the disk tools always allocate the lowest free FAT clusters
    first.  This tool parses the FAT12, finds the end of the
    last *used* cluster (boot sector, FATs and root directory
    included) and cuts the image there.  It then picks the
    smallest cartridge size from 128K/256K/512K/1M/2M that holds
    system + cut image and pads the tail with 0xFF (erased
    flash state).

The BPB inside the image still describes the full 720 KB disk.
That is safe for this read-only use: every file's data, the FATs
and the root directory lie below the cut (this tool verifies the
FAT chains), and the clusters beyond the cut are all marked
free, so neither LSX-Dodgers nor the game ever reads them.  If a
future build grows files past a cartridge size, this tool fails
loudly instead of producing a silently truncated ROM.

Default --max-rom is 512 KB, the capacity of the MSX Simple Mega
Flash cartridge (4 Mbit, SST39SF040) this ROM is written to.

History: the Tablacus DISK ROM Lite route (tools/tablacus/) was
tried first and abandoned - its 64 KB Ese-DOS build cannot load
a .COM whose load range crosses 4000h (7,264 B works, 16,384 B
hangs), and LUMINA.COM is 49,463 B.  See tools/ROM-NOTES.md.
"""
import struct
import sys

CART_SIZES = [128 * 1024, 256 * 1024, 512 * 1024,
              1024 * 1024, 2048 * 1024]


class Fat12:
    def __init__(self, img: bytearray):
        self.img = img
        self.bps = struct.unpack_from("<H", img, 11)[0]
        self.spc = img[13]
        self.reserved = struct.unpack_from("<H", img, 14)[0]
        self.num_fats = img[16]
        self.root_entries = struct.unpack_from("<H", img, 17)[0]
        total_sec = struct.unpack_from("<H", img, 19)[0]
        if total_sec == 0:
            total_sec = struct.unpack_from("<I", img, 32)[0]
        self.total_sec = total_sec
        self.spf = struct.unpack_from("<H", img, 22)[0]
        self.fat_size = self.spf * self.bps
        self.fat_offs = [(self.reserved + i * self.spf) * self.bps
                         for i in range(self.num_fats)]
        self.root_off = (self.reserved +
                         self.num_fats * self.spf) * self.bps
        self.data_off = self.root_off + self.root_entries * 32
        self.clus_size = self.spc * self.bps
        self.max_cluster = (2 + (self.total_sec * self.bps -
                                 self.data_off) // self.clus_size)
        self.fat = bytearray(img[self.fat_offs[0]:
                                 self.fat_offs[0] + self.fat_size])

    def get(self, c):
        off = c + c // 2
        if c % 2 == 0:
            return self.fat[off] | ((self.fat[off + 1] & 0x0F) << 8)
        return (self.fat[off] >> 4) | (self.fat[off + 1] << 4)

    def set(self, c, v):
        off = c + c // 2
        if c % 2 == 0:
            self.fat[off] = v & 0xFF
            self.fat[off + 1] = ((self.fat[off + 1] & 0xF0) |
                                 ((v >> 8) & 0x0F))
        else:
            self.fat[off] = ((self.fat[off] & 0x0F) |
                             ((v << 4) & 0xF0))
            self.fat[off + 1] = (v >> 4) & 0xFF

    def chain(self, first):
        out, c = [], first
        while 2 <= c < 0xFF8:
            out.append(c)
            c = self.get(c)
        return out

    def find(self, name83):
        base, ext = name83.upper().split(".")
        raw = base.ljust(8).encode() + ext.ljust(3).encode()
        for i in range(self.root_entries):
            off = self.root_off + i * 32
            if self.img[off] == 0x00:
                break
            if self.img[off] == 0xE5:
                continue
            if bytes(self.img[off:off + 11]) == raw:
                return off
        return None

    def add(self, name83, data):
        """Add or replace a root-directory file."""
        base, ext = name83.upper().split(".")
        raw = base.ljust(8).encode() + ext.ljust(3).encode()
        slot = self.find(name83)
        if slot is not None:
            old = struct.unpack_from("<H", self.img, slot + 26)[0]
            for c in self.chain(old):
                self.set(c, 0)
        else:
            for i in range(self.root_entries):
                off = self.root_off + i * 32
                if self.img[off] in (0x00, 0xE5):
                    slot = off
                    break
            else:
                raise RuntimeError("root directory full")
        cs = self.clus_size
        need = max(1, (len(data) + cs - 1) // cs)
        free = [c for c in range(2, self.max_cluster)
                if self.get(c) == 0]
        if len(free) < need:
            raise RuntimeError(f"disk full adding {name83}")
        alloc = free[:need]
        for i, c in enumerate(alloc):
            self.set(c, alloc[i + 1] if i + 1 < need else 0xFFF)
            start = self.data_off + (c - 2) * cs
            self.img[start:start + cs] = \
                data[i * cs:(i + 1) * cs].ljust(cs, b"\x00")
        self.img[slot:slot + 32] = b"\x00" * 32
        self.img[slot:slot + 11] = raw
        self.img[slot + 11] = 0x20
        struct.pack_into("<H", self.img, slot + 26, alloc[0])
        struct.pack_into("<I", self.img, slot + 28, len(data))

    def flush_fats(self):
        for off in self.fat_offs:
            self.img[off:off + self.fat_size] = self.fat

    def remove(self, name83):
        """Delete a file (entry marked 0xE5, clusters freed).
        compact_root() drops the entry for good afterwards."""
        slot = self.find(name83)
        if slot is None:
            return False
        first = struct.unpack_from("<H", self.img, slot + 26)[0]
        for c in self.chain(first):
            self.set(c, 0)
        self.img[slot] = 0xE5
        return True

    def compact_root(self):
        """Rewrite the root directory with no deleted (0xE5) entries.

        The floppy image built by make_dsk.py carries deleted
        entries left over from template file replacement, and some
        live files (AUTOEXEC.BAT, LUMINA.COM) sit behind them.
        LSX-Dodgers' FCB file search stops at the first deleted
        entry, so those files cannot be opened from the ROM even
        though DIR lists them.  Compacting the directory (live
        entries first, in their original order, rest zeroed)
        fixes the ROM image; cluster allocation is untouched.
        """
        live = []
        for i in range(self.root_entries):
            off = self.root_off + i * 32
            if self.img[off] == 0x00:
                break
            if self.img[off] == 0xE5:
                continue
            live.append(bytes(self.img[off:off + 32]))
        size = self.root_entries * 32
        self.img[self.root_off:self.root_off + size] = b"\x00" * size
        for i, entry in enumerate(live):
            off = self.root_off + i * 32
            self.img[off:off + 32] = entry
        return len(live)


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    max_rom = 512 * 1024
    if "--max-rom" in sys.argv:
        max_rom = int(sys.argv[sys.argv.index("--max-rom") + 1], 0)
    if len(args) != 4:
        print("usage: make_rom.py LDMSX.BIN GO.COM LUMINA.DSK "
              "LUMINA.ROM [--max-rom N]", file=sys.stderr)
        return 1
    sys_path, go_path, dsk_path, out_path = args

    system = open(sys_path, "rb").read()
    if len(system) != 16384 or system[:2] != b"AB":
        print(f"{sys_path}: not the 16 KB LDMSX.BIN system image",
              file=sys.stderr)
        return 1
    go = open(go_path, "rb").read()

    img = bytearray(open(dsk_path, "rb").read())
    fs = Fat12(img)
    fs.add("GO.COM", go)
    fs.add("AUTOEXEC.BAT", b"GO\r\n")
    # The floppy's own DOS files are dead weight in the ROM (LSX
    # brings its own command processor), and worse: on a turboR
    # (FS-A1GT) the machine's MSX-DOS2 IPL picks the volume's
    # DOS.SYS up and hangs.  The author's sample disk carries no
    # DOS files and boots everywhere.  Drop them.
    dropped = [n for n in ("DOS.SYS", "COMMAND.COM") if fs.remove(n)]
    fs.flush_fats()
    kept = fs.compact_root()
    print(f"injected GO.COM loader; AUTOEXEC.BAT now runs GO; "
          f"dropped {dropped}; "
          f"root directory compacted to {kept} live entries")

    # End of the last used cluster; system area is always included.
    used_end = fs.data_off
    used_clusters = 0
    for c in range(2, fs.max_cluster):
        if fs.get(c) != 0:
            used_clusters += 1
            end = fs.data_off + (c - 1) * fs.clus_size
            if end > used_end:
                used_end = end
    if used_end > len(img):
        print("FAT references clusters past the end of the image",
              file=sys.stderr)
        return 1

    # Report the files and check every chain stays below the cut.
    print(f"disk image: {len(img)} bytes, {used_clusters} clusters "
          f"used, file data ends at {used_end}")
    for i in range(fs.root_entries):
        off = fs.root_off + i * 32
        if img[off] == 0x00:
            break
        if img[off] == 0xE5:
            continue
        name = bytes(img[off:off + 11]).decode("ascii", "replace")
        size = struct.unpack_from("<I", img, off + 28)[0]
        first = struct.unpack_from("<H", img, off + 26)[0]
        chain = fs.chain(first)
        top = chain[-1] if chain else 0
        print(f"  {name} size {size:6d} clusters "
              f"{chain[0] if chain else '-'}..{top if chain else '-'}")
        if chain and fs.data_off + (top - 1) * fs.clus_size > used_end:
            print("internal error: chain above used_end",
                  file=sys.stderr)
            return 1

    rom_size = None
    for cand in CART_SIZES:
        if len(system) + used_end <= cand:
            rom_size = cand
            break
    if rom_size is None or rom_size > max_rom:
        print(f"needs a {rom_size} byte cartridge, over the "
              f"{max_rom} byte limit (--max-rom)", file=sys.stderr)
        return 1

    rom = bytearray(b"\xff" * rom_size)
    rom[0:len(system)] = system
    rom[len(system):len(system) + used_end] = img[:used_end]
    open(out_path, "wb").write(rom)
    print(f"wrote {out_path} ({rom_size} bytes = LDMSX {len(system)}"
          f" + DSK cut at {used_end}, rest 0xFF padding)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
