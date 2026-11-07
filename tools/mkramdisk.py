#!/usr/bin/env python3
"""mkramdisk.py - build the e1ruBSD root ramdisk (ustar archive).

Usage: mkramdisk.py output.tar
Reads a manifest from argv[2:]: each entry is PATH=FILE.
Entries named "DIR/" (trailing slash) become directory entries.
"""

import os
import sys

BLOCK = 512


def tar_header(name, size, typeflag):
    h = bytearray(BLOCK)
    name = name.encode()
    if len(name) > 100:
        sys.exit(f"name too long: {name}")
    h[0:len(name)] = name
    h[100:108] = b"0000644\x00"          # mode
    h[108:116] = b"0000000\x00"          # uid
    h[116:124] = b"0000000\x00"          # gid
    h[124:136] = ("%011o\x00" % size).encode()
    h[136:148] = ("%011o\x00" % 0).encode()   # mtime
    h[148:156] = b"        "             # chksum placeholder
    h[156:157] = typeflag
    h[257:263] = b"ustar\x00"
    h[263:265] = b"00"
    chk = sum(h)
    h[148:156] = ("%06o\x00 " % chk).encode()
    return bytes(h)


def main():
    out, *entries = sys.argv[1:]
    blob = bytearray()

    for ent in entries:
        if ent.endswith("/"):
            blob += tar_header(ent, 0, b"5")
            continue
        path, _, fname = ent.partition("=")
        with open(fname, "rb") as f:
            data = f.read()
        blob += tar_header(path, len(data), b"0")
        blob += data
        if len(data) % BLOCK:
            blob += b"\0" * (BLOCK - len(data) % BLOCK)

    blob += b"\0" * (BLOCK * 2)          # end of archive
    with open(out, "wb") as f:
        f.write(blob)
    print(f"mkramdisk: {out} ({len(blob)} bytes, {len(entries)} entries)")


if __name__ == "__main__":
    main()
