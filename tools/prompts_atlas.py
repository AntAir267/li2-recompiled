"""The button prompts' pictures (runtime/prompts.c): one RGBA atlas of the controller's buttons in
two brick styles, the keyboard's keys as LEGO tiles, and the prompts' words, written as
runtime/prompts_atlas.h (the atlas QOI-encoded, and a table of where each picture is).

All drawn here: shapes, and DejaVu Sans Bold Oblique (Bitstream Vera licence: free to embed) for
the letters and words; nothing from the game.
  stud_*   the game's menu-row button look: a dark ring, a glossy rim, a dished centre
  lego_*   a LEGO stud from above, the letter moulded where the logo is
  key_*    a key as a 1xN light bluish grey tile
  word_*   a prompt's word: yellow italic with a dark outline, as the game's HUD numbers
    tools/prompts_atlas.py [font.ttf] > runtime/prompts_atlas.h"""
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf"
LEGO = {'A': (75, 159, 74), 'B': (201, 26, 9), 'X': (0, 85, 191), 'Y': (242, 205, 55),
        'LB': (100, 100, 100), 'RB': (100, 100, 100), 'BACK': (100, 100, 100), 'START': (100, 100, 100)}
KEYS = ["Alt", "Shift", "Space", "Ctrl", "Enter", "Esc"]
WORDS = ["Jump", "Talk", "Pizza", "Skateboard", "Skip", "Select", "Back", "Pause", "Toy", "Ollie", "Flip",
         "Grab", "Trick"]
B = 112          # a button's size
KH = 96          # a key tile's height
WH = 72          # a word's height


def font(size):
    return ImageFont.truetype(FONT, size)


def label_of(name):
    return {'BACK': '◄', 'START': '►'}.get(name, name)     # (Back / Start as arrows)


def letter(img, text, size, fill, stroke, sw, dy=0):
    d = ImageDraw.Draw(img)
    f = font(size)
    bb = d.textbbox((0, 0), text, font=f, stroke_width=sw)
    x = (img.size[0] - (bb[2] - bb[0])) // 2 - bb[0]
    y = (img.size[1] - (bb[3] - bb[1])) // 2 - bb[1] + dy
    d.text((x, y), text, font=f, fill=fill, stroke_width=sw, stroke_fill=stroke)


def shade(c, k, add=0):
    return tuple(max(0, min(255, int(v * k + add))) for v in c)


def stud(name):
    """the game's menu-row button: dark ring, glossy rim lit from the upper left, dished centre"""
    col, ss = LEGO[name], 4
    N = B * ss
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse([N * .07, N * .09, N * .99, N * .99], fill=(0, 0, 0, 110))            # shadow
    d.ellipse([N * .02, N * .02, N * .94, N * .94], fill=(18, 18, 18, 255))         # ring
    for i in range(48):                                                         # rim: light at the upper left
        t = i / 47
        r0, r1 = N * (.07 + .30 * t), N * (.89 - .30 * t)
        d.ellipse([r0, r0, r1, r1], fill=shade(col, .55 + .9 * (1 - t) ** 2.2, 10 * (1 - t)) + (255,))
    d.ellipse([N * .24, N * .24, N * .72, N * .72], outline=shade(col, .45) + (255,), width=int(N * .03))
    d.ellipse([N * .27, N * .27, N * .69, N * .69], fill=shade(col, .78) + (255,))  # the dish
    hl = Image.new("L", (N, N), 0)
    ImageDraw.Draw(hl).ellipse([N * .14, N * .10, N * .50, N * .38], fill=170)
    hl = hl.filter(ImageFilter.GaussianBlur(N * .04))
    im = Image.composite(Image.new("RGBA", (N, N), (255, 255, 255, 255)), im, hl.point(lambda v: v * 0.55))
    im = im.resize((B, B), Image.LANCZOS)
    letter(im, label_of(name), int(B * (.46 if len(name) == 1 else .30)), (255, 255, 255), (20, 20, 20), 4, -3)
    return im


def lego(name):
    """a LEGO stud from above, the letter moulded in it"""
    col, ss = LEGO[name], 4
    N = B * ss
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse([N * .07, N * .11, N * .99, N * .99], fill=(0, 0, 0, 100))
    d.ellipse([N * .02, N * .02, N * .94, N * .94], fill=shade(col, .55) + (255,))  # the stud's side
    d.ellipse([N * .05, N * .03, N * .91, N * .86], fill=col + (255,))               # its top
    hl = Image.new("L", (N, N), 0)
    ImageDraw.Draw(hl).ellipse([N * .14, N * .08, N * .60, N * .44], fill=150)
    hl = hl.filter(ImageFilter.GaussianBlur(N * .05))
    im = Image.composite(Image.new("RGBA", (N, N), shade(col, 1.25, 30) + (255,)), im, hl)
    im = im.resize((B, B), Image.LANCZOS)
    t = Image.new("RGBA", (B, int(B * .9)), (0, 0, 0, 0))                       # the moulding: dark below, light above
    size = int(B * (.50 if len(name) == 1 else .32))
    sh = Image.new("RGBA", t.size, (0, 0, 0, 0))
    letter(sh, label_of(name), size, shade(col, .45) + (255,), (0, 0, 0, 0), 0, 3)
    letter(t, label_of(name), size, shade(col, 1.35, 40) + (255,), shade(col, .6) + (255,), 2, -2)
    im.alpha_composite(sh, (2, 2))
    im.alpha_composite(t, (0, 0))
    return im


