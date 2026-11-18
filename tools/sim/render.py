#!/usr/bin/env python3
"""Converte out/*.rgb do simulador em PNG redondos e monta folhas de contato."""
import sys, glob, os
from PIL import Image, ImageDraw
N = 360
d = sys.argv[1] if len(sys.argv) > 1 else 'out'
mask = Image.new('L', (N, N), 0); ImageDraw.Draw(mask).ellipse([0, 0, N - 1, N - 1], fill=255)
names = sorted(glob.glob(os.path.join(d, '*.rgb')))
imgs = []
for f in names:
    im = Image.frombytes('RGB', (N, N), open(f, 'rb').read())
    out = Image.new('RGB', (N, N), (20, 20, 20)); out.paste(im, (0, 0), mask)
    out.save(f[:-4] + '.png'); imgs.append((os.path.basename(f)[:-4], out))
cols = 4
for k in range(0, len(imgs), 8):
    part = imgs[k:k + 8]
    sheet = Image.new('RGB', (cols * (N + 10), ((len(part) + cols - 1) // cols) * (N + 30)), (20, 20, 20))
    dr = ImageDraw.Draw(sheet)
    for i, (n, im) in enumerate(part):
        x, y = (i % cols) * (N + 10), (i // cols) * (N + 30)
        sheet.paste(im, (x, y + 24)); dr.text((x + 6, y + 6), n, fill=(200, 200, 200))
    sheet.save(os.path.join(d, f'sheet_{k // 8}.png'))
print(len(imgs), 'imagens')
