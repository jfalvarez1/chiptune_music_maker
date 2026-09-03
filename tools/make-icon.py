"""Draw the ChiptuneTracker icon as pixel art, and build a Windows .ico.

Hand-drawn rather than generated, for a reason that is technical rather
than aesthetic: an app icon has to read at 16x16 in a taskbar and a title
bar, and a large image downscaled to 16 pixels is mush. Pixel art is drawn
at the size it is shown, and scales UP by integer factors with no loss - so
one 32x32 master serves 32, 64, 128 and 256 exactly, and a 16x16 master
serves 16 and 48.

The subject is a pulse wave, which is the sound a 2A03 is known for and the
first thing this program makes. Two of them, because the NES had two pulse
channels, in the two colours the app's own themes use for channels 1 and 2.
"""
import io
import os

from PIL import Image

# The palette. Dark navy ground, the two channel colours, and a frame that
# reads as a screen bezel without drawing attention to itself.
BG      = (18, 18, 30, 255)
FRAME   = (44, 46, 78, 255)
PULSE_A = (74, 227, 181, 255)    # mint - channel 1
PULSE_B = (231, 108, 189, 255)   # pink - channel 2
GLOW    = (30, 34, 56, 255)
CLEAR   = (0, 0, 0, 0)


def blank(size):
    return Image.new("RGBA", (size, size), CLEAR)


def rounded_ground(image, size, inset=0):
    """A filled square with the four corner pixels dropped.

    A one-pixel corner notch is all it takes to stop a small icon looking
    like a rectangle of colour, and it survives every scale factor because
    it is part of the artwork rather than an anti-aliased radius.
    """
    px = image.load()
    lo = inset
    hi = size - 1 - inset
    for y in range(lo, hi + 1):
        for x in range(lo, hi + 1):
            corner = ((x == lo or x == hi) and (y == lo or y == hi))
            if corner:
                continue
            edge = (x == lo or x == hi or y == lo or y == hi)
            px[x, y] = FRAME if edge else BG


def hline(px, x0, x1, y, colour):
    for x in range(x0, x1 + 1):
        px[x, y] = colour


def vline(px, x, y0, y1, colour):
    for y in range(min(y0, y1), max(y0, y1) + 1):
        px[x, y] = colour


def pulse(px, points, colour, thickness=1):
    """Draw a square wave from (x, level) points.

    `points` is a list of (start_x, end_x, y). Consecutive segments are
    joined with a vertical edge, which is what makes a square wave look
    square rather than like a dotted line.
    """
    previous = None
    for (x0, x1, y) in points:
        for t in range(thickness):
            hline(px, x0, x1, y + t, colour)
        if previous is not None:
            py, px0 = previous
            for t in range(thickness):
                vline(px, x0, min(py, y) + t, max(py, y) + t, colour)
        previous = (y, x0)


def master16():
    """The 16x16 master. Serves 16 and, tripled, 48.

    Deliberately not a shrunken version of the 32. At this size there are
    fourteen usable pixels across, so the wave is drawn two pixels thick and
    everything else is removed: the second wave, the inner glow and the
    playhead all became noise rather than detail. An icon that is legible at
    16 and plain at 256 is a better icon than the reverse.
    """
    image = blank(16)
    px = image.load()
    rounded_ground(image, 16)

    # One bold pulse wave, two pixels thick so it survives being 16 pixels
    # tall on a bright taskbar as well as a dark one.
    pulse(px, [(2, 5, 4), (6, 9, 9), (10, 13, 4)], PULSE_A, thickness=2)
    return image


def master32():
    """The 32x32 master. Serves 32, 64, 128 and 256 by doubling."""
    image = blank(32)
    px = image.load()
    rounded_ground(image, 32)

    # A soft inner glow, so the waves sit in a screen rather than on a flat
    # square. One shade, because a gradient would not survive 8x scaling as
    # anything but banding.
    for y in range(3, 29):
        for x in range(3, 29):
            if 4 <= x <= 27 and 6 <= y <= 25:
                px[x, y] = GLOW

    # Two pulse waves, for the two pulse channels a 2A03 had. The second is
    # a narrower duty cycle, which is a real difference between them and not
    # only a visual one.
    pulse(px, [(4, 10, 11), (11, 17, 19), (18, 24, 11), (25, 27, 19)],
          PULSE_A, thickness=2)

    # Two pixels thick as well, so it reads as the other channel rather than
    # as a hairline that disappears the moment the icon is scaled down.
    pulse(px, [(4, 7, 21), (8, 16, 25), (17, 20, 21), (21, 27, 25)],
          PULSE_B, thickness=2)

    return image


def upscale(image, factor):
    size = image.width * factor
    return image.resize((size, size), Image.NEAREST)


def build(out_dir):
    os.makedirs(out_dir, exist_ok=True)

    small = master16()
    large = master32()

    # Every size from an integer multiple of a master, so nothing is ever
    # resampled to a fractional grid.
    sizes = {
        16:  small,
        32:  large,
        48:  upscale(small, 3),
        64:  upscale(large, 2),
        128: upscale(large, 4),
        256: upscale(large, 8),
    }

    for size, image in sizes.items():
        image.save(os.path.join(out_dir, "icon_%d.png" % size))

    ico_path = os.path.join(out_dir, "ChiptuneTracker.ico")
    # Pillow writes a multi-image .ico from the largest and a size list, but
    # that resamples. Passing the frames explicitly keeps each hand-made.
    sizes[256].save(
        ico_path,
        format="ICO",
        sizes=[(s, s) for s in sorted(sizes)],
        append_images=[sizes[s] for s in sorted(sizes) if s != 256],
    )

    # A flat PNG for the README and anywhere else a picture is wanted.
    sizes[256].save(os.path.join(out_dir, "icon.png"))

    print("wrote", ico_path)
    for size in sorted(sizes):
        print("  %dx%d" % (size, size))


if __name__ == "__main__":
    import sys
    build(sys.argv[1] if len(sys.argv) > 1 else ".")
