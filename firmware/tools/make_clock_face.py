# Generates firmware/keychain/ClockFace.h: the big clock digits, the small caps font and the
# narrow "Listening" label, all drawn here from scratch. Requires Pillow.
# Usage: python make_clock_face.py <output.h> [preview.png]
import math
import sys
from PIL import Image, ImageDraw

DIGIT_W, DIGIT_H, STROKE = 21, 29, 5.2
SS = 8  # supersampling, so diagonals land on clean pixels
X0, X1, Y0, Y1, YM = 2.5, 18.5, 2.5, 26.5, 14.5


def octagon(top, bottom, ch):
    return [(X0 + ch, top), (X1 - ch, top), (X1, top + ch), (X1, bottom - ch),
            (X1 - ch, bottom), (X0 + ch, bottom), (X0, bottom - ch), (X0, top + ch), (X0 + ch, top)]


SIX = [[(18.5, -2), (X0, 14)], octagon(10.5, Y1, 4)]
DIGITS = {
    0: [octagon(Y0, Y1, 5)],
    1: [[(0.5, Y0), (11, Y0), (11, Y1)], [(0, Y1), (21, Y1)]],
    2: [[(X0, Y0 + 4), (X0 + 4, Y0), (X1 - 4, Y0), (X1, Y0 + 4), (X1, 10.5), (X0, Y1)], [(X0 - 2, Y1), (21, Y1)]],
    3: [[(X0, Y0 + 4), (X0 + 4, Y0), (X1 - 4, Y0), (X1, Y0 + 4), (X1, YM - 4), (X1 - 4, YM),
         (X1, YM + 4), (X1, Y1 - 4), (X1 - 4, Y1), (X0 + 4, Y1), (X0, Y1 - 4)], [(7, YM), (X1 - 4, YM)]],
    4: [[(14.5, 31), (14.5, Y0), (X0, 14.5), (X0, 20.5)], [(X0 - 2, 20.5), (21, 20.5)]],
    5: [[(21, Y0), (X0, Y0), (X0, 13), (X1 - 4, 13), (X1, 17), (X1, Y1 - 4), (X1 - 4, Y1), (X0 + 4, Y1), (X0, Y1 - 4)]],
    6: SIX,
    7: [[(0, Y0), (X1, Y0), (X1, Y0 + 3), (7, 31)]],
    8: [octagon(Y0, YM, 3.5), octagon(YM, Y1, 3.5)],
    9: [[(DIGIT_W - x, DIGIT_H - y) for x, y in path] for path in SIX],
}


def render_digit(paths):
    img = Image.new("L", (DIGIT_W * SS, DIGIT_H * SS), 0)
    d = ImageDraw.Draw(img)
    r = STROKE / 2 * SS
    for path in paths:
        pts = [(x * SS, y * SS) for x, y in path]
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            n = math.hypot(bx - ax, by - ay) or 1
            nx, ny = -(by - ay) / n * r, (bx - ax) / n * r
            d.polygon([(ax + nx, ay + ny), (bx + nx, by + ny), (bx - nx, by - ny), (ax - nx, ay - ny)], fill=255)
        closed = pts[0] == pts[-1]
        for x, y in pts[(0 if closed else 1):(None if closed else -1)]:
            d.ellipse([x - r, y - r, x + r, y + r], fill=255)
    small = img.resize((DIGIT_W, DIGIT_H), Image.BOX)
    return [[1 if small.getpixel((x, y)) >= 128 else 0 for x in range(DIGIT_W)] for y in range(DIGIT_H)]


