"""Write the exe's main icon (its first icon group) as a PNG, for a menu entry.

usage: icon.py <exe> <out.png>"""
import io, struct, sys
from PIL import Image


def resources(pe):
    e = struct.unpack_from('<I', pe, 0x3c)[0]
    nsec = struct.unpack_from('<H', pe, e + 6)[0]
    opt = e + 24
    rva = struct.unpack_from('<I', pe, opt + 96 + 2 * 8)[0]
    secs = e + 24 + struct.unpack_from('<H', pe, e + 20)[0]
    for i in range(nsec):
        va, size, raw = struct.unpack_from('<III', pe, secs + i * 40 + 12)
        if va <= rva < va + max(size, 1):
            return rva, lambda r: raw + r - va
    raise SystemExit('no resources')


def entries(pe, base, off):
    n = sum(struct.unpack_from('<HH', pe, off + 12))
    for i in range(n):
        name, data = struct.unpack_from('<II', pe, off + 16 + i * 8)
        yield name, data


def first_leaf(pe, base, off):
    while True:
        name, data = next(entries(pe, base, off))
        if not data & 0x80000000:
            return data
        off = base + (data & 0x7fffffff)


def find(pe, base, to_file, typ, ident=None):
    for name, data in entries(pe, base, base):
        if name == typ:
            tdir = base + (data & 0x7fffffff)
            for n2, d2 in entries(pe, base, tdir):
                if ident is None or n2 == ident:
                    leaf = first_leaf(pe, base, base + (d2 & 0x7fffffff))
                    rva, size = struct.unpack_from('<II', pe, base + leaf)
                    o = to_file(rva)
                    return pe[o:o + size]
    return None


pe = open(sys.argv[1], 'rb').read()
rva, to_file = resources(pe)
base = to_file(rva)
grp = find(pe, base, to_file, 14)
count = struct.unpack_from('<H', grp, 4)[0]
best = None
for i in range(count):
    w, h, cc, _, planes, bpp, size, ident = struct.unpack_from('<BBBBHHIH', grp, 6 + i * 14)
    key = ((w or 256) * (h or 256), bpp)
    if best is None or key > best[0]:
        best = (key, (w, h, cc, planes, bpp, ident))
w, h, cc, planes, bpp, ident = best[1]
img = find(pe, base, to_file, 3, ident)
ico = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', w, h, cc, 0, planes, bpp, len(img), 22) + img
im = Image.open(io.BytesIO(ico)).convert('RGBA')
k = max(1, 256 // im.width)        # whole-pixel scaling, so menus show it sharp
im.resize((im.width * k, im.height * k), Image.NEAREST).save(sys.argv[2])
print(f'{w or 256}x{h or 256} {bpp} bpp')
