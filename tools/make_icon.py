"""Generates resources/icon.ico: a pixel-art alchemist's flask with glowing liquid.

Run from the repository root:  python3 tools/make_icon.py
"""
from PIL import Image

# 32x32 pixel art. Legend: . transparent, o outline, g glass, h highlight,
# l liquid, L bright liquid, c cork, s spark
ART = [
    "................................",
    "..............cccc..............",
    ".............occcco.............",
    ".............occcco.............",
    "..............oggo..............",
    "..............oggo..............",
    "..............oghn..............",
    "..............oggo..............",
    "..............oggo..............",
    ".............oggggo.............",
    "...........oogggggoo............",
    ".........ooggggggggghoo.........",
    "........oggggggggggghhgo........",
    ".......ogggggggggggggghgo.......",
    "......oggggggggggggggggggo......",
    "......oLLLLLLLLLLLLLLLLLLLo.....",
    ".....oLLLLLLLLsLLLLLLLLLLLo.....",
    ".....olllLLLLLLLLLLLLsLLLlo.....",
    ".....ollllllLLLLLLLLLLllllo.....",
    ".....olllsllllllLLLlllllllo.....",
    ".....ollllllllllllllllllslo.....",
    ".....olllllllllllllllllllllo....",
    "......olllllllsllllllllllo......",
    "......ollllllllllllllllllo......",
    ".......olllllllllllllllo........",
    "........olllllllllllllo.........",
    ".........oollllllllloo..........",
    "...........oooooooooo...........",
    "................................",
    "................................",
    "................................",
    "................................",
]

COLORS = {
    "o": (28, 20, 40, 255),
    "g": (150, 200, 230, 90),
    "h": (235, 250, 255, 220),
    "n": (235, 250, 255, 220),
    "l": (226, 120, 30, 255),
    "L": (255, 196, 70, 255),
    "c": (120, 76, 40, 255),
    "s": (255, 250, 210, 255),
}


def main() -> None:
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    for y, row in enumerate(ART):
        for x, ch in enumerate(row[:32]):
            if ch in COLORS:
                img.putpixel((x, y), COLORS[ch])
    big = img.resize((256, 256), Image.NEAREST)
    big.save("resources/icon.ico", sizes=[(256, 256), (64, 64), (48, 48), (32, 32), (16, 16)])
    big.save("resources/icon.png")


if __name__ == "__main__":
    main()
