#!/usr/bin/env python3
"""Compose the README showcase image in the clean light layout.

The first cell is the title block; every other cell stacks a top-screen
shot over its centered bottom-screen companion, rounded corners, caption
underneath. --pairs picks the scenes and their order, --rows wraps them
into a grid instead of one horizontal row. Run from the repo root:

    python3 tools/dualscreen/compose_showcase.py [--pairs map,battle,...]
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFont

SRC = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "screenshots")

BG = (255, 255, 255)
INK = (17, 17, 17)
MUTED = (110, 110, 110)
CAPTION = (128, 128, 128)
FAINT = (170, 170, 170)

COL_W = 640
PAD = 48
GAP = 14
TOP_H = COL_W * 1080 // 1920
BOT_W = int(COL_W * 0.74)
BOT_H = BOT_W * 1080 // 1240
CAP_H = 52
CELL_H = TOP_H + GAP + BOT_H + CAP_H

SCENES = {
    "map": ("shot_map_top.png", "shot_map_bottom.png", "Widescreen + live Hoenn map"),
    "battle": ("shot_battle_top.png", "shot_battle_bottom.png", "Dual-screen battles"),
    "moves": ("shot_moves_top.png", "shot_moves_bottom.png", "Move select"),
    "battlebag": ("shot_battlebag_top.png", "shot_battlebag_bottom.png", "Battle bag"),
    "battleparty": ("shot_battleparty_top.png", "shot_battleparty_bottom.png", "Party switch"),
    "party": ("shot_party_top.png", "shot_party_bottom.png", "Party"),
    "card": ("shot_card_top.png", "shot_card_bottom.png", "Trainer card"),
}

TITLE_LINES = ["Pokémon Emerald", "Dual Screen"]
BULLETS = [
    "Gen 4-style battles, touch or buttons",
    "True widescreen",
    "Live party, map, bag, card",
    "Runs the decomp natively",
]
FOOTER = "github.com/Goldoire/pokeemerald-dualscreen"


def load_font(bold, size):
    names = (["arialbd.ttf", "DejaVuSans-Bold.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"] if bold else
             ["arial.ttf", "DejaVuSans.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"])
    for name in names:
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def rounded(im, radius):
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, im.size[0] - 1, im.size[1] - 1],
                                           radius=radius, fill=255)
    out = Image.new("RGB", im.size, BG)
    out.paste(im.convert("RGB"), (0, 0), mask)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pairs", default="map,battle,moves,party,card")
    ap.add_argument("--rows", type=int, default=1)
    ap.add_argument("--out", default=os.path.join(SRC, "showcase.png"))
    args = ap.parse_args()
    keys = [k.strip() for k in args.pairs.split(",") if k.strip()]
    cells = [None] + [SCENES[k] for k in keys]

    f_title = load_font(True, 58)
    f_bullet = load_font(False, 28)
    f_cap = load_font(True, 26)
    f_foot = load_font(False, 22)

    cols = (len(cells) + args.rows - 1) // args.rows
    rows = (len(cells) + cols - 1) // cols
    w = cols * COL_W + (cols + 1) * PAD
    h = rows * CELL_H + (rows + 1) * PAD
    img = Image.new("RGB", (w, h), BG)
    draw = ImageDraw.Draw(img)

    def place(path, x, y, pw, ph, radius):
        im = Image.open(os.path.join(SRC, path)).convert("RGB").resize((pw, ph), Image.LANCZOS)
        img.paste(rounded(im, radius), (x, y))

    for index, cell in enumerate(cells):
        col, row = index % cols, index // cols
        x = PAD + col * (COL_W + PAD)
        y = PAD + row * (CELL_H + PAD)

        if cell is None:
            ty = y + 34
            for line in TITLE_LINES:
                draw.text((x, ty), line, font=f_title, fill=INK)
                ty += 72
            ty += 36
            for bullet in BULLETS:
                draw.text((x + 4, ty), "·", font=f_bullet, fill=FAINT)
                draw.text((x + 30, ty), bullet, font=f_bullet, fill=MUTED)
                ty += 48
            draw.text((x + 4, ty + 24), FOOTER, font=f_foot, fill=FAINT)
            continue

        top, bottom, caption = cell
        place(top, x, y, COL_W, TOP_H, 16)
        place(bottom, x + (COL_W - BOT_W) // 2, y + TOP_H + GAP, BOT_W, BOT_H, 14)
        cy = y + TOP_H + GAP + BOT_H + 16
        tw = draw.textlength(caption, font=f_cap)
        draw.text((x + (COL_W - tw) / 2, cy), caption, font=f_cap, fill=CAPTION)

    img.save(args.out)
    print("wrote", args.out, img.size)


if __name__ == "__main__":
    main()
