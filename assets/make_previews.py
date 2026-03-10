#!/usr/bin/env python3
"""
Generate synthetic JPEG preview images with embedded EXIF metadata.
Output goes to tests/data/  (used by import/thumbnail unit tests).

Usage:  python3 assets/make_previews.py
Requires: Pillow + piexif   (pip install Pillow piexif)
"""

import math, os, random, struct, io
from PIL import Image, ImageDraw, ImageFont
import piexif
import piexif.helper

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "tests", "data")
os.makedirs(OUT_DIR, exist_ok=True)

# ── Colour palettes for the synthetic scenes ──────────────────────────────────
PALETTES = [
    {"sky": (100, 160, 230), "ground": (60, 120, 50),  "sun": (255, 220, 80)},
    {"sky": (200, 120,  60), "ground": (80,  60, 30),  "sun": (255, 180, 60)},
    {"sky": (160, 200, 240), "ground": (30,  80, 30),  "sun": (255, 250,200)},
    {"sky": ( 50,  70, 120), "ground": (20,  40, 70),  "sun": (180, 200,255)},
    {"sky": (220, 200, 180), "ground": (100, 80, 60),  "sun": (255, 230,120)},
]

CAMERA_MAKES   = ["Canon", "Sony", "Nikon", "Fujifilm", "Leica"]
CAMERA_MODELS  = ["EOS R5", "A7R V", "Z9", "X-T5", "M11"]
LENS_MODELS    = ["24-70mm f/2.8", "85mm f/1.4", "16-35mm f/2.8",
                  "70-200mm f/2.8", "50mm f/1.2"]

def lerp(a, b, t):
    return int(a + (b - a) * max(0.0, min(1.0, t)))

def lerp_color(c1, c2, t):
    return (lerp(c1[0], c2[0], t),
            lerp(c1[1], c2[1], t),
            lerp(c1[2], c2[2], t))

def render_scene(w: int, h: int, pal: dict, seed: int) -> Image.Image:
    rng = random.Random(seed)
    img = Image.new("RGB", (w, h))
    d   = ImageDraw.Draw(img)

    horizon = int(h * rng.uniform(0.45, 0.65))

    # Sky gradient
    sky_bot = tuple(min(255, c + 30) for c in pal["sky"])
    for y in range(horizon):
        t = y / max(1, horizon - 1)
        d.line([(0, y), (w, y)], fill=lerp_color(pal["sky"], sky_bot, t))

    # Ground gradient
    gnd_bot = tuple(max(0, c - 25) for c in pal["ground"])
    for y in range(horizon, h):
        t = (y - horizon) / max(1, h - horizon - 1)
        d.line([(0, y), (w, y)], fill=lerp_color(pal["ground"], gnd_bot, t))

    # Sun / moon
    sx = int(w * rng.uniform(0.55, 0.85))
    sy = int(horizon * rng.uniform(0.15, 0.50))
    sr = max(8, int(w * 0.04))
    for r in range(sr * 3, sr - 1, -2):
        t   = max(0.0, 1.0 - (r - sr) / (sr * 2.5))
        alp = int(t * t * 160)
        d.ellipse([sx-r, sy-r, sx+r, sy+r],
                  fill=pal["sun"] + (alp,) if img.mode == "RGBA" else pal["sun"])
    d.ellipse([sx-sr, sy-sr, sx+sr, sy+sr], fill=pal["sun"])

    # Mountain silhouettes
    peaks = [(rng.uniform(0, 1), rng.uniform(0.06, 0.28)) for _ in range(8)]
    peaks.sort()
    pts = [(0, horizon)]
    for px, py in peaks:
        pts.append((int(px * w), horizon - int(py * horizon)))
    pts += [(w, horizon), (w, h), (0, h)]
    shadow = lerp_color(pal["sky"], (0,0,0), 0.55)
    d.polygon(pts, fill=shadow)

    # Near ridge
    near = [(rng.uniform(0, 1), rng.uniform(0.03, 0.14)) for _ in range(6)]
    near.sort()
    pts2 = [(0, horizon)]
    for px, py in near:
        pts2.append((int(px * w), horizon - int(py * horizon)))
    pts2 += [(w, horizon), (w, h), (0, h)]
    d.polygon(pts2, fill=pal["ground"])

    # Label (filename watermark)
    d.text((8, 8), f"preview_{seed:02d}.jpg", fill=(255,255,255))

    return img

