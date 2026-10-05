"""Rasterize the product SVG for the Windows icon and bitmap fallback."""

from pathlib import Path
import re
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets' / 'mirrorfly-mark.svg'
TOKEN = re.compile(r'[MmLlZz]|-?\d+(?:\.\d+)?')
SIZE = 1024


def path_polygons(path):
    tokens = TOKEN.findall(path)
    cursor = 0
    command = ''
    point = (0.0, 0.0)
    origin = point
    polygon = []
    while cursor < len(tokens):
        if tokens[cursor].isalpha():
            command = tokens[cursor]
            cursor += 1
        if command and command in 'Zz':
            if polygon:
                yield polygon
                polygon = []
            point = origin
            command = ''
            continue
        if not command or command not in 'MmLl' or cursor + 1 >= len(tokens):
            raise ValueError(f'Unsupported SVG path near {tokens[cursor:]}')
        x, y = float(tokens[cursor]), float(tokens[cursor + 1])
        cursor += 2
        if command.islower():
            x += point[0]
            y += point[1]
        point = (x, y)
        if command in 'Mm':
            if polygon:
                yield polygon
            polygon = [point]
            origin = point
            command = 'l' if command == 'm' else 'L'
        else:
            polygon.append(point)
    if polygon:
        yield polygon


def main():
    tree = ET.parse(SOURCE)
    image = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
    canvas = ImageDraw.Draw(image)
    scale = SIZE / 256
    for element in tree.iter():
        if element.tag.endswith('path'):
            for polygon in path_polygons(element.attrib['d']):
                canvas.polygon([(round(x * scale), round(y * scale)) for x, y in polygon],
                               fill=element.attrib['fill'])
    png = image.resize((256, 256), Image.Resampling.LANCZOS)
    png.save(ROOT / 'assets' / 'mirrorfly-mark.png')
    image.save(ROOT / 'assets' / 'mirrorfly.ico', format='ICO',
               sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])


if __name__ == '__main__':
    main()
