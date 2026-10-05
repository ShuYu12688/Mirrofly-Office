"""Independently check actual Word tab editing, preserved OOXML and PDF leader marks."""

from pathlib import Path
from zipfile import ZipFile
from xml.etree import ElementTree as ET
import sys

from docx import Document
import pdfplumber
import pypdfium2


directory = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / 'build'
NS = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'}
W = '{' + NS['w'] + '}'


def read(name):
    path = directory / name
    document = Document(path)
    with ZipFile(path) as archive:
        assert archive.testzip() is None
        parts = {name: archive.read(name) for name in archive.namelist()}
    paragraphs = ET.fromstring(parts['word/document.xml']).findall('w:body/w:p', NS)
    return document, paragraphs, parts


def semantic(node):
    return node.tag, sorted(node.attrib.items()), (node.text or '').strip(), [semantic(n) for n in node]


def tabs(properties):
    container = properties.find('w:tabs', NS) if properties is not None else None
    return list(container) if container is not None else []


def resolve(paragraph, parts):
    styles = ET.fromstring(parts['word/tabs/styles.xml'])
    definitions = {node.get(W + 'styleId'): node for node in styles.findall('w:style', NS)}
    properties = [styles.find('w:docDefaults/w:pPrDefault/w:pPr', NS)]
    direct = paragraph.find('w:pPr', NS)
    style = direct.find('w:pStyle', NS)
    chain, seen = [], set()
    identity = style.get(W + 'val') if style is not None else None
    while identity:
        assert identity not in seen
        seen.add(identity)
        definition = definitions[identity]
        chain.append(definition.find('w:pPr', NS))
        parent = definition.find('w:basedOn', NS)
        identity = parent.get(W + 'val') if parent is not None else None
    properties.extend(reversed(chain))
    properties.append(direct)
    stops = {}
    for props in properties:
        for node in tabs(props):
            position = int(node.get(W + 'pos'))
            if node.get(W + 'val') == 'clear':
                stops.pop(position, None)
            else:
                stops[position] = (node.get(W + 'val', 'left'), node.get(W + 'leader', 'none'))
    return [(position / 20, *stops[position]) for position in sorted(stops)]


original_doc, original, original_parts = read('word-tabs-original.docx')
assert [p.text for p in original_doc.paragraphs] == ['Name\t12.34\tend', 'keep\t100']
assert resolve(original[0], original_parts) == [
    (72, 'center', 'hyphen'), (144, 'decimal', 'middleDot'), (180, 'right', 'hyphen')]
assert resolve(original[1], original_parts) == [
    (72, 'center', 'hyphen'), (108, 'left', 'underscore'), (144, 'decimal', 'middleDot')]
settings = ET.fromstring(original_parts['word/tabs/settings.xml'])
assert settings.find('w:defaultTabStop', NS).get(W + 'val') == '360'
for name, expected in [('word-tabs-edited.docx', [(90, 'right', 'dot')]),
                       ('word-tabs-cleared.docx', [])]:
    document, paragraphs, parts = read(name)
    assert [p.text for p in document.paragraphs] == [p.text for p in original_doc.paragraphs]
    assert set(parts) == set(original_parts)
    assert [key for key in parts if parts[key] != original_parts[key]] == ['word/document.xml']
    assert resolve(paragraphs[0], parts) == expected
    assert resolve(paragraphs[1], parts) == resolve(original[1], original_parts)
    assert semantic(paragraphs[1]) == semantic(original[1])
    assert paragraphs[0].get('{urn:keep}keep') == 'paragraph'
    props = paragraphs[0].find('w:pPr', NS)
    assert props.find('w:keepNext', NS) is not None
    assert props.find('w:tabs', NS).get('{urn:keep}keep') == 'tabs'
    retained = next(n for n in tabs(props) if n.get(W + 'pos') == '3600')
    assert retained.get('{urn:keep}keep') == 'stop'