def rational(numerator, denominator=1):
    return (int(numerator), int(denominator))

def make_exif(i: int) -> bytes:
    rng = random.Random(i + 1000)
    year  = rng.randint(2020, 2025)
    month = rng.randint(1, 12)
    day   = rng.randint(1, 28)
    hour  = rng.randint(6, 20)
    minute= rng.randint(0, 59)
    sec   = rng.randint(0, 59)
    dt_str = f"{year}:{month:02d}:{day:02d} {hour:02d}:{minute:02d}:{sec:02d}"

    make  = CAMERA_MAKES[i % len(CAMERA_MAKES)].encode()
    model = CAMERA_MODELS[i % len(CAMERA_MODELS)].encode()
    lens  = LENS_MODELS[i % len(LENS_MODELS)].encode()

    iso   = rng.choice([100, 200, 400, 800, 1600, 3200])
    focal = rng.randint(24, 200)
    ap_n  = rng.choice([14, 18, 28, 40, 56])   # f/1.4, 1.8, 2.8, 4.0, 5.6
    ss_n  = rng.choice([1, 1, 1, 1])            # 1/xx
    ss_d  = rng.choice([60, 125, 250, 500, 1000, 2000])

    exif_ifd = {
        piexif.ExifIFD.DateTimeOriginal:  dt_str.encode(),
        piexif.ExifIFD.DateTimeDigitized: dt_str.encode(),
        piexif.ExifIFD.ISOSpeedRatings:   iso,
        piexif.ExifIFD.FocalLength:       rational(focal),
        piexif.ExifIFD.FNumber:           rational(ap_n, 10),
        piexif.ExifIFD.ExposureTime:      rational(ss_n, ss_d),
        piexif.ExifIFD.LensModel:         lens,
        piexif.ExifIFD.PixelXDimension:   4000,
        piexif.ExifIFD.PixelYDimension:   2667,
    }
    zeroth = {
        piexif.ImageIFD.Make:     make,
        piexif.ImageIFD.Model:    model,
        piexif.ImageIFD.DateTime: dt_str.encode(),
    }
    gps_lat  = rng.uniform(35.0, 50.0)
    gps_lon  = rng.uniform(-120.0, 20.0)
    gps_ifd  = {
        piexif.GPSIFD.GPSLatitudeRef:  b"N",
        piexif.GPSIFD.GPSLatitude:     [rational(int(abs(gps_lat))),
                                         rational(int((abs(gps_lat) % 1) * 60)),
                                         rational(0)],
        piexif.GPSIFD.GPSLongitudeRef: b"E" if gps_lon >= 0 else b"W",
        piexif.GPSIFD.GPSLongitude:    [rational(int(abs(gps_lon))),
                                         rational(int((abs(gps_lon) % 1) * 60)),
                                         rational(0)],
        piexif.GPSIFD.GPSAltitude:     rational(rng.randint(0, 3000)),
    }
    return piexif.dump({"0th": zeroth, "Exif": exif_ifd, "GPS": gps_ifd})

N = 20
print(f"Generating {N} preview JPEGs → {OUT_DIR}/")
for i in range(N):
    pal   = PALETTES[i % len(PALETTES)]
    scene = render_scene(800, 533, pal, seed=i)
    exif  = make_exif(i)
    path  = os.path.join(OUT_DIR, f"preview_{i:02d}.jpg")
    scene.save(path, "JPEG", quality=90, exif=exif)
    print(f"  {os.path.basename(path)}")

print("Done.")
