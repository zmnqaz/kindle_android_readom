#!/usr/bin/env python3
"""Readom cover, variant 3: an e-reader whose screen shows the project's own robot head
(the square-headed, one-antenna, visored robot of variant 1)."""
from PIL import Image, ImageDraw, ImageFont
W, H, S = 1072, 1448, 3
img = Image.new('L', (W * S, H * S), 247); d = ImageDraw.Draw(img)
R = lambda *v: [int(x * S) for x in v]
INK, MID, PAGE = 40, 120, 252
d.rounded_rectangle(R(40, 40, W - 40, H - 40), radius=36 * S, outline=INK, width=6 * S)
# the reader
x0, y0, x1, y1 = 246, 170, 826, 1060
d.rounded_rectangle(R(x0, y0, x1, y1), radius=48 * S, fill=MID, outline=INK, width=10 * S)
sx0, sy0, sx1, sy1 = x0 + 46, y0 + 60, x1 - 46, y1 - 110
d.rounded_rectangle(R(sx0, sy0, sx1, sy1), radius=12 * S, fill=PAGE, outline=INK, width=6 * S)
# the word on the bottom bezel, as the real reader has (plain Roboto, not a logo)
_f = ImageFont.truetype('/work/aa/idea/aosp-8.1/external/roboto-fonts/Roboto-Regular.ttf', 44 * S)
_t = 'kindle'; _w = d.textlength(_t, font=_f)
d.text(((x0 + x1) / 2 * S - _w / 2, (y1 - 88) * S), _t, font=_f, fill=225)
# the robot head, centred on the screen
cx, cy, k = (sx0 + sx1) / 2, (sy0 + sy1) / 2 + 40, 1.0
hw, hh = 165, 150                        # half width, half height of the head
d.line(R(cx, cy - hh - 90, cx, cy - hh), fill=INK, width=14 * S)            # antenna
d.ellipse(R(cx - 30, cy - hh - 130, cx + 30, cy - hh - 70), fill=INK)
d.rounded_rectangle(R(cx - hw, cy - hh, cx + hw, cy + hh), radius=90 * S, fill=MID, outline=INK, width=12 * S)
d.rounded_rectangle(R(cx - 120, cy - 80, cx + 120, cy + 80), radius=70 * S, fill=PAGE, outline=INK, width=9 * S)  # visor
for ex in (cx - 52, cx + 52):                                                # eyes
    d.ellipse(R(ex - 30, cy - 40, ex + 30, cy + 20), fill=INK)
    d.ellipse(R(ex - 10, cy - 28, ex + 6, cy - 12), fill=PAGE)
d.arc(R(cx - 40, cy + 10, cx + 40, cy + 58), start=20, end=160, fill=INK, width=8 * S)  # smile
for ex in (cx - hw - 20, cx + hw + 20):                                      # ear bolts
    d.ellipse(R(ex - 24, cy - 36, ex + 24, cy + 36), fill=INK)
def font(p, size):
    return ImageFont.truetype('/work/aa/idea/aosp-8.1/external/roboto-fonts/' + p, size * S)
for text, p, size, y, fill in (("Readom", 'Roboto-Bold.ttf', 150, 1120, INK), ("Android on Kindle", 'Roboto-Regular.ttf', 60, 1300, MID)):
    f = font(p, size); tw = d.textlength(text, font=f)
    d.text((W * S / 2 - tw / 2, y * S), text, font=f, fill=fill)
img = img.resize((W, H), Image.LANCZOS); img.save('readom-cover-full.png')
img.resize((300, 405), Image.LANCZOS).save('readom-cover.png', optimize=True)
