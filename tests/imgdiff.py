"""Two folders of frame dumps (.ppm, the same names) compared: per frame the largest and mean
difference of a colour channel (0-255), the share of pixels that differ at all and by more than 8;
then side by side (A | B) and difference (x8) images of the frame that differs most.
    tests/imgdiff.py DIR_A DIR_B OUTDIR"""
import os
import sys

import numpy as np
from PIL import Image

a_dir, b_dir, out = sys.argv[1:4]
names = sorted(n for n in os.listdir(a_dir) if n.endswith(".ppm") and os.path.exists(os.path.join(b_dir, n)))
if not names:
    sys.exit("no frames in common")
print(f"{'frame':28} {'max':>4} {'mean':>8} {'differ':>8} {'>8':>8}")
worst, rows = None, []
for n in names:
    a = np.asarray(Image.open(os.path.join(a_dir, n)).convert("RGB"), dtype=np.int16)
    b = np.asarray(Image.open(os.path.join(b_dir, n)).convert("RGB"), dtype=np.int16)
    if a.shape != b.shape:
        print(f"{n:28} sizes differ {a.shape} {b.shape}")
        continue
    d = np.abs(a - b)
    px = d.max(axis=2)
    r = (n, int(d.max()), float(d.mean()), float((px > 0).mean()), float((px > 8).mean()))
    rows.append(r)
    print(f"{r[0]:28} {r[1]:4} {r[2]:8.4f} {100 * r[3]:7.3f}% {100 * r[4]:7.3f}%")
    if worst is None or r[2] > worst[1]:
        worst = (n, r[2], a, b, d)
m = np.array([[r[1], r[2], r[3], r[4]] for r in rows])
print(f"{'all ' + str(len(rows)) + ' frames':28} {int(m[:, 0].max()):4} {m[:, 1].mean():8.4f} {100 * m[:, 2].mean():7.3f}% "
      f"{100 * m[:, 3].mean():7.3f}%  (max of max; means of the rest)")
n, _, a, b, d = worst
base = os.path.join(out, os.path.splitext(n)[0])
Image.fromarray(np.concatenate([a, b], axis=1).astype(np.uint8)).save(base + "_side.png")
Image.fromarray(np.clip(d * 8, 0, 255).astype(np.uint8)).save(base + "_diff8.png")
print(f"most different: {n} -> {base}_side.png (A | B), {base}_diff8.png")
