#!/usr/bin/env python3
"""Cut the pieces of Groucho Time out of the public-domain pictures in assets/src, in
the manner of Terry Gilliam's animations: scissors, a white paper edge, and parts
that hinge and slide.

  father-time.jpg       "Design for Clock" (Met 358177, CC0): Father Time sitting
                        on a clock. Cut from its paper as one piece, the monument.
  groucho-1931.jpg      Groucho Marx, 1931 (Library of Congress, public domain):
                        his head, cut along a traced outline into pieces: the head
                        with holes where his eyes are, the top of his head (which
                        flips open on a hinge), his two eyebrows, and his irises.
  four-marx-bros-1913.png  The four Marx Brothers in a New Orleans paper, 1913
                        (public domain): Gummo, front right, cut out at the chest.

Writes assets/art/*.png and src/cutouts.h (where everything goes), and
src/art.h (every picture, for the program to build in).

    tools/make_cutouts.py
"""
import os
from collections import deque
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageOps

SRC, OUT = "assets/src", "assets/art"
EDGE = 5                                   # the white paper edge, in output pixels
PAPER = (246, 242, 230)

# ---- Groucho's head, in the 1931 photograph's pixels ----
HEAD = [(230, 70), (300, 30), (380, 15), (450, 18), (520, 38), (600, 78), (648, 128), (664, 200),
        (660, 290), (652, 340), (668, 352), (682, 400), (674, 470), (652, 505), (648, 520),
        (626, 548), (600, 600), (566, 648), (600, 690), (604, 745), (262, 745), (266, 690),
        (290, 640), (262, 590), (240, 525), (215, 510), (196, 452), (194, 382), (210, 362),
        (206, 300), (194, 200), (196, 120)]
FLAP_Y0, FLAP_Y1 = 222, 205                # the cut across his forehead, left end to right end
HINGE = (200, 222)                         # the flap turns about his left temple
BROWS = {"brow_l": (262, 230, 354, 268), "brow_r": (410, 220, 506, 256)}
EYES = {"eye_l": (301, 330, 37, 16), "eye_r": (483, 322, 35, 15)}   # the openings: middle, half width, half height
IRIS_R = 17

# ---- Father Time's clock, in the engraving's pixels ----
DIAL = (1356, 1990, 115)                   # the dial's middle and the length of the minute hand
HEAD_AT = (1270, 990)                      # where Groucho's chin goes, on Father Time
MONUMENT_SCALE = 0.62

# ---- Chico, Harpo and Zeppo, from the four brothers in 1931 (public domain), in
# that photograph's pixels: each head's outline and where its chin is ----
BROTHERS = {
    "chico": ([(1230, 330), (1280, 220), (1400, 165), (1550, 170), (1680, 240), (1730, 350), (1722, 470),
               (1702, 560), (1680, 640), (1620, 720), (1520, 770), (1420, 762), (1340, 700), (1290, 620),
               (1260, 520), (1232, 440)], (1470, 768)),
    "harpo": ([(940, 1000), (980, 850), (1080, 760), (1230, 715), (1400, 730), (1520, 800), (1570, 920),
               (1560, 1060), (1520, 1150), (1488, 1250), (1458, 1350), (1400, 1430), (1300, 1470),
               (1200, 1440), (1130, 1360), (1080, 1260), (1000, 1200), (950, 1120)], (1300, 1470)),
    "zeppo": ([(1150, 2700), (1180, 2560), (1280, 2480), (1420, 2460), (1560, 2500), (1640, 2600),
               (1660, 2740), (1650, 2880), (1620, 2990), (1580, 3100), (1520, 3190), (1420, 3240),
               (1320, 3220), (1240, 3140), (1190, 3020), (1160, 2880)], (1420, 3240)),
}
SPHINX_CHIN = (1345, 955)                  # where Harpo's chin goes, on the monument picture

# ---- Gummo, in the 1913 newspaper page's pixels ----
GUMMO = [(1430, 880), (1500, 858), (1600, 855), (1680, 878), (1740, 928), (1766, 1000), (1762, 1080),
         (1745, 1140), (1722, 1200), (1702, 1280), (1684, 1338), (1702, 1368), (1782, 1398), (1832, 1458),
         (1842, 1560), (1360, 1560), (1380, 1460), (1440, 1400), (1490, 1368), (1462, 1300), (1432, 1220),
         (1420, 1130), (1410, 1040), (1405, 960)]


