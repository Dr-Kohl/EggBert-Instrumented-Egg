"""Render the static plugged-in HOME 1 texture using EggBert's 5x7 glyphs."""
import ast
import re
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/ssd1306.c').read_text()
table = source.split('static const uint8_t glyphs[][5] = {')[1].split('};')[0]
glyphs = [ast.literal_eval('[' + row + ']') for row in re.findall(r'\{([^{}]*)\}', table)]
image = Image.new('RGB', (64, 128), (4, 8, 11))

def pixel(x, y, on=True):
    color = (255, 205, 65) if x < 16 else (235, 249, 255)
    image.putpixel((x, y), color if on else (4, 8, 11))

def rect(x, y, width, height):
    for px in range(x, min(64, x + width)):
        for py in range(y, min(128, y + height)):
            pixel(px, py)

def text(x, y, value, on=True):
    for c in value:
        if x + 5 > 64:
            break
        if '0' <= c <= '9':
            index = 1 + ord(c) - ord('0')
        elif 'A' <= c <= 'Z':
            index = 11 + ord(c) - ord('A')
        else:
            index = {'-': 37, '.': 38, '+': 39}.get(c, 0)
        for col, bits in enumerate(glyphs[index]):
            for row in range(7):
                if bits & (1 << row):
                    pixel(x + col, y + row, on)
        x += 6

# The plugged-in status rail, exactly as ui_draw_power_indicator renders it.
rect(4, 7, 9, 10)
rect(6, 2, 2, 5)
rect(10, 2, 2, 5)
rect(7, 17, 3, 12)
text(5, 40, 'I')
text(18, 4, 'HOME 1')
for y, label in [(25, 'RECORD'), (38, 'CATCH'), (51, 'PENDULUM'),
                 (64, 'ACCEL (g)'), (77, 'GYRO'), (90, 'LEVEL'), (103, 'FRICTION')]:
    if y == 25:
        rect(17, y - 1, 47, 10)
    text(17, y, label, on=y != 25)
text(18, 116, 'MID GO')
image.save(ROOT / 'docs/models/eggbert-home-screen.png')
