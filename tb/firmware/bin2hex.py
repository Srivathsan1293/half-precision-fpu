#!/usr/bin/env python3
"""Convert a flat little-endian binary image to a Verilog $readmemh file.

The image base is 0x0000 and contiguous, so a plain one-word-per-line hex
stream (no @address directives) is emitted.
"""
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: bin2hex.py <in.bin> <out.hex>", file=sys.stderr)
        return 1
    data = open(sys.argv[1], "rb").read()
    with open(sys.argv[2], "w") as f:
        for i in range(0, len(data), 4):
            word = data[i:i + 4].ljust(4, b"\x00")
            f.write("%08X\n" % int.from_bytes(word, "little"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
