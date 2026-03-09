#!/usr/bin/env python3
"""Generate PhotoLibrary.icns — vibrant camera-with-landscape icon."""
import math, os, shutil, subprocess, sys
from PIL import Image, ImageDraw, ImageFilter

def lerp(a, b, t):
    return a + (b - a) * t

def lerp_color(c1, c2, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(lerp(a, b, t)) for a, b in zip(c1, c2))

def create_icon(size: int) -> Image.Image:
    s = size
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))

    # ── Background: deep teal → indigo gradient ────────────────────────────
    bg = Image.new("RGBA", (s, s), (0, 0, 0, 255))
    bd = ImageDraw.Draw(bg)
    top_col    = (18,  90, 140)   # ocean teal
    bottom_col = (40,  35,  90)   # deep indigo
    for y in range(s):
        t = y / (s - 1)
        c = lerp_color(top_col, bottom_col, t)
        bd.line([(0, y), (s - 1, y)], fill=c + (255,))

    # Rounded-rect mask for background
    radius = int(s * 0.22)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [0, 0, s - 1, s - 1], radius=radius, fill=255
    )
    bg.putalpha(mask)
    img.paste(bg, (0, 0), bg)

    draw = ImageDraw.Draw(img, "RGBA")

    # ── Camera body ────────────────────────────────────────────────────────
    pad    = int(s * 0.09)
    body_h = int(s * 0.54)
    body_y = int(s * 0.28)
    body_r = int(s * 0.055)

    # Main body
    draw.rounded_rectangle(
        [pad, body_y, s - pad, body_y + body_h],
        radius=body_r,
        fill=(245, 248, 255, 255),
    )
    # Viewfinder bump
    bw = int(s * 0.26)
    bh = int(s * 0.09)
    bx = int(s * 0.38)
    draw.rounded_rectangle(
        [bx, body_y - bh, bx + bw, body_y + body_r],
        radius=int(s * 0.035),
        fill=(245, 248, 255, 255),
    )

    # ── Lens ──────────────────────────────────────────────────────────────
    cx = s // 2
    cy = body_y + int(body_h * 0.56)
    outer_r = int(s * 0.205)

    # Outer dark bezel
    draw.ellipse(
        [cx - outer_r, cy - outer_r, cx + outer_r, cy + outer_r],
        fill=(35, 35, 50, 255),
    )
    # Chrome ring
    chrome_r = int(outer_r * 0.90)
    chrome_w = max(4, int(outer_r * 0.08))
    draw.ellipse(
        [cx - chrome_r, cy - chrome_r, cx + chrome_r, cy + chrome_r],
        fill=(210, 218, 235, 255),
    )

    # Interior: sky + mountain landscape
    inner_r = chrome_r - chrome_w
    lsz = inner_r * 2

    lens = Image.new("RGBA", (lsz, lsz), (0, 0, 0, 0))
    ld   = ImageDraw.Draw(lens)

    # Sky gradient — warm golden-hour sky
    sky_top = (30, 100, 200)
    sky_bot = (130, 200, 240)
    horiz   = int(lsz * 0.60)
    for y in range(horiz + 1):
        t = y / max(1, horiz)
        c = lerp_color(sky_top, sky_bot, t)
        ld.line([(0, y), (lsz, y)], fill=c + (255,))

    # Ground
    gnd_top = (34, 120, 60)
    gnd_bot = (18,  70, 35)
    for y in range(horiz, lsz):
        t = (y - horiz) / max(1, lsz - horiz)
        c = lerp_color(gnd_top, gnd_bot, t)
        ld.line([(0, y), (lsz, y)], fill=c + (255,))

    # Sun glow
    sx, sy = int(lsz * 0.72), int(lsz * 0.22)
    sr = int(lsz * 0.09)
    for gr in range(sr * 4, sr - 1, -2):
        t   = max(0.0, 1.0 - (gr - sr) / (sr * 3))
        alp = int(t * t * 180)
        ld.ellipse([sx-gr, sy-gr, sx+gr, sy+gr], fill=(255, 200, 60, alp))
    ld.ellipse([sx-sr, sy-sr, sx+sr, sy+sr], fill=(255, 230, 110, 255))

    # Far mountains (blue-gray)
    far_pts = [(0, horiz)]
    for px in [0, 12, 25, 40, 58, 73, 88, 100]:
        x = int(px / 100 * lsz)
        py_vals = [18, 30, 15, 27, 12, 25, 18, 20]
        y = horiz - int(py_vals[far_pts.__len__() - 1] / 100 * lsz)
        far_pts.append((x, y))
    far_pts = [
        (0,              horiz - int(0.18 * lsz)),
        (int(0.15 * lsz), horiz - int(0.30 * lsz)),
        (int(0.32 * lsz), horiz - int(0.12 * lsz)),
        (int(0.52 * lsz), horiz - int(0.28 * lsz)),
        (int(0.70 * lsz), horiz - int(0.14 * lsz)),
        (int(0.88 * lsz), horiz - int(0.24 * lsz)),
        (lsz,             horiz - int(0.08 * lsz)),
        (lsz, horiz + 2), (0, horiz + 2),
    ]
    ld.polygon(far_pts, fill=(55, 90, 115, 230))

    # Near mountains (dark green)
    near_pts = [
        (0,              horiz - int(0.08 * lsz)),
        (int(0.12 * lsz), horiz - int(0.22 * lsz)),
        (int(0.30 * lsz), horiz - int(0.18 * lsz)),
        (int(0.45 * lsz), horiz - int(0.05 * lsz)),
        (int(0.60 * lsz), horiz - int(0.20 * lsz)),
        (int(0.78 * lsz), horiz - int(0.10 * lsz)),
        (int(0.90 * lsz), horiz - int(0.16 * lsz)),
        (lsz,             horiz - int(0.04 * lsz)),
        (lsz, horiz + 2), (0, horiz + 2),
    ]
    ld.polygon(near_pts, fill=(28, 75, 40, 255))

    # Circular clip mask
    lmask = Image.new("L", (lsz, lsz), 0)
    ImageDraw.Draw(lmask).ellipse([0, 0, lsz - 1, lsz - 1], fill=255)
    lens.putalpha(lmask)

    # Lens glare overlay (drawn on transparent bg — no putalpha override)
    glare = Image.new("RGBA", (lsz, lsz), (0, 0, 0, 0))
    for r in range(inner_r, 0, -3):
        t   = r / inner_r
        alp = int((1 - t) * 25)
        ImageDraw.Draw(glare).ellipse(
            [inner_r - r, inner_r - r, inner_r + r, inner_r + r],
            fill=(255, 255, 255, alp),
        )
    lens = Image.alpha_composite(lens, glare)

    # Highlight arc (top-left) — drawn on transparent bg, no putalpha
    hl = Image.new("RGBA", (lsz, lsz), (0, 0, 0, 0))
    hl_r = int(inner_r * 0.60)
    hl_x = int(inner_r * 0.30)
    hl_y = int(inner_r * 0.25)
    ImageDraw.Draw(hl).ellipse(
        [hl_x, hl_y, hl_x + hl_r, hl_y + hl_r],
        fill=(255, 255, 255, 35),
    )
    lens = Image.alpha_composite(lens, hl)

    img.paste(lens, (cx - inner_r, cy - inner_r), lens)

    # Re-draw chrome ring edge on top
    draw = ImageDraw.Draw(img, "RGBA")
    draw.ellipse(
        [cx - chrome_r, cy - chrome_r, cx + chrome_r, cy + chrome_r],
        outline=(220, 228, 245, 220),
        width=max(2, chrome_w // 2),
    )

    # ── Flash / indicator LED ──────────────────────────────────────────────
    fr  = max(3, int(s * 0.022))
    fx  = int(pad + s * 0.09)
    fy  = body_y + int(body_h * 0.28)
    for gr in range(fr * 3, fr - 1, -1):
        t   = max(0.0, 1.0 - (gr - fr) / (fr * 2))
        alp = int(t * t * 120)
        draw.ellipse([fx-gr, fy-gr, fx+gr, fy+gr], fill=(255, 185, 30, alp))
    draw.ellipse([fx-fr, fy-fr, fx+fr, fy+fr], fill=(255, 210, 60, 240))

    # ── Shutter button ─────────────────────────────────────────────────────
    sb_w = int(s * 0.07)
    sb_h = int(s * 0.032)
    sb_x = int(s * 0.455)
    sb_y = body_y - int(s * 0.09) + int(s * 0.025)
    draw.rounded_rectangle(
        [sb_x, sb_y, sb_x + sb_w, sb_y + sb_h],
        radius=int(sb_h * 0.5),
        fill=(190, 200, 225, 200),
    )

    # ── Subtle edge shadow on camera body (depth) ──────────────────────────
    # Redraw body outline in a very slightly darker tone
    draw.rounded_rectangle(
        [pad, body_y, s - pad, body_y + body_h],
        radius=body_r,
        outline=(200, 210, 235, 180),
        width=max(2, int(s * 0.008)),
    )

    # ── Subtle inner-shadow vignette at edges ─────────────────────────────
    # Composite thin dark bands from the edge inward (no putalpha override)
    vig = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    vd  = ImageDraw.Draw(vig)
    steps = 18
    for i in range(steps, 0, -1):
        t     = i / steps
        inset = int((1.0 - t) * s * 0.12)   # only affect outer 12%
        alp   = int(t * 55)
        r_v   = max(1, radius - inset)
        vd.rounded_rectangle(
            [inset, inset, s - 1 - inset, s - 1 - inset],
            radius=r_v,
            outline=(0, 0, 0, alp),
            width=1,
        )
    img = Image.alpha_composite(img, vig)

    return img

# ── Generate all iconset sizes ────────────────────────────────────────────────
ICONSET_SIZES = [
    ("icon_16x16.png",       16),
    ("icon_16x16@2x.png",    32),
    ("icon_32x32.png",       32),
    ("icon_32x32@2x.png",    64),
    ("icon_128x128.png",    128),
    ("icon_128x128@2x.png", 256),
    ("icon_256x256.png",    256),
    ("icon_256x256@2x.png", 512),
    ("icon_512x512.png",    512),
    ("icon_512x512@2x.png",1024),
]

out_dir  = os.path.dirname(os.path.abspath(__file__))
iconset  = os.path.join(out_dir, "PhotoLibrary.iconset")
icns_out = os.path.join(out_dir, "PhotoLibrary.icns")

os.makedirs(iconset, exist_ok=True)
print("Rendering master at 1024×1024…")
master = create_icon(1024)

for fname, sz in ICONSET_SIZES:
    path = os.path.join(iconset, fname)
    img  = master.copy() if sz == 1024 else master.resize((sz, sz), Image.LANCZOS)
    img.save(path, "PNG")
    print(f"  {fname}  ({sz}px)")

print("Converting to .icns…")
res = subprocess.run(
    ["iconutil", "-c", "icns", "-o", icns_out, iconset],
    capture_output=True, text=True,
)
if res.returncode != 0:
    print("iconutil error:", res.stderr, file=sys.stderr); sys.exit(1)

shutil.rmtree(iconset)
print(f"Done → {icns_out}")
