#!/usr/bin/env python3
"""Draws the Video Studio app icon (no external artwork) and writes every size the
platforms need. Run from the repository root:  python3 packaging/icons/make_icons.py

Outputs (committed, so builds need no Python):
  packaging/icons/videostudio.png / .ico / .icns     desktop (Linux, Windows, macOS)
  mobile/android/app/src/main/res/mipmap-*/ic_launcher.png
  mobile/ios/Runner/Assets.xcassets/AppIcon.appiconset/*.png
"""
import json
import os
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
S = 1024  # master size


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def master(rounded=True):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # Background: dark panel with a warm glow (tokens: bg #0f1115, accent #f2a93b).
    bg = Image.new("RGBA", (S, S))
    px = bg.load()
    top, bottom = (36, 40, 50), (13, 15, 19)
    for y in range(S):
        c = lerp(top, bottom, y / (S - 1))
        for x in range(S):
            px[x, y] = c + (255,)
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((S * 0.18, S * 0.12, S * 0.95, S * 0.9), fill=(242, 169, 59, 70))
    glow = glow.filter(ImageFilter.GaussianBlur(S * 0.12))
    bg = Image.alpha_composite(bg, glow)
    mask = Image.new("L", (S, S), 0)
    if rounded:
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, S - 1, S - 1), radius=int(S * 0.225), fill=255)
    else:
        mask.paste(255, (0, 0, S, S))
    img.paste(bg, (0, 0), mask)

    d = ImageDraw.Draw(img)
    # Three timeline clips (video, overlay, audio colours) under a play mark.
    clips = [
        ((0.16, 0.66, 0.50, 0.74), (74, 111, 165)),
        ((0.52, 0.66, 0.84, 0.74), (54, 85, 127)),
        ((0.30, 0.77, 0.72, 0.83), (47, 156, 143)),
    ]
    for (x0, y0, x1, y1), col in clips:
        d.rounded_rectangle((S * x0, S * y0, S * x1, S * y1), radius=int(S * 0.02), fill=col + (255,))
    # Playhead
    d.rounded_rectangle((S * 0.585, S * 0.60, S * 0.605, S * 0.87), radius=int(S * 0.01), fill=(242, 169, 59, 255))
    # Play triangle with rounded look (drawn large then downsampled for smooth edges).
    big = Image.new("RGBA", (S * 2, S * 2), (0, 0, 0, 0))
    bd = ImageDraw.Draw(big)
    tri = [(S * 2 * 0.38, S * 2 * 0.17), (S * 2 * 0.38, S * 2 * 0.55), (S * 2 * 0.70, S * 2 * 0.36)]
    bd.polygon(tri, fill=(242, 169, 59, 255))
    bd.line(tri + [tri[0]], fill=(242, 169, 59, 255), width=int(S * 0.09), joint="curve")
    for p in tri:
        r = S * 0.045
        bd.ellipse((p[0] - r, p[1] - r, p[0] + r, p[1] + r), fill=(242, 169, 59, 255))
    big = big.resize((S, S), Image.LANCZOS)
    img = Image.alpha_composite(img, big)
    return img


def save(img, path, size):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.resize((size, size), Image.LANCZOS).save(path)


def main():
    rounded = master(True)
    square = master(False).convert("RGB")  # iOS masks the corners itself and refuses alpha

    out = os.path.join(ROOT, "packaging", "icons")
    os.makedirs(out, exist_ok=True)
    save(rounded, os.path.join(out, "videostudio.png"), 512)
    rounded.save(os.path.join(out, "videostudio.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    rounded.save(os.path.join(out, "videostudio.icns"))

    for name, size in {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}.items():
        save(rounded, os.path.join(ROOT, "mobile/android/app/src/main/res", f"mipmap-{name}", "ic_launcher.png"), size)

    ios = os.path.join(ROOT, "mobile/ios/Runner/Assets.xcassets/AppIcon.appiconset")
    with open(os.path.join(ios, "Contents.json")) as f:
        contents = json.load(f)
    for im in contents["images"]:
        if "filename" not in im:
            continue
        base = float(im["size"].split("x")[0])
        scale = int(im["scale"].rstrip("x"))
        save(square, os.path.join(ios, im["filename"]), int(round(base * scale)))


if __name__ == "__main__":
    main()