ui, _, _ = read('word-tabs-ui.docx')
assert [p.text for p in ui.paragraphs] == ['first\t12.34', 'keep\tend']
stop = list(ui.paragraphs[0].paragraph_format.tab_stops)
assert len(stop) == 1 and stop[0].position.pt == 108
assert str(stop[0].alignment).startswith('DECIMAL') and str(stop[0].leader).startswith('LINES')
assert len(ui.paragraphs[1].paragraph_format.tab_stops) == 0
pasted, _, _ = read('word-tabs-pasted.docx')
assert [p.text for p in pasted.paragraphs] == ['first\t12.34', 'first\t12.34']
for paragraph in pasted.paragraphs:
    stop = list(paragraph.paragraph_format.tab_stops)
    assert len(stop) == 1 and stop[0].position.pt == 108
    assert str(stop[0].alignment).startswith('DECIMAL') and str(stop[0].leader).startswith('LINES')
indented, _, _ = read('word-tabs-indent.docx')
assert [p.paragraph_format.left_indent.pt for p in indented.paragraphs] == [36, 36, 12]
assert all(list(p.paragraph_format.tab_stops)[0].position.pt == 72 for p in indented.paragraphs)
for first_indent in [12, -18]:
    preserved, _, _ = read(f'word-tabs-first-indent-{first_indent}.docx')
    paragraph = preserved.paragraphs[0]
    assert paragraph.text == 'A\t12.34\nB\t56.78'
    assert paragraph.paragraph_format.left_indent.pt == 18
    assert paragraph.paragraph_format.first_line_indent.pt == first_indent
    stop = list(paragraph.paragraph_format.tab_stops)
    assert len(stop) == 1 and stop[0].position.pt == 72

for alignment in ['center', 'right']:
    preserved, _, _ = read(f'word-tabs-soft-break-{alignment}.docx')
    paragraph = preserved.paragraphs[0]
    assert paragraph.text == 'A\t12.34\nB\t56.78'
    stop = list(paragraph.paragraph_format.tab_stops)
    assert len(stop) == 1 and stop[0].position.pt == 72
    assert str(stop[0].alignment).lower().startswith(alignment)

pdf_path = directory / 'word-tab-leaders.pdf'
with pdfplumber.open(pdf_path) as pdf:
    assert len(pdf.pages) == 1
    page = pdf.pages[0]
    rows = sorted({round(c['top'], 1) for c in page.chars if c['text'] == 'r'})
    assert len(rows) == 5
    for top in rows:
        chars = [c for c in page.chars if abs(c['top'] - top) < 0.2 and c['text'].isalpha()]
        assert ''.join(c['text'] for c in chars) == 'rowend'
        assert abs(next(c['x0'] for c in chars if c['text'] == 'e') - 117.75) < 1
    assert sum(c['text'] == '.' for c in page.chars) >= 20
    assert sum(c['text'] == '-' for c in page.chars) >= 15
    lines = sorted(page.lines, key=lambda line: line['top'])
    assert len(lines) == 2 and lines[1]['linewidth'] > lines[0]['linewidth'] * 1.9
    assert all(28 < line['x0'] < 31 and 116 < line['x1'] < 118 for line in lines)

# An independent PDF engine checks every leader row, including fonts with incomplete text mappings.
pdf = pypdfium2.PdfDocument(pdf_path)
page = pdf[0]
bitmap = page.render(scale=2)
image = bitmap.to_pil().convert('RGB')
for top in rows:
    region = image.crop((64, int(top * 2), 228, int((top + 15) * 2)))
    pixels = region.get_flattened_data() if hasattr(region, 'get_flattened_data') else region.getdata()
    assert sum(max(pixel) < 220 for pixel in pixels) > 35
bitmap.close()
page.close()
pdf.close()
print('Word tabs: inherited replacement/clear, unchanged package parts, parent/AI/paste/indent DOCX, '
      'all five actual PDF leaders independently passed.')
