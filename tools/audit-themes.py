#!/usr/bin/env python3
"""Audit every theme in src/UI.h for legibility, alpha and completeness.

Static counterpart to the "Theme legibility" group in ChiptuneTests, which
checks the same things at runtime. This one parses the source instead, so it
can tell you two separate things:

  AUTHORED   what the theme's case block actually writes
  SHIPPED    what survives DeriveRemainingThemeColors, which darkens any
             surface a label cannot be read on

A theme whose AUTHORED value is far worse than its SHIPPED value still
works, but it means the palette is leaning on the automatic correction
rather than being designed - worth knowing when tuning by hand.

Usage:  python tools/audit-themes.py
"""
import io
import re
import sys

MIN_TEXT = 4.5
MIN_LARGE = 3.0

# Every surface an ImGuiCol_Text label is actually drawn on top of. Checking
# only WindowBg once hid a theme whose button labels sat at 1.22:1.
TEXT_SURFACES = [
    "WindowBg", "ChildBg", "PopupBg", "FrameBg", "FrameBgHovered", "FrameBgActive",
    "Button", "ButtonHovered", "ButtonActive",
    "Header", "HeaderHovered", "HeaderActive",
    "Tab", "TabHovered", "TabActive", "TitleBgActive", "MenuBarBg",
]

# Mirrors the labelSurfaces list in DeriveRemainingThemeColors.
CORRECTED = {
    "Button", "ButtonHovered", "ButtonActive",
    "Header", "HeaderHovered", "HeaderActive",
    "FrameBg", "FrameBgHovered", "FrameBgActive",
    "Tab", "TabHovered", "TabActive", "TitleBgActive", "MenuBarBg", "PopupBg",
}

PR_FIELDS = {
    "keyWhite", "keyBlack", "gridLine", "gridLineMeasure", "gridLinePattern",
    "noteDefault", "noteSelected", "playhead", "background",
}


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4


def luminance(rgb):
    r, g, b = (srgb_to_linear(x) for x in rgb[:3])
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(fg, bg):
    a, b = luminance(fg), luminance(bg)
    if a < b:
        a, b = b, a
    return (a + 0.05) / (b + 0.05)


def ensure_readable(surface, text, min_ratio):
    """Same search as ensureReadable() in src/UI.h."""
    if contrast(surface, text) >= min_ratio:
        return surface
    darken = luminance(text) > luminance(surface) or luminance(text) > 0.4
    best = surface
    for step in range(1, 25):
        t = step / 24.0
        if darken:
            cand = [c * (1.0 - t) for c in surface[:3]]
        else:
            cand = [c + (1.0 - c) * t for c in surface[:3]]
        best = cand
        if contrast(cand, text) >= min_ratio:
            break
    return best


def parse_cases(path):
    s = io.open(path, encoding="utf-8", errors="replace").read()
    start = s.index("inline void ApplyTheme(Theme theme)")
    body = s[start:s.index("\n}\n", start)]
    return re.findall(r"case Theme::(\w+):(.*?)break;", body, re.S)


def parse_colors(block):
    out = {}
    for m in re.finditer(r"colors\[ImGuiCol_(\w+)\]\s*=\s*ImVec4\(([^)]*)\)", block):
        out[m.group(1)] = [float(v.strip().rstrip("f")) for v in m.group(2).split(",")]
    return out


def main():
    cases = parse_cases("src/UI.h")
    issues = []

    print("%-14s %-12s %-12s  %s" % ("THEME", "AUTHORED", "SHIPPED", "WORST SURFACE"))
    print("-" * 76)

    for name, block in cases:
        colors = parse_colors(block)
        pr = set(re.findall(r"g_PianoRollColors\.(\w+)\s*=", block))

        if len(colors) != 24:
            issues.append("%s: %d colour slots, expected 24" % (name, len(colors)))
        for missing in sorted(PR_FIELDS - pr):
            issues.append("%s: piano-roll field '%s' missing" % (name, missing))

        text = colors["Text"]

        worst_authored, worst_shipped, worst_on = 99.0, 99.0, ""
        for slot in TEXT_SURFACES:
            if slot not in colors:
                continue
            authored = contrast(text, colors[slot])
            shipped = authored
            if slot in CORRECTED:
                shipped = contrast(text, ensure_readable(colors[slot], text, MIN_TEXT))

            worst_authored = min(worst_authored, authored)
            if shipped < worst_shipped:
                worst_shipped, worst_on = shipped, slot

            if shipped < MIN_LARGE:
                issues.append("%s: label on %s ships at %.2f:1 - barely legible"
                              % (name, slot, shipped))
            elif shipped < MIN_TEXT - 0.1:
                issues.append("%s: label on %s ships at %.2f:1 - below 4.5:1"
                              % (name, slot, shipped))

        for slot, v in colors.items():
            if re.match(r"^(Button|Header|FrameBg)", slot):
                alpha = v[3] if len(v) > 3 else 1.0
                if alpha < 0.85:
                    issues.append("%s: %s alpha %.2f below 0.85" % (name, slot, alpha))

        if colors["Header"][:3] == colors["Button"][:3]:
            issues.append("%s: Header and Button are identical" % name)

        flag = "" if worst_shipped >= MIN_TEXT - 0.1 else "  <-- FAIL"
        leaning = "  (relies on auto-correction)" if worst_authored < MIN_LARGE else ""
        print("%-14s %9.2f:1 %9.2f:1  %s%s%s"
              % (name, worst_authored, worst_shipped, worst_on, flag, leaning))

    minimal = dict(cases)["Minimal"]
    reds = [m.group(1) for m in
            re.finditer(r"colors\[ImGuiCol_(\w+)\]\s*=\s*ImVec4\(([^)]*)\)", minimal)
            if (lambda v: v[0] > 0.45 and v[0] > v[1] * 1.6 and v[0] > v[2] * 1.6)(
                [float(x.strip().rstrip("f")) for x in m.group(2).split(",")])]
    if reds:
        issues.append("Minimal uses red in: %s (red is reserved for record)" % reds)

    print("\n%d issue(s)" % len(issues))
    for i in issues:
        print("  -", i)
    return 1 if issues else 0


if __name__ == "__main__":
    sys.exit(main())