# Small capitals, 6 pixels tall. Lowercase letters use the same shapes.
SMALL = {
    "A": ".###. #...# #...# ##### #...# #...#", "B": "####. #...# ####. #...# #...# ####.",
    "C": ".#### #.... #.... #.... #.... .####", "D": "####. #...# #...# #...# #...# ####.",
    "E": "##### #.... ####. #.... #.... #####", "F": "##### #.... ####. #.... #.... #....",
    "G": ".#### #.... #..## #...# #...# .####", "H": "#...# #...# ##### #...# #...# #...#",
    "I": "### .#. .#. .#. .#. ###", "J": "..### ...#. ...#. ...#. #..#. .##..",
    "K": "#...# #..#. ###.. #..#. #...# #...#", "L": "#.... #.... #.... #.... #.... #####",
    "M": "#...# ##.## #.#.# #...# #...# #...#", "N": "#...# ##..# #.#.# #..## #...# #...#",
    "O": ".###. #...# #...# #...# #...# .###.", "P": "####. #...# #...# ####. #.... #....",
    "Q": ".###. #...# #...# #.#.# #..#. .##.#", "R": "####. #...# #...# ####. #..#. #...#",
    "S": ".#### #.... .###. ....# ....# ####.", "T": "##### ..#.. ..#.. ..#.. ..#.. ..#..",
    "U": "#...# #...# #...# #...# #...# .###.", "V": "#...# #...# #...# #...# .#.#. ..#..",
    "W": "#...# #...# #...# #.#.# ##.## #...#", "X": "#...# .#.#. ..#.. ..#.. .#.#. #...#",
    "Y": "#...# .#.#. ..#.. ..#.. ..#.. ..#..", "Z": "##### ...#. ..#.. .#... #.... #####",
    "0": ".###. #..## #.#.# ##..# #...# .###.", "1": "##. .#. .#. .#. .#. ###",
    "2": ".###. #...# ...#. ..#.. .#... #####", "3": "####. ....# ..##. ....# ....# ####.",
    "4": "#..#. #..#. #..#. ##### ...#. ...#.", "5": "##### #.... ####. ....# ....# ####.",
    "6": ".###. #.... ####. #...# #...# .###.", "7": "##### ....# ...#. ..#.. ..#.. ..#..",
    "8": ".###. #...# .###. #...# #...# .###.", "9": ".###. #...# #...# .#### ....# .###.",
    "/": "....# ...#. ..#.. ..#.. .#... #....", ":": ". # . . # .", ".": ". . . . . #",
    ",": ".. .. .. .. .# #.", "-": "... ... ### ... ... ...", "'": "# # . . . .", "!": "# # # # . #",
    "?": ".###. #...# ...#. ..#.. ..... ..#..", "&": ".##.. #..#. .##.. #.#.# #..#. .##.#",
    "+": "..... ..#.. .###. ..#.. ..... .....",
}

# The word "Listening" with 3-pixel-wide letters. L and the dotted i are tall, g hangs below.
LETTERS = {
    "L": ["#..", "#..", "#..", "#..", "#..", "#..", "###", "...", "..."],
    "i": ["#", ".", "#", "#", "#", "#", "#", ".", "."],
    "s": ["...", "...", ".##", "#..", ".#.", "..#", "##.", "...", "..."],
    "t": ["...", ".#.", "###", ".#.", ".#.", ".#.", "..#", "...", "..."],
    "e": ["...", "...", ".#.", "#.#", "###", "#..", ".##", "...", "..."],
    "n": ["...", "...", "##.", "#.#", "#.#", "#.#", "#.#", "...", "..."],
    "g": ["...", "...", ".##", "#.#", "#.#", "#.#", ".##", "..#", "##."],
}


def listening_rows():
    return [".".join(LETTERS[c][r] for c in "Listening") for r in range(9)]


def pack(rows):
    out = []
    for row in rows:
        for i in range(0, len(row), 8):
            byte = 0
            for j in range(8):
                byte <<= 1
                if i + j < len(row) and row[i + j] in (1, "#"):
                    byte |= 1
            out.append(byte)
    return out


def hex_list(values):
    return ", ".join(f"0x{v:02X}" for v in values)


