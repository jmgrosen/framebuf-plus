#!python3
import freetype
import zlib
import sys
import math
import argparse
from collections import namedtuple

parser = argparse.ArgumentParser(description="Generate a header file from a font to be used with epdiy.")
parser.add_argument("name", action="store", help="name of the font.")
parser.add_argument("size", type=int, help="font size to use.")
parser.add_argument("fontstack", action="store", nargs='+', help="list of font files, ordered by descending priority.")
parser.add_argument("--compress", dest="compress", action="store_true", help="compress glyph bitmaps.")
args = parser.parse_args()

GlyphProps = namedtuple("GlyphProps", ["width", "height", "advance_x", "left", "top", "compressed_size", "data_offset", "code_point"])

font_stack = [freetype.Face(f) for f in args.fontstack]
compress = args.compress
size = args.size
font_name = args.name

# inclusive unicode code point intervals
# must not overlap and be in ascending order
intervals = [
    (32, 126),
    (160, 255),
    # (0x2500, 0x259F),
    # (0x2700, 0x27BF),
    # # powerline symbols
    # (0xE0A0, 0xE0A2),
    # (0xE0B0, 0xE0B3),
    # (0x1F600, 0x1F680),
]


def norm_floor(val):
    return int(math.floor(val / (1 << 6)))

def norm_ceil(val):
    return int(math.ceil(val / (1 << 6)))

for face in font_stack:
    # shift by 6 bytes, because sizes are given as 6-bit fractions
    # the display has about 150 dpi.
    face.set_char_size(size << 6, size << 6, 150, 150)

def chunks(l, n):
    for i in range(0, len(l), n):
        yield l[i:i + n]

total_size = 0
total_packed = 0
all_glyphs = []

def load_glyph(code_point):
    face_index = 0
    while face_index < len(font_stack):
        face = font_stack[face_index]
        glyph_index = face.get_char_index(code_point)
        if glyph_index > 0:
            face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER)
            return face
            break
        face_index += 1
        print (f"falling back to font {face_index} for {chr(code_point)}.", file=sys.stderr)
    raise ValueError(f"code point {code_point} not found in font stack!")

for i_start, i_end in intervals:
    for code_point in range(i_start, i_end + 1):
        face = load_glyph(code_point)
        bitmap = face.glyph.bitmap
        pixels = []
        px = 0
        for i, v in enumerate(bitmap.buffer):
            y = i / bitmap.width
            x = i % bitmap.width
            if x % 2 == 0:
                px = (v >> 4)
            else:
                px = px | (v & 0xF0)
                pixels.append(px)
                px = 0
            # eol
            if x == bitmap.width - 1 and bitmap.width % 2 > 0:
                pixels.append(px)
                px = 0

        packed = bytes(pixels)
        total_packed += len(packed)
        compressed = packed
        if compress:
            compressed = zlib.compress(packed)

        glyph = GlyphProps(
            width = bitmap.width,
            height = bitmap.rows,
            advance_x = norm_floor(face.glyph.advance.x),
            left = face.glyph.bitmap_left,
            top = face.glyph.bitmap_top,
            compressed_size = len(compressed),
            data_offset = total_size,
            code_point = code_point,
        )
        total_size += len(compressed)
        all_glyphs.append((glyph, compressed))

# pipe seems to be a good heuristic for the "real" descender
face = load_glyph(ord('|'))

glyph_data = []
glyph_props = []
for index, glyph in enumerate(all_glyphs):
    props, compressed = glyph
    glyph_data.extend([b for b in compressed])
    glyph_props.append(props)

print("total", total_packed, file=sys.stderr)
print("compressed", total_size, file=sys.stderr)

f = open("{}{}pt.py".format(font_name, size), 'w')

## out
f.write("{}Bitmaps =".format(font_name))
for c in chunks(glyph_data, 16):
    f.write(" \\\n")
    f.write("    " + "b\'" + "".join(f"\\x{b:02X}" for b in c) + "'")
f.write('\n')
f.write('\n')

f.write("{}Glyphs = (\n".format(font_name))
f.write("    # width height xAdvance left top compressedSize dataOffset\n")
for i, g in enumerate(glyph_props):
    f.write("    ({:>2d}, {:>2d}, {:>2d}, {:>2d}, {:>2d}, {:>3d}, {:>4d}), # {}\n".format(
        g.width,
        g.height,
        g.advance_x,
        g.left,
        g.top,
        g.compressed_size,
        g.data_offset,
        chr(g.code_point) if g.code_point != 92 else '<backslash>'
    ))
f.write(")\n")
f.write("\n")

f.write("{}Intervals = (\n".format(font_name))
f.write("    # first last offset\n")
offset = 0
for i_start, i_end in intervals:
    f.write("    ({:>2d}, {:>2d}, {:>2d}),\n".format(i_start, i_end, offset))
    offset += i_end - i_start + 1
f.write(")\n")
f.write("\n")
f.write("{}{}pt = (\n".format(font_name, size))
f.write("    {}Bitmaps,\n".format(font_name))
f.write("    {}Glyphs,\n".format(font_name))
f.write("    {}Intervals,\n".format(font_name))
f.write("    {:d},\n".format(len(intervals)))
f.write("    {},\n".format("True" if compress else "False"))
f.write("    {},\n".format(norm_ceil(face.size.height)))
f.write("    {},\n".format(norm_ceil(face.size.ascender)))
f.write("    {},\n".format(norm_floor(face.size.descender)))
f.write("    {},\n".format(7))
f.write(") # bitmap glyph intervals intervalCount compressed yAdvance ascender descender\n")