def flood_background(rgb, start_mask, is_bg):
    """The background reached from the pixels in start_mask over pixels where is_bg."""
    H, W = is_bg.shape
    seen = np.zeros((H, W), bool)
    q = deque()
    for y, x in zip(*np.nonzero(start_mask & is_bg)):
        seen[y, x] = True; q.append((y, x))
    while q:
        y, x = q.popleft()
        for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
            if 0 <= ny < H and 0 <= nx < W and is_bg[ny, nx] and not seen[ny, nx]:
                seen[ny, nx] = True; q.append((ny, nx))
    return seen

def components(mask):
    H, W = mask.shape
    lab = np.zeros((H, W), np.int32); n = 0; sizes = [0]
    for y0, x0 in zip(*np.nonzero(mask)):
        if lab[y0, x0]: continue
        n += 1; lab[y0, x0] = n; st = [(y0, x0)]; c = 0
        while st:
            y, x = st.pop(); c += 1
            for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if 0 <= ny < H and 0 <= nx < W and mask[ny, nx] and not lab[ny, nx]:
                    lab[ny, nx] = n; st.append((ny, nx))
        sizes.append(c)
    return lab, sizes

def with_edge(rgba, width=EDGE):
    """A white paper edge round the outline, as a cut-out has."""
    a = Image.fromarray(rgba[..., 3])
    grown = np.asarray(a.filter(ImageFilter.MaxFilter(2 * width + 1)))
    out = np.zeros_like(rgba)
    out[grown > 0] = PAPER + (255,)
    keep = rgba[..., 3] > 0
    out[keep] = rgba[keep]
    return out

def pad(rgba, p):
    H, W = rgba.shape[:2]
    out = np.zeros((H + 2 * p, W + 2 * p, 4), np.uint8)
    out[p:p + H, p:p + W] = rgba
    return out

def save(rgba, name):
    Image.fromarray(rgba, "RGBA").save(f"{OUT}/{name}.png", optimize=True)
    print(f"{name}.png {rgba.shape[1]}x{rgba.shape[0]}")

def poly_mask(size, pts):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).polygon(pts, fill=255)
    return np.asarray(m) > 0

def fill_in(rgb, hole):
    """Paint over `hole` with the skin round it, blended in from its edges (each pixel
    in the hole becomes the average of its neighbours, over and over), and a little
    of the photograph's grain put back."""
    out = rgb.astype(float).copy()
    ys, xs = np.nonzero(hole)
    y0, y1, x0, x1 = max(ys.min() - 2, 0), ys.max() + 3, max(xs.min() - 2, 0), xs.max() + 3
    sub = out[y0:y1, x0:x1]; h = hole[y0:y1, x0:x1]
    ring = np.asarray(Image.fromarray((h * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5))) > 0
    sub[h] = sub[ring & ~h].mean(0)
    for _ in range(400):
        avg = (np.roll(sub, 1, 0) + np.roll(sub, -1, 0) + np.roll(sub, 1, 1) + np.roll(sub, -1, 1)) / 4
        sub[h] = avg[h]
    grain = np.random.default_rng(7).normal(0, 3.5, h.shape)
    sub[h] += grain[h][:, None]
    out[y0:y1, x0:x1] = sub
    return np.clip(out, 0, 255).astype(np.uint8)

