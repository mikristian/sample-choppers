#!/usr/bin/env python3
"""Draw a self-contained, original ASR-style sampler macOS application icon."""
from pathlib import Path
import math
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFilter, ImageFont

BASE = Path(__file__).resolve().parents[1]
DEST = BASE / "assets"
DEST.mkdir(parents=True, exist_ok=True)
SIZE = 1024

im = Image.new("RGBA", (SIZE, SIZE))
shade = Image.new("RGBA", (SIZE, SIZE))
sd = ImageDraw.Draw(shade)
sd.rounded_rectangle((49, 73, 975, 951), radius=178, fill=(0, 0, 0, 165))
im.alpha_composite(shade.filter(ImageFilter.GaussianBlur(30)))
d = ImageDraw.Draw(im)
d.rounded_rectangle((66, 51, 958, 946), radius=167, fill=(20, 22, 26),
                    outline=(103, 111, 116), width=9)
d.rounded_rectangle((90, 83, 935, 919), radius=143, fill=(31, 33, 38),
                    outline=(51, 57, 63), width=3)
for y in range(128, 560, 7):
    tint = 34 + (y % 17) // 4
    d.line((105, y, 921, y), fill=(tint, tint+1, tint+5, 26), width=1)

# Black firmware display, turquoise 90s fluorescent 22-character-style readout.
d.rounded_rectangle((173, 206, 838, 390), radius=19, fill=(5, 9, 14),
                    outline=(84, 92, 97), width=7)
d.rounded_rectangle((197, 231, 813, 371), radius=9, fill=(4, 24, 34))
d.text((221, 243), "LOAD   INST     STOP", fill=(53, 134, 146))
text = "FILE  1  ASR SAMPLER"
try:
    f = ImageFont.truetype("/System/Library/Fonts/Supplemental/Courier New Bold.ttf", 40)
except OSError:
    f = ImageFont.load_default()
glow = Image.new("RGBA", (SIZE, SIZE))
gd = ImageDraw.Draw(glow)
gd.text((215, 291), text, font=f, fill=(58, 236, 244, 190))
im.alpha_composite(glow.filter(ImageFilter.GaussianBlur(13)))
d = ImageDraw.Draw(im)
d.text((215, 291), text, font=f, fill=(102, 244, 245))

# Original cream/gold insignia, reminiscent of the keyboard's gold typographic signature.
try:
    brand_font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Helvetica Bold.ttf", 58)
    sub_font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Helvetica.ttf", 19)
except OSError:
    brand_font = sub_font = ImageFont.load_default()
d.text((176, 113), "A  S  R", font=brand_font, fill=(220, 174, 80))
d.text((575, 143), "SAMPLING RECORDER", font=sub_font, fill=(146, 145, 139))

# Eight physical miniature program buttons and activity LEDs.
for index in range(8):
    x = 186 + index * 81
    d.ellipse((x + 25, 426, x + 38, 439), fill=(50, 240, 169) if index == 0 else (85, 94, 98))
    d.rounded_rectangle((x, 452, x + 65, 495), radius=8, fill=(6, 10, 15),
                        outline=(91, 100, 108), width=3)
    d.text((x + 26, 466), str(index + 1), fill=(204, 220, 222))

# Entire keybed is rendered as 25 visible white keys with the correct black-key pattern.
keytop, keybottom = 568, 875
left, right = 111, 913
count = 25
keywidth = (right-left) / count
d.rounded_rectangle((101, keytop - 14, 926, keybottom + 19), radius=17,
                    fill=(8, 9, 11), outline=(91, 95, 98), width=5)
for i in range(count):
    x0 = left + i * keywidth
    x1 = left + (i + 1) * keywidth - 2
    d.rounded_rectangle((x0, keytop, x1, keybottom), radius=6,
                        fill=(224, 222, 208), outline=(40, 43, 47), width=3)
    d.line((x0 + 5, keytop + 12, x0 + 5, keybottom - 24), fill=(249, 249, 236), width=2)
black_after = {0, 1, 3, 4, 5}
for i in range(count-1):
    if i % 7 not in black_after:
        continue
    x = left + (i + 1) * keywidth
    d.rounded_rectangle((x - keywidth * .28, keytop,
                         x + keywidth * .27, keytop + 175),
                        radius=5, fill=(10, 12, 17), outline=(66, 73, 79), width=3)
    d.line((x - keywidth * .22, keytop + 9, x - keywidth * .22, keytop + 136),
           fill=(52, 56, 60), width=3)

png = DEST / "ASR10SamplerLab.png"
icns = DEST / "ASR10SamplerLab.icns"
im.save(png, optimize=True)
with tempfile.TemporaryDirectory(prefix="asr-icon-") as temp:
    icons = Path(temp) / "ASR10SamplerLab.iconset"
    icons.mkdir()
    for size in (16, 32, 128, 256, 512):
        im.resize((size, size), Image.Resampling.LANCZOS).save(icons / f"icon_{size}x{size}.png")
        im.resize((size * 2, size * 2), Image.Resampling.LANCZOS).save(
            icons / f"icon_{size}x{size}@2x.png")
    subprocess.run(["iconutil", "-c", "icns", str(icons), "-o", str(icns)], check=True)
print(f"{png}\n{icns}")
