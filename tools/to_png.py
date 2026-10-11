#!/usr/bin/env python3
"""PPM -> PNG（stdlib only），把 PC 仿真的帧缓冲转成能贴进对话的图。"""
import sys, zlib, struct

def to_png(src, dst):
    with open(src, "rb") as f:
        assert f.readline().strip() == b"P6", "not a P6 ppm"
        w, h = map(int, f.readline().split())
        assert int(f.readline()) == 255
        raw = f.read()
    assert len(raw) == w * h * 3, (len(raw), w, h)
    comp = zlib.compress(b"".join([b"\x00" + raw[y*w*3:(y+1)*w*3] for y in range(h)]), 6)
    def ch(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t+d) & 0xFFFFFFFF)
    with open(dst, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + ch(b"IDAT", comp) + ch(b"IEND", b""))
    print("wrote %s (%dx%d)" % (dst, w, h))

if __name__ == "__main__":
    to_png(sys.argv[1], sys.argv[2])
