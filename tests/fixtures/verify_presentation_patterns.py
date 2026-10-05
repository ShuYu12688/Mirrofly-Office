"""Independent DrawingML and raster checks for the common pattern/dash authoring path."""
from pathlib import Path
from xml.etree import ElementTree as ET
import sys
import zipfile

from PIL import Image, ImageDraw

directory = Path(sys.argv[1])
NS = {'a': 'http://schemas.openxmlformats.org/drawingml/2006/main',
      'p': 'http://schemas.openxmlformats.org/presentationml/2006/main'}
patterns = ['pct5', 'pct10', 'pct25', 'pct50', 'pct75', 'pct90', 'horz', 'vert',
            'dnDiag', 'upDiag', 'cross', 'diagCross', 'smGrid', 'lgGrid', 'smCheck', 'lgCheck']
dashes = ['solid', 'dot', 'dash', 'lgDash', 'dashDot', 'lgDashDot', 'lgDashDotDot',
          'sysDot', 'sysDash', 'sysDashDot', 'sysDashDotDot']


def slide(name, number=1):
    with zipfile.ZipFile(directory / name) as package:
        return ET.fromstring(package.read(f'ppt/slides/slide{number}.xml'))


def shape_fill(shape):
    return shape.find('p:spPr/a:pattFill', NS)


shapes = slide('presentation-patterns.pptx').findall('.//p:sp', NS)
assert len(shapes) == 16
image = Image.open(directory / 'presentation-patterns.png').convert('RGB')
for index, shape in enumerate(shapes):
    fill = shape_fill(shape)
    assert fill.get('prst') == patterns[index]
    assert fill.find('a:fgClr/a:srgbClr', NS).get('val') == 'FF0000'
    assert fill.find('a:bgClr/a:srgbClr', NS).get('val') == '00FF00'
    x, y = 20 + index % 4 * 235, 20 + index // 4 * 125
    colors = image.crop((x + 5, y + 5, x + 200, y + 85)).getcolors(20000)
    assert colors and sum(n for n, c in colors if c[0] > 240 and c[1] < 15) > 20
    assert sum(n for n, c in colors if c[1] > 240 and c[0] < 15) > 20

fill = shape_fill(slide('presentation-pattern-alpha.pptx').find('.//p:sp', NS))
assert fill.find('a:fgClr/a:srgbClr/a:alpha', NS).get('val') == '50000'
assert fill.find('a:bgClr/a:srgbClr/a:alpha', NS).get('val') == '25000'
assert fill.find('a:fgClr/a:srgbClr', NS).get('val') == 'FF0000'
assert fill.find('a:bgClr/a:srgbClr', NS).get('val') == '008000'
alpha = Image.open(directory / 'presentation-pattern-alpha.png').convert('RGB')
assert alpha.getpixel((1, 1)) == (0, 128, 0)
red, green, blue = alpha.getpixel((0, 0))
assert abs(red - 128) <= 1 and green == 0 and abs(blue - 127) <= 1

shapes = slide('presentation-dashes.pptx').findall('.//p:sp', NS)
assert len(shapes) == 11
dash_image = Image.open(directory / 'presentation-dashes.png').convert('RGB')
for index, shape in enumerate(shapes):
    line = shape.find('p:spPr/a:ln', NS)
    assert line.get('w') == '25400'  # 2pt in EMU.
    preset = line.find('a:prstDash', NS)
    # An omitted dash element is the OOXML solid-line default.
    assert (preset.get('val') if preset is not None else 'solid') == dashes[index]
    assert line.find('a:custDash', NS) is None
    pixels = [dash_image.getpixel((x, 25 + index * 45)) for x in range(45, 910)]
    dark = sum(max(c) < 120 for c in pixels)
    assert dark > 25
    if index:
        assert dark < len(pixels) - 25

shape = slide('presentation-pattern-ui.pptx').find('.//p:sp', NS)
fill = shape_fill(shape)
assert fill.get('prst') == 'pct25'
assert fill.find('a:fgClr/a:srgbClr', NS).get('val') == '123456'
assert shape.find('p:spPr/a:ln/a:prstDash', NS).get('val') == 'sysDot'

directions = ['editorial', 'research', 'modern', 'natural']
facts = ['Read the landscape before acting.', 'Link local knowledge with field observations.',
         'Protect habitats through steady daily choices.']
fingerprints = []
for style_index, style in enumerate(directions):
    previous = None
    for page_index, number in enumerate([3, 4]):
        root = slide('presentation-art-directions.pptx', style_index * 2 + page_index + 1)
        texts = [''.join(node.itertext()) for node in root.findall('.//a:t', NS)]
        assert all(fact in texts for fact in facts)
        assert all(heading in texts for heading in ['Observe', 'Connect', 'Care'])
        positions = []
        for shape in root.findall('.//p:sp', NS):
            if shape.find('p:txBody', NS) is not None:
                transform = shape.find('p:spPr/a:xfrm', NS)
                offset = transform.find('a:off', NS)
                size = transform.find('a:ext', NS)
                positions.append((offset.get('x'), offset.get('y'), size.get('cx'), size.get('cy')))
        fingerprints.append(positions)
        if page_index:
            assert positions != previous
        previous = positions

assert all(fingerprints[left] != fingerprints[right]
           for left in range(8) for right in range(left + 1, 8) if left % 2 == right % 2)
contact = Image.new('RGB', (960, 1160), '#E7E8EA')
draw = ImageDraw.Draw(contact)
for style_index, style in enumerate(directions):
    draw.text((12, style_index * 290 + 4), style, fill='#193049')
    for page_index, number in enumerate([3, 4]):
        image = Image.open(directory / f'presentation-art-{style}-{number}.png').convert('RGB')
        image.thumbnail((480, 270))
        contact.paste(image, (page_index * 480, style_index * 290 + 20))
contact.save(directory / 'presentation-art-contact.png')
print('Common patterns: actual two-color pixels, independent alpha XML, all eleven native dash presets, '
      'UI/AI output and eight geometrically distinct art pages passed. Texture preview remains approximate.')
