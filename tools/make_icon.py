"""Generate the Scribble application icon.

Three bars in three colours: a transcript where the speakers are told apart,
which is the whole point of the program. Kept to solid shapes with wide spacing
because the icon is read at 16px in the taskbar far more often than at 256.

Run from the repository root:
    python tools/make_icon.py
"""

from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"

BACKGROUND = (27, 29, 33, 255)
SPEAKERS = [
    (214, 38, 42, 255),
    (232, 163, 61, 255),
    (61, 165, 196, 255),
]

# Rendered large and downsampled: drawing directly at 16px produces ragged
# edges on the rounded corners.
SUPERSAMPLE = 8
SIZES = [16, 24, 32, 48, 64, 128, 256]


def render(size: int) -> Image.Image:
    s = size * SUPERSAMPLE
    image = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    margin = s * 0.06
    draw.rounded_rectangle(
        [margin, margin, s - margin, s - margin],
        radius=s * 0.22,
        fill=BACKGROUND,
    )

    bar_height = s * 0.115
    gap = s * 0.085
    total = len(SPEAKERS) * bar_height + (len(SPEAKERS) - 1) * gap
    top = (s - total) / 2

    left = s * 0.22
    widths = [0.58, 0.44, 0.5]

    for index, colour in enumerate(SPEAKERS):
        y = top + index * (bar_height + gap)
        draw.rounded_rectangle(
            [left, y, left + s * widths[index], y + bar_height],
            radius=bar_height / 2,
            fill=colour,
        )
        # Speaker marker, the visual claim that each line has a known owner.
        dot = bar_height * 0.42
        cy = y + bar_height / 2
        draw.ellipse(
            [s * 0.13 - dot, cy - dot, s * 0.13 + dot, cy + dot],
            fill=colour,
        )

    return image.resize((size, size), Image.LANCZOS)


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)

    frames = [render(size) for size in SIZES]

    ico = ASSETS / "scribble.ico"
    # Every size is embedded rather than left for the shell to downscale from
    # 256, which turns to mush in the notification area.
    frames[-1].save(ico, format="ICO", sizes=[(s, s) for s in SIZES])

    png = ASSETS / "scribble.png"
    render(512).save(png, format="PNG")

    print(f"wrote {ico} ({ico.stat().st_size} bytes)")
    print(f"wrote {png} ({png.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