def write_header(path, digits):
    rows = listening_rows()
    bitmaps, glyphs = [], []
    for code in range(0x20, 0x7F):
        shape = SMALL.get(chr(code).upper())
        if shape is None:
            glyphs.append((len(bitmaps), 0, 0, 4, 0, 0))
            continue
        lines = shape.split()
        width = len(lines[0])
        bits = "".join(lines)
        offset = len(bitmaps)
        bitmaps += pack([bits])
        glyphs.append((offset, width, len(lines), width + 1, 0, -len(lines)))

    with open(path, "w") as f:
        f.write("// Generated by firmware/tools/make_clock_face.py. Edit that script, not this file.\n")
        f.write("#pragma once\n#include <Adafruit_GFX.h>\n\n")
        f.write(f"constexpr int CLOCK_DIGIT_W = {DIGIT_W}, CLOCK_DIGIT_H = {DIGIT_H};\n")
        f.write(f"const uint8_t CLOCK_DIGITS[10][{(DIGIT_W + 7) // 8 * DIGIT_H}] PROGMEM = {{\n")
        for n in range(10):
            f.write(f"  {{{hex_list(pack(digits[n]))}}},\n")
        f.write("};\n\n")
        f.write(f"constexpr int LISTENING_W = {len(rows[0])}, LISTENING_H = {len(rows)};\n")
        f.write(f"const uint8_t LISTENING_LABEL[] PROGMEM = {{{hex_list(pack(rows))}}};\n\n")
        f.write(f"const uint8_t ClockSmallBitmaps[] PROGMEM = {{{hex_list(bitmaps)}}};\n\n")
        f.write("const GFXglyph ClockSmallGlyphs[] PROGMEM = {\n")
        for g in glyphs:
            f.write("  {%d, %d, %d, %d, %d, %d},\n" % g)
        f.write("};\n\n")
        f.write("const GFXfont ClockSmall PROGMEM = {(uint8_t*)ClockSmallBitmaps, (GFXglyph*)ClockSmallGlyphs, 0x20, 0x7E, 8};\n")


def text_width(s):
    total = 0
    for ch in s:
        shape = SMALL.get(ch.upper())
        total += len(shape.split()[0]) + 1 if shape else 4
    return total - 1


def draw_text(px, s, baseline):
    x = (128 - text_width(s)) // 2
    for ch in s:
        shape = SMALL.get(ch.upper())
        if not shape:
            x += 4
            continue
        lines = shape.split()
        for r, line in enumerate(lines):
            for c, v in enumerate(line):
                if v == "#":
                    px[x + c, baseline - len(lines) + r] = 255
        x += len(lines[0]) + 1


def preview(path, digits):
    screens = [("16:38", "TUESDAY  3/11/26", "TEXT"), ("09:27", "SATURDAY  14/2/26", "TACO TUESDAY"), ("20:45", "MONDAY  1/6/26", "")]
    sheet = Image.new("L", (128 * 2 + 6, 64 * 2 + 6), 80)
    for i, (t, date, cap) in enumerate(screens + [("", "", "")]):
        img = Image.new("L", (128, 64), 0)
        px = img.load()
        if t:
            for slot, x in zip([0, 1, 3, 4], [5, 29, 77, 101]):
                for r, row in enumerate(digits[int(t[slot])]):
                    for c, v in enumerate(row):
                        if v:
                            px[x + c, 8 + r] = 255
            for top in (18, 31):
                for y in range(top, top + 6):
                    for x in range(61, 67):
                        px[x, y] = 255
            draw_text(px, date, 49)
            if cap:
                draw_text(px, cap, 59)
        else:
            for r, row in enumerate(listening_rows()):
                for c, v in enumerate(row):
                    if v == "#":
                        px[8 + c, 8 + r] = 255
            for n in range(5):
                for r, row in enumerate(digits[[0, 2, 4, 7, 9][n]]):
                    for c, v in enumerate(row):
                        if v:
                            px[2 + n * 25 + c, 25 + r] = 255
        sheet.paste(img, ((i % 2) * 134, (i // 2) * 70))
    sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(path)


if __name__ == "__main__":
    rendered = {n: render_digit(p) for n, p in DIGITS.items()}
    write_header(sys.argv[1], rendered)
    if len(sys.argv) > 2:
        preview(sys.argv[2], rendered)
