#!/usr/bin/env python3
"""Build LUMINA.DSK from the try16 template DSK.

Usage: make_dsk.py TEMPLATE.DSK LUMINA.COM OUTPUT.DSK [EXTRA ...]

Any EXTRA files are added as new root entries under their
basename (8.3) - used by the SCREEN 8 build for the streamed
Earth images (data/tpl8.dat, data/gpl8.dat).

The template (try16's shipped TRY16.DSK) carries MSX-DOS
(COMMAND.COM / DOS.SYS) and an AUTOEXEC.BAT. This tool:
  - renames the TRY16.COM root entry to LUMINA.COM and replaces
    its FAT chain/data with the new binary,
  - rewrites AUTOEXEC.BAT in place to run "starbrk".
Deleted (0xE5) root entries left in the template are skipped.
FAT handling follows ~/workspace/msx/tools/replace_in_dsk.py.
"""
import struct
import sys


def main() -> int:
    template_path, newfile_path, out_path = sys.argv[1:4]
    img = bytearray(open(template_path, "rb").read())
    data_new = open(newfile_path, "rb").read()

    bytes_per_sec = struct.unpack_from("<H", img, 11)[0]
    sec_per_clus = img[13]
    reserved = struct.unpack_from("<H", img, 14)[0]
    num_fats = img[16]
    root_entries = struct.unpack_from("<H", img, 17)[0]
    sec_per_fat = struct.unpack_from("<H", img, 22)[0]

    fat_size = sec_per_fat * bytes_per_sec
    fat_offsets = [(reserved + i * sec_per_fat) * bytes_per_sec
                   for i in range(num_fats)]
    root_off = (reserved + num_fats * sec_per_fat) * bytes_per_sec
    data_off = root_off + root_entries * 32
    clus_size = sec_per_clus * bytes_per_sec

    fat = bytearray(img[fat_offsets[0]:fat_offsets[0] + fat_size])

    def fat_get(c):
        off = c + c // 2
        if c % 2 == 0:
            return fat[off] | ((fat[off + 1] & 0x0F) << 8)
        return (fat[off] >> 4) | (fat[off + 1] << 4)

    def fat_set(c, val):
        off = c + c // 2
        if c % 2 == 0:
            fat[off] = val & 0xFF
            fat[off + 1] = (fat[off + 1] & 0xF0) | ((val >> 8) & 0x0F)
        else:
            fat[off] = (fat[off] & 0x0F) | ((val << 4) & 0xF0)
            fat[off + 1] = (val >> 4) & 0xFF

    def chain(start):
        out = []
        c = start
        while 2 <= c < 0xFF8:
            out.append(c)
            c = fat_get(c)
        return out

    def find_active(name83):
        base, ext = name83.upper().split(".")
        raw = base.ljust(8).encode() + ext.ljust(3).encode()
        for i in range(root_entries):
            off = root_off + i * 32
            if img[off] == 0x00:
                break
            if img[off] == 0xE5:
                continue
            if bytes(img[off:off + 11]) == raw:
                return off
        return None

    # --- replace TRY16.COM entry with LUMINA.COM ---
    entry = find_active("TRY16.COM")
    if entry is None:
        print("TRY16.COM not found in template", file=sys.stderr)
        return 1
    old_first = struct.unpack_from("<H", img, entry + 26)[0]
    old_size = struct.unpack_from("<I", img, entry + 28)[0]
    print(f"old TRY16.COM: cluster {old_first}, size {old_size}, "
          f"chain {chain(old_first)}")
    for c in chain(old_first):
        fat_set(c, 0)

    need = max(1, (len(data_new) + clus_size - 1) // clus_size)
    free = [c for c in range(2, 2 + (len(img) - data_off) // clus_size)
            if fat_get(c) == 0]
    if len(free) < need:
        print("not enough free clusters", file=sys.stderr)
        return 1
    alloc = free[:need]
    for i, c in enumerate(alloc):
        fat_set(c, alloc[i + 1] if i + 1 < need else 0xFFF)
        start = data_off + (c - 2) * clus_size
        chunk = data_new[i * clus_size:(i + 1) * clus_size]
        img[start:start + clus_size] = chunk.ljust(clus_size, b"\x00")
    img[entry:entry + 11] = b"LUMINA  COM"
    struct.pack_into("<H", img, entry + 26, alloc[0])
    struct.pack_into("<I", img, entry + 28, len(data_new))
    print(f"new LUMINA.COM: size {len(data_new)}, clusters {alloc}")

    # --- rewrite AUTOEXEC.BAT in place (same cluster, new size) ---
    bat = find_active("AUTOEXEC.BAT")
    if bat is None:
        print("AUTOEXEC.BAT not found in template", file=sys.stderr)
        return 1
    bat_first = struct.unpack_from("<H", img, bat + 26)[0]
    content = b"lumina\r\n"
    start = data_off + (bat_first - 2) * clus_size
    img[start:start + clus_size] = content.ljust(clus_size, b"\x00")
    struct.pack_into("<I", img, bat + 28, len(content))
    print(f"AUTOEXEC.BAT: cluster {bat_first}, now {content!r}")

    # --- add any extra files as new root entries ---
    import os
    for extra_path in sys.argv[4:]:
        data_x = open(extra_path, "rb").read()
        name83 = os.path.basename(extra_path).upper()
        base, ext = name83.split(".")
        raw = base.ljust(8).encode() + ext.ljust(3).encode()
        slot = None
        for i in range(root_entries):
            off = root_off + i * 32
            if img[off] in (0x00, 0xE5):
                slot = off
                break
        if slot is None:
            print(f"no free root entry for {name83}", file=sys.stderr)
            return 1
        need = max(1, (len(data_x) + clus_size - 1) // clus_size)
        free = [c for c in range(2, 2 + (len(img) - data_off) // clus_size)
                if fat_get(c) == 0]
        if len(free) < need:
            print(f"not enough free clusters for {name83}",
                  file=sys.stderr)
            return 1
        alloc = free[:need]
        for i, c in enumerate(alloc):
            fat_set(c, alloc[i + 1] if i + 1 < need else 0xFFF)
            start = data_off + (c - 2) * clus_size
            chunk = data_x[i * clus_size:(i + 1) * clus_size]
            img[start:start + clus_size] = chunk.ljust(clus_size, b"\x00")
        img[slot:slot + 32] = b"\x00" * 32
        img[slot:slot + 11] = raw
        img[slot + 11] = 0x20
        struct.pack_into("<H", img, slot + 26, alloc[0])
        struct.pack_into("<I", img, slot + 28, len(data_x))
        print(f"added {name83}: size {len(data_x)}, clusters {alloc}")

    for fo in fat_offsets:
        img[fo:fo + fat_size] = fat

    open(out_path, "wb").write(img)
    print(f"wrote {out_path} ({len(img)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