def key(text):
    """a keyboard key as a 1xN light bluish grey tile"""
    col, ss = (160, 165, 169), 3
    f = font(int(KH * .40))
    w = ImageDraw.Draw(Image.new("RGB", (1, 1))).textbbox((0, 0), text, font=f)[2]
    W = max(KH, int(w + KH * .75))
    N = (W * ss, KH * ss)
    im = Image.new("RGBA", N, (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    r = int(KH * ss * .16)
    d.rounded_rectangle([8, 14, N[0] - 1, N[1] - 1], r, fill=(0, 0, 0, 100))
    d.rounded_rectangle([0, 0, N[0] - 8, N[1] - 12], r, fill=shade(col, .62) + (255,))
    top = N[1] - 26
    face = Image.new("RGBA", N, (0, 0, 0, 0))
    fd = ImageDraw.Draw(face)
    for y in range(4, top):
        t = (y - 4) / (top - 4)
        fd.line([(6, y), (N[0] - 14, y)], fill=shade(col, 1.18 - .30 * t, 12 * (1 - t)) + (255,))
    m = Image.new("L", N, 0)
    ImageDraw.Draw(m).rounded_rectangle([6, 4, N[0] - 14, top], r, fill=255)
    im.paste(face, (0, 0), m)
    ImageDraw.Draw(im).rounded_rectangle([6, 4, N[0] - 14, top], r, outline=(255, 255, 255, 120), width=3)
    im = im.resize((W, KH), Image.LANCZOS)
    t = Image.new("RGBA", (W - 8, KH - 12), (0, 0, 0, 0))
    letter(t, text, int(KH * .40), (35, 35, 35), (0, 0, 0, 0), 0)
    im.alpha_composite(t, (0, 0))
    return im


def word(text):
    """a prompt's word: yellow italic, a dark outline (the game's HUD numbers' colours)"""
    f = font(int(WH * .62))
    bb = ImageDraw.Draw(Image.new("RGB", (1, 1))).textbbox((0, 0), text, font=f, stroke_width=5)
    im = Image.new("RGBA", (bb[2] - bb[0] + 8, WH), (0, 0, 0, 0))
    ImageDraw.Draw(im).text((4 - bb[0], int(WH * .74)), text, font=f, anchor="ls", fill=(255, 236, 60),
                            stroke_width=5, stroke_fill=(40, 26, 0))     # (one baseline for all the words)
    return im


def qoi(img):
    """QOI encoding (qoiformat.org), RGBA"""
    w, h = img.size
    px = img.tobytes()
    out = bytearray(b"qoif" + w.to_bytes(4, "big") + h.to_bytes(4, "big") + bytes([4, 0]))
    index = [(0, 0, 0, 0)] * 64
    prev, run = (0, 0, 0, 255), 0
    n = w * h
    for i in range(n):
        p = tuple(px[4 * i:4 * i + 4])
        if p == prev:
            run += 1
            if run == 62 or i == n - 1:
                out.append(0xc0 | (run - 1))
                run = 0
            continue
        if run:
            out.append(0xc0 | (run - 1))
            run = 0
        h_ = (p[0] * 3 + p[1] * 5 + p[2] * 7 + p[3] * 11) % 64
        if index[h_] == p:
            out.append(h_)
        else:
            index[h_] = p
            if p[3] == prev[3]:
                dr, dg, db = (p[0] - prev[0] + 128) % 256 - 128, (p[1] - prev[1] + 128) % 256 - 128, \
                    (p[2] - prev[2] + 128) % 256 - 128
                if -2 <= dr <= 1 and -2 <= dg <= 1 and -2 <= db <= 1:
                    out.append(0x40 | (dr + 2) << 4 | (dg + 2) << 2 | (db + 2))
                elif -32 <= dg <= 31 and -8 <= dr - dg <= 7 and -8 <= db - dg <= 7:
                    out += bytes([0x80 | (dg + 32), (dr - dg + 8) << 4 | (db - dg + 8)])
                else:
                    out += bytes([0xfe, p[0], p[1], p[2]])
            else:
                out += bytes([0xff, p[0], p[1], p[2], p[3]])
        prev = p
    return bytes(out + b"\0" * 7 + b"\1")


def disc():
    """white, for the strip's rounded ends (tinted)"""
    N = 64 * 4
    im = Image.new("RGBA", (N, N), (255, 255, 255, 0))
    ImageDraw.Draw(im).ellipse([2, 2, N - 3, N - 3], fill=(255, 255, 255, 255))
    return im.resize((64, 64), Image.LANCZOS)


pics = [("disc", disc()), ("white", Image.new("RGBA", (8, 8), (255, 255, 255, 255)))] + \
       [("stud_" + n, stud(n)) for n in LEGO] + [("lego_" + n, lego(n)) for n in LEGO] + \
       [("key_" + k, key(k)) for k in KEYS] + [("word_" + w, word(w)) for w in WORDS]
W, x, y, row, place = 1024, 0, 0, 0, []
for name, im in pics:                                   # shelves, 2 pixels apart
    if x + im.size[0] > W:
        x, y, row = 0, y + row + 2, 0
    place.append((name, x, y, im))
    x += im.size[0] + 2
    row = max(row, im.size[1])
H = y + row
atlas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
for name, px, py, im in place:
    atlas.paste(im, (px, py))
data = qoi(atlas)
print("/* tools/prompts_atlas.py: the button prompts' atlas (%dx%d RGBA, QOI) and its pictures */" % (W, H))
print("static const struct { const char *name; int x, y, w, h; } prompt_pics[] = {")
for name, px, py, im in place:
    print('    {"%s", %d, %d, %d, %d},' % (name, px, py, im.size[0], im.size[1]))
print("};")
print("static const unsigned char prompt_atlas_qoi[%d] = {" % len(data))
for i in range(0, len(data), 24):
    print("    " + ",".join(str(b) for b in data[i:i + 24]) + ",")
print("};")
if len(sys.argv) > 2:
    atlas.save(sys.argv[2])
