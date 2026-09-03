"""A synthwave app-tile icon, in the modern rounded-square style.

The shape language is the one every modern music app uses - a rounded
square, a strong gradient, one bold centred subject - and the subject is
the synthwave sunset: a sliced sun over a perspective grid.

DRAWN, not generated, and at two levels of detail. A tile like this has
gradients and glow, so the large sizes are supersampled and downscaled for
smooth edges. The small ones cannot be: at 16 pixels a grid and a horizon
are three grey smudges, so 16 and 32 get their own simplified drawing with
a bigger sun and no grid at all. Same colours, same subject, less of it.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

# The synthwave palette. Deep indigo at the top through magenta to a hot
# orange at the horizon, which is the gradient the whole look rests on.
SKY_TOP    = (26, 11, 61)
SKY_MID    = (94, 23, 122)
SKY_LOW    = (214, 41, 128)
HORIZON    = (255, 122, 51)

SUN_TOP    = (255, 233, 122)
SUN_BOTTOM = (255, 61, 122)

GRID       = (94, 231, 255)
GROUND_TOP = (36, 12, 66)
GROUND_LOW = (12, 5, 28)


# The pepper's own colours. A dark body so it reads against the sun, and a
# neon rim so it does not become a hole in the artwork.
PEPPER_BODY  = (26, 8, 38)
PEPPER_RIM   = (255, 61, 140)
PEPPER_RIM2  = (94, 231, 255)
PEPPER_STEM  = (74, 222, 128)


def pepper_outline(cx, cy, height):
    """Points around a chilli pepper, as a closed polygon.

    A curved centreline with a width that starts full and tapers to a
    point. The proportions are the whole game: at five to one it reads as a
    dagger, and the sideways bend is what separates a chilli from a carrot.
    """
    steps = 56
    left, right = [], []

    max_w = height * 0.30      # about two and a half to one overall

    for i in range(steps + 1):
        t = i / steps                       # 0 at the shoulder, 1 at the tip

        # A clear arc to one side and back. Shallower than this and the
        # shape reads as a root vegetable.
        bend = math.sin(t * math.pi * 0.92) * height * 0.30
        x = cx + bend
        y = cy + t * height

        # Full at the shoulder, pinching to nothing. The square root keeps
        # the top rounded rather than conical.
        w = max_w * math.sqrt(max(0.0, 1.0 - t ** 1.7))

        # The tip is drawn as a point rather than a stub.
        if t > 0.93:
            w *= (1.0 - t) / 0.07

        left.append((x - w, y))
        right.append((x + w, y))

    return left + list(reversed(right))


def draw_pepper(image, size, detailed):
    """The subject, over the scene.

    Sized and placed to sit INSIDE the sun rather than across the horizon:
    a silhouette that crosses the boundary cuts the tile in half, and the
    contrast that makes it legible comes from the bright disc behind it.
    """
    draw = ImageDraw.Draw(image, "RGBA")

    height = size * (0.34 if detailed else 0.40)
    cx = size * (0.46 if detailed else 0.44)
    cy = size * (0.20 if detailed else 0.16)

    points = pepper_outline(cx, cy, height)

    rim = max(1, int(size * 0.018))
    draw.polygon(points, fill=PEPPER_BODY, outline=PEPPER_RIM, width=rim)

    if detailed:
        # A cool rim down the left, so a flat silhouette looks lit from
        # behind rather than pasted on.
        edge = points[:len(points) // 2]
        draw.line(edge, fill=PEPPER_RIM2,
                  width=max(1, int(size * 0.010)), joint="curve")

    # ---- Stem ----------------------------------------------------------------
    #
    # Short, angled, and thick enough to survive. A long stem at 16 pixels
    # is one stray green pixel that reads as damage.
    stem_w = max(1, int(size * (0.034 if detailed else 0.050)))
    stem_top = (cx - size * 0.055, cy - size * 0.075)
    draw.line([(cx, cy + height * 0.04), stem_top],
              fill=PEPPER_STEM, width=stem_w)

    if detailed:
        # A small leaf where the stem meets the fruit.
        leaf = size * 0.048
        draw.ellipse([cx - leaf * 1.5, cy - leaf * 0.5,
                      cx + leaf * 0.9, cy + leaf * 0.7],
                     fill=PEPPER_STEM)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def sky_colour(t):
    """The vertical gradient, in three legs so the sunset bends properly."""
    if t < 0.45:
        return lerp(SKY_TOP, SKY_MID, t / 0.45)
    if t < 0.80:
        return lerp(SKY_MID, SKY_LOW, (t - 0.45) / 0.35)
    return lerp(SKY_LOW, HORIZON, (t - 0.80) / 0.20)


def rounded_mask(size, radius):
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [0, 0, size - 1, size - 1], radius=radius, fill=255)
    return mask


def draw_tile(size, detailed):
    """One icon at `size`. `detailed` adds the grid, horizon glow and slices."""
    image = Image.new("RGB", (size, size), SKY_TOP)
    draw = ImageDraw.Draw(image)

    horizon_y = int(size * 0.62)

    # ---- Sky -----------------------------------------------------------------
    for y in range(horizon_y):
        draw.line([(0, y), (size, y)], fill=sky_colour(y / max(1, horizon_y)))

    # ---- Sun -----------------------------------------------------------------
    #
    # Sized so it still reads when the tile is tiny: at small sizes it is
    # most of the artwork, because a grid and a horizon at 16 pixels are
    # three grey smudges.
    sun_r = size * (0.34 if detailed else 0.40)
    cx = size * 0.5
    cy = horizon_y - sun_r * (0.28 if detailed else 0.18)

    top = int(cy - sun_r)
    bottom = int(cy + sun_r)
    for y in range(max(0, top), min(horizon_y, bottom + 1)):
        dy = (y - cy) / sun_r
        if abs(dy) > 1.0:
            continue
        half = sun_r * (1.0 - dy * dy) ** 0.5
        colour = lerp(SUN_TOP, SUN_BOTTOM, (y - top) / max(1.0, bottom - top))
        draw.line([(cx - half, y), (cx + half, y)], fill=colour)

    if detailed:
        # The slices: horizontal cuts through the lower half of the sun,
        # widening as they descend. This is the single detail that makes a
        # circle read as a synthwave sun rather than as a dot.
        slices = 5
        for i in range(slices):
            t = (i + 1) / (slices + 1)
            y = cy + sun_r * t * 0.95
            thickness = max(1, int(size * (0.006 + 0.010 * t)))
            dy = (y - cy) / sun_r
            if abs(dy) >= 1.0:
                continue
            half = sun_r * (1.0 - dy * dy) ** 0.5
            draw.rectangle([cx - half, y, cx + half, y + thickness],
                           fill=sky_colour(y / max(1, horizon_y)))

    # ---- Ground --------------------------------------------------------------
    for y in range(horizon_y, size):
        t = (y - horizon_y) / max(1, size - horizon_y)
        draw.line([(0, y), (size, y)], fill=lerp(GROUND_TOP, GROUND_LOW, t))

    if detailed:
        # A bright horizon line, which is what separates sky from ground and
        # gives the tile its glow.
        glow = max(1, int(size * 0.012))
        draw.rectangle([0, horizon_y - glow // 2, size, horizon_y + glow // 2],
                       fill=(255, 210, 170))

        # ---- Perspective grid ------------------------------------------------
        line_w = max(1, int(size * 0.008))

        # Fewer lines when the tile is small: six converging lines at 32
        # pixels is a smear, and three reads as a grid.
        small = size < 64 * 4      # `size` here is the supersampled width
        spread = 3 if small else 6
        rows = 3 if small else 6

        # Verticals, converging on a vanishing point at the horizon.
        for i in range(-spread, spread + 1):
            x_bottom = cx + i * size * (0.34 if small else 0.22)
            draw.line([(cx, horizon_y), (x_bottom, size)],
                      fill=GRID, width=line_w)

        # Horizontals, spaced so they bunch up toward the horizon.
        for i in range(1, rows + 1):
            t = (i / rows) ** 2.1
            y = horizon_y + t * (size - horizon_y)
            draw.line([(0, y), (size, y)], fill=GRID, width=line_w)

    # The subject, last, over everything. A tile with only a scene has
    # nothing for the eye to land on, and at 16 pixels the scene is all that
    # survives unless something is in front of it.
    draw_pepper(image, size, detailed)

    return image


def build_size(size, supersample=4):
    """One finished icon, rounded and antialiased."""
    detailed = size >= 32

    # Supersampled so the circle, the grid and the corner radius are smooth.
    # The small sizes are drawn simplified but still supersampled, which is
    # what keeps the sun's edge clean at 16.
    big = size * supersample
    tile = draw_tile(big, detailed)

    # A soft bloom around the sun and grid, which is most of what makes this
    # look lit rather than flat.
    # Bloom is what makes the large tile look lit and what turns a small
    # one to fog - there are not enough pixels at 16 for a glow to be
    # anything but a loss of contrast. Scaled down accordingly.
    bloomAmount = 0.32 if size >= 64 else (0.18 if size >= 32 else 0.06)
    bloom = tile.filter(ImageFilter.GaussianBlur(radius=big * 0.02))
    tile = Image.blend(tile, bloom, bloomAmount)

    radius = int(big * 0.20)
    mask = rounded_mask(big, radius)

    out = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    out.paste(tile, (0, 0), mask)

    return out.resize((size, size), Image.LANCZOS)


def build(out_dir):
    os.makedirs(out_dir, exist_ok=True)

    sizes = [16, 32, 48, 64, 128, 256]
    images = {s: build_size(s) for s in sizes}

    for size, image in images.items():
        image.save(os.path.join(out_dir, "icon_%d.png" % size))

    ico_path = os.path.join(out_dir, "ChiptuneTracker.ico")
    images[256].save(
        ico_path,
        format="ICO",
        sizes=[(s, s) for s in sizes],
        append_images=[images[s] for s in sizes if s != 256],
    )
    images[256].save(os.path.join(out_dir, "icon.png"))

    print("wrote", ico_path)
    for size in sizes:
        print("  %dx%d%s" % (size, size, "" if size >= 48 else "  (simplified)"))


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else ".")
