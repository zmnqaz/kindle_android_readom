#!/usr/bin/env python3
"""Readom library cover: an original little robot reading a book (grey, for e-ink)."""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import sys
W, H, S = 1072, 1448, 3              # final size, supersampling factor
w, h = W * S, H * S
img = Image.new('L', (w, h), 247)    # paper white
d = ImageDraw.Draw(img)
def R(*v): return [int(x * S) for x in v]
INK, MID, LIGHT, PAGE = 40, 120, 200, 252

# frame
d.rounded_rectangle(R(40, 40, W - 40, H - 40), radius=36 * S, outline=INK, width=6 * S)

cx = W / 2
# antenna
d.line(R(cx, 250, cx, 330), fill=INK, width=12 * S)
d.ellipse(R(cx - 26, 214, cx + 26, 266), fill=INK)
# head: rounded square
d.rounded_rectangle(R(cx - 200, 320, cx + 200, 640), radius=90 * S, fill=MID, outline=INK, width=10 * S)
# visor
d.rounded_rectangle(R(cx - 150, 400, cx + 150, 560), radius=70 * S, fill=PAGE, outline=INK, width=8 * S)
# eyes (looking down at the book) and a small smile
for ex in (cx - 62, cx + 62):
    d.ellipse(R(ex - 26, 455, ex + 26, 507), fill=INK)
    d.ellipse(R(ex - 9, 466, ex + 5, 480), fill=PAGE)
d.arc(R(cx - 36, 498, cx + 36, 540), start=20, end=160, fill=INK, width=7 * S)
# ear bolts
for ex in (cx - 222, cx + 222):
    d.ellipse(R(ex - 22, 460, ex + 22, 520), fill=INK)
# neck and body
d.rectangle(R(cx - 50, 640, cx + 50, 680), fill=INK)
d.rounded_rectangle(R(cx - 230, 672, cx + 230, 1010), radius=60 * S, fill=MID, outline=INK, width=10 * S)
# open book held in front of the body
bt, bb = 760, 1000
d.polygon(R(cx - 300, bt + 20, cx - 6, bt + 50, cx - 6, bb + 20, cx - 300, bb - 10), fill=PAGE, outline=INK)
d.polygon(R(cx + 300, bt + 20, cx + 6, bt + 50, cx + 6, bb + 20, cx + 300, bb - 10), fill=PAGE, outline=INK)
d.line(R(cx - 300, bt + 20, cx - 6, bt + 50, cx - 6, bb + 20, cx - 300, bb - 10, cx - 300, bt + 20), fill=INK, width=9 * S)
d.line(R(cx + 300, bt + 20, cx + 6, bt + 50, cx + 6, bb + 20, cx + 300, bb - 10, cx + 300, bt + 20), fill=INK, width=9 * S)
d.line(R(cx, bt + 46, cx, bb + 24), fill=INK, width=10 * S)
for i in range(5):                       # text lines on the pages
    y = bt + 90 + i * 36
    d.line(R(cx - 260, y - 4 + i * 0, cx - 50, y + 18), fill=LIGHT, width=8 * S)
    d.line(R(cx + 50, y + 18, cx + 260, y - 4), fill=LIGHT, width=8 * S)
# hands on the book edges
for hx in (cx - 300, cx + 300):
    d.ellipse(R(hx - 34, 850, hx + 34, 918), fill=MID, outline=INK, width=8 * S)
# legs
for lx in (cx - 120, cx + 120):
    d.rounded_rectangle(R(lx - 48, 1010, lx + 48, 1100), radius=24 * S, fill=MID, outline=INK, width=8 * S)

# title
def font(size, bold=True):
    for p in ['/work/aa/idea/aosp-8.1/external/roboto-fonts/Roboto-Bold.ttf' if bold else
              '/work/aa/idea/aosp-8.1/external/roboto-fonts/Roboto-Regular.ttf']:
        try: return ImageFont.truetype(p, size * S)
        except OSError: pass
    return ImageFont.load_default()
for text, size, y, bold, fill in (("Readom", 150, 1150, True, INK), ("Android", 64, 1325, False, MID)):
    f = font(size, bold)
    tw = d.textlength(text, font=f)
    d.text((w / 2 - tw / 2, y * S), text, font=f, fill=fill)

img = img.resize((W, H), Image.LANCZOS)
big = sys.argv[1] if len(sys.argv) > 1 else "readom-cover-full.png"
img.save(big)
img.resize((300, 405), Image.LANCZOS).save("readom-cover.png", optimize=True)