def monument(defs):
    im = Image.open(f"{SRC}/father-time.jpg").convert("RGB")
    W0, H0 = im.size
    q = 4                                                  # the background is found at a quarter size
    small = im.resize((W0 // q, H0 // q))
    blur = np.asarray(small.filter(ImageFilter.GaussianBlur(2))).astype(int)
    lum, sat = blur.mean(-1), blur.max(-1) - blur.min(-1)
    paper = (lum > 200) & (sat < 45)
    H, W = paper.shape
    edge = np.zeros((H, W), bool); edge[0, :] = edge[-1, :] = edge[:, 0] = edge[:, -1] = True
    bg = flood_background(blur, edge, paper)
    # paper shut in by the scythe and his arm, above the clock: background too
    yy, xx = np.mgrid[0:H, 0:W]
    dial_x, dial_y, _ = DIAL
    upper = (yy < 1700 // q) & ~(((xx - dial_x // q) ** 2 + (yy - dial_y // q) ** 2) < (230 // q) ** 2)
    lab, sizes = components(paper & ~bg & upper)
    for i, s in enumerate(sizes):
        if s > 300: bg |= lab == i
    # the monument is the biggest piece left, and it stops at the foot of the plinth
    keep = ~bg
    keep[3760 // q:] = False
    lab, sizes = components(keep)
    keep = lab == int(np.argmax(sizes))
    hole = ~keep                                             # and fill any holes in it
    lab, sizes = components(hole)
    outside = set(lab[0, :]) | set(lab[-1, :]) | set(lab[:, 0]) | set(lab[:, -1])
    for i in range(1, len(sizes)):
        if i not in outside: keep |= lab == i
    m = Image.fromarray((keep * 255).astype(np.uint8)).resize((W0, H0), Image.BILINEAR).filter(ImageFilter.GaussianBlur(3))
    m = np.asarray(m) > 128
    ys, xs = np.nonzero(m)
    x0, y0, x1, y1 = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
    rgba = np.dstack([np.asarray(im), (m * 255).astype(np.uint8)])[y0:y1, x0:x1]
    s = MONUMENT_SCALE
    out = np.asarray(Image.fromarray(rgba, "RGBA").resize((round((x1 - x0) * s), round((y1 - y0) * s)), Image.LANCZOS))
    out = pad(out, EDGE + 2)
    out = with_edge(out)
    save(out, "monument")
    o = EDGE + 2
    at = lambda x, y: ((x - x0) * s + o, (y - y0) * s + o)
    defs["MONUMENT"] = (out.shape[1], out.shape[0])
    defs["DIAL"] = at(DIAL[0], DIAL[1]) + (DIAL[2] * s,)
    defs["CHIN"] = at(*HEAD_AT)

def groucho(defs):
    im = Image.open(f"{SRC}/groucho-1931.jpg").convert("L")
    im = ImageOps.autocontrast(im, cutoff=0.5)
    W, H = im.size
    # a photograph printed on warm paper, as Gilliam's were
    g = np.asarray(im).astype(float)
    rgb = np.dstack([np.clip(g * 1.0 + 8, 0, 255), np.clip(g * 0.96 + 4, 0, 255), np.clip(g * 0.86, 0, 255)]).astype(np.uint8)
    head = poly_mask((W, H), HEAD)
    yy, xx = np.mgrid[0:H, 0:W]
    flap_line = FLAP_Y0 + (FLAP_Y1 - FLAP_Y0) * (xx - 190) / (680 - 190)
    flap = head & (yy < flap_line)
    # the eyebrows: the dark greasepaint in their boxes, lifted off, and his skin
    # painted in behind them (the colour of the forehead just above)
    face = rgb.copy()
    for name, (bx0, by0, bx1, by1) in BROWS.items():
        box = np.zeros((H, W), bool); box[by0:by1, bx0:bx1] = True
        dark = box & (g < 95)
        dark = np.asarray(Image.fromarray((dark * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5))) > 0
        dark &= box
        piece = np.zeros((H, W, 4), np.uint8); piece[dark] = np.dstack([rgb, np.full((H, W), 255, np.uint8)])[dark]
        wide = np.asarray(Image.fromarray((dark * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(11))) > 0
        face = fill_in(face, wide & (g < 200))
        ys, xs = np.nonzero(dark)
        a, b, c, d = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
        save(with_edge(pad(piece[b:d, a:c], 3), 2), name)
        defs[name.upper()] = (a - 3, b - 3)
    # the irises, and holes in the head where the eyes are
    holes = np.zeros((H, W), bool)
    for name, (ex, ey, rx, ry) in EYES.items():
        holes |= ((xx - ex) / rx) ** 2 + ((yy - ey) / ry) ** 2 < 1
        r = IRIS_R
        piece = np.zeros((2 * r, 2 * r, 4), np.uint8)
        piece[..., :3] = rgb[ey - r:ey + r, ex - r:ex + r]
        cy, cx = np.mgrid[0:2 * r, 0:2 * r]
        piece[..., 3] = np.where((cx - r + 0.5) ** 2 + (cy - r + 0.5) ** 2 < r * r, 255, 0)
        save(piece, name.replace("eye", "iris"))
        defs[name.upper()] = (ex, ey, rx, ry)
    def cut(mask, name, edge_mask=None):
        rgba = np.dstack([face, (mask * 255).astype(np.uint8)])
        ys, xs = np.nonzero(mask)
        a, b, c, d = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
        out = with_edge(pad(rgba[b:d, a:c], EDGE + 1))
        if edge_mask is not None:                            # no paper edge round the eye holes
            em = pad(np.dstack([edge_mask[b:d, a:c]] * 4).astype(np.uint8), EDGE + 1)[..., 0] > 0
            out[em] = 0
        save(out, name)
        return (a - EDGE - 1, b - EDGE - 1)
    grown_holes = np.asarray(Image.fromarray((holes * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(2 * EDGE + 1))) > 0
    defs["HEAD"] = cut(head & ~flap & ~holes, "head", holes)
    defs["FLAP"] = cut(flap, "flap")
    defs["HINGE"] = HINGE
    defs["PHOTO_CHIN"] = (432, 745)

def gummo(defs):
    im = Image.open(f"{SRC}/four-marx-bros-1913.png").convert("L")
    im = ImageOps.autocontrast(im, cutoff=0.5)
    g = np.asarray(im).astype(float)
    rgb = np.dstack([np.clip(g + 8, 0, 255), np.clip(g * 0.96 + 4, 0, 255), np.clip(g * 0.86, 0, 255)]).astype(np.uint8)
    W, H = im.size
    m = poly_mask((W, H), GUMMO)
    ys, xs = np.nonzero(m)
    a, b, c, d = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
    out = with_edge(pad(np.dstack([rgb, (m * 255).astype(np.uint8)])[b:d, a:c], EDGE + 1))
    save(out, "gummo")

def brothers(defs):
    im = Image.open(f"{SRC}/marx-brothers-1931.jpg").convert("L")
    im = ImageOps.autocontrast(im, cutoff=0.5)
    g = np.asarray(im).astype(float)
    rgb = np.dstack([np.clip(g + 8, 0, 255), np.clip(g * 0.96 + 4, 0, 255), np.clip(g * 0.86, 0, 255)]).astype(np.uint8)
    W, H = im.size
    for name, (pts, chin) in BROTHERS.items():
        m = poly_mask((W, H), pts)
        ys, xs = np.nonzero(m)
        a, b, c, d = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
        # these are big: halved, so that they are near Groucho's size
        rgba = np.dstack([rgb, (m * 255).astype(np.uint8)])[b:d, a:c]
        half = np.asarray(Image.fromarray(rgba, "RGBA").resize(((c - a) // 2, (d - b) // 2), Image.LANCZOS))
        out = with_edge(pad(half, EDGE + 1))
        save(out, name)
        defs[name.upper() + "_CHIN"] = ((chin[0] - a) / 2 + EDGE + 1, (chin[1] - b) / 2 + EDGE + 1)
    defs["SPHINX_CHIN"] = SPHINX_CHIN

def write_lists(defs):
    lines = ["/* Made by tools/make_cutouts.py: where the pieces go, in their pictures' pixels. */"]
    for k, v in defs.items():
        for i, n in enumerate(v):
            suffix = "XYZW"[i] if len(v) > 2 else "XY"[i]
            if k in ("EYE_L", "EYE_R"): suffix = ("X", "Y", "RX", "RY")[i]
            if k == "DIAL": suffix = ("X", "Y", "R")[i]
            if k == "MONUMENT": suffix = ("W", "H")[i]
            lines.append(f"#define {k}_{suffix} {n:.1f}")
    open("src/cutouts.h", "w").write("\n".join(lines) + "\n")
    files = sorted(f for f in os.listdir(OUT) if f.endswith((".png", ".jpg")))
    items = [f'X({os.path.splitext(f)[0].replace("-", "_")}, "{f}")' for f in files]
    lines = ["/* Made by tools/make_cutouts.py: every picture in assets/art. */", "#define SPRITES \\"]
    for i in range(0, len(items), 4):
        lines.append("    " + " ".join(items[i:i + 4]) + (" \\" if i + 4 < len(items) else ""))
    open("src/art.h", "w").write("\n".join(lines) + "\n")
    print(f"src/art.h: {len(files)} pictures")

def main():
    os.makedirs(OUT, exist_ok=True)
    defs = {}
    monument(defs)
    groucho(defs)
    gummo(defs)
    brothers(defs)
    write_lists(defs)

if __name__ == "__main__":
    main()
