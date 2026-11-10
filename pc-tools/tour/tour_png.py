"""Screenshot tour: turn the launcher's raw screen dumps (menu/tour/NNN.raw on the SD card) into PNGs.

Usage: python tour_png.py <tour folder> <output folder>

Each .raw is one 640x480 framebuffer, 16-bit RGBA 5551, big-endian, rows of (file size / 480) bytes. 16:9 scenes
are drawn squeezed for a TV set to stretch, so their PNGs are widened to 853x480, as the TV shows them.
Names come from index.txt: 001_home_binder_Castle-Sky_16x9_RPF.png. Python standard library only.
"""
import os
import struct
import sys
import zlib


def png(path, width, height, rows):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + r for r in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 6)))
        f.write(chunk(b'IEND', b''))


# 5-bit to 8-bit, and every 16-bit pixel to its RGB bytes, worked out once.
FIVE = [(v << 3) | (v >> 2) for v in range(32)]
RGB = [bytes((FIVE[(p >> 11) & 31], FIVE[(p >> 6) & 31], FIVE[(p >> 1) & 31])) for p in range(65536)]


def convert(raw_path, out_path, wide):
    data = open(raw_path, 'rb').read()
    height = 480
    stride = len(data) // height
    width = 640
    out_w = 853 if wide else width
    # Widening: each output column takes the source column under it.
    cols = [min(width - 1, x * width // out_w) for x in range(out_w)]
    rows = []
    for y in range(height):
        line = data[y * stride: y * stride + width * 2]
        px = struct.unpack('>%dH' % width, line)
        rows.append(b''.join(RGB[px[c]] for c in cols))
    png(out_path, out_w, height, rows)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    names = {}
    index = os.path.join(src, 'index.txt')
    if os.path.exists(index):
        for line in open(index, encoding='utf-8', errors='replace'):
            if line.startswith('#') or not line.strip():
                continue
            f = line.rstrip('\n').split('\t')
            names[f[0]] = f
    done = 0
    for name in sorted(os.listdir(src)):
        if not name.endswith('.raw'):
            continue
        num = name[:-4]
        f = names.get(num)
        if f:
            screen, variant, theme, aspect, lists, quick = f[1], f[2], f[3], f[4], f[5], f[6]
            label = '_'.join([num, screen, variant, theme.replace(' ', '-'), aspect.replace(':', 'x'),
                              lists.replace('lists=', '') or 'none'] + (['quick'] if quick == 'quick-menu' else []))
            wide = aspect == '16:9'
        else:
            label, wide = num, False
        convert(os.path.join(src, name), os.path.join(dst, label + '.png'), wide)
        done += 1
        print('\r%d converted' % done, end='', flush=True)
    print()


if __name__ == '__main__':
    main()
