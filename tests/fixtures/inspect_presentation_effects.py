"""Read a PPTX effect inventory without changing or extracting its resources."""

import argparse
from collections import Counter
import json
from pathlib import Path
from zipfile import ZipFile
from lxml import etree


def inspect(path):
    namespaces = {'a': 'http://schemas.openxmlformats.org/drawingml/2006/main',
                  'p': 'http://schemas.openxmlformats.org/presentationml/2006/main'}
    fonts, effects, warps = Counter(), Counter(), Counter()
    slides = []
    parser = etree.XMLParser(resolve_entities=False, no_network=True)
    with ZipFile(path) as package:
        for item in package.infolist():
            if not item.filename.endswith('.xml') or item.file_size > 8 * 1024 * 1024:
                continue
            if not item.filename.startswith(('ppt/slides/', 'ppt/slideMasters/', 'ppt/slideLayouts/')):
                continue
            root = etree.fromstring(package.read(item), parser)
            local = Counter()
            for element in root.iter():
                name = etree.QName(element).localname
                if name in ('latin', 'ea', 'cs') and element.get('typeface'):
                    fonts[element.get('typeface')] += 1
                if name == 'prstTxWarp':
                    warps[element.get('prst')] += 1
                if name in ('outerShdw', 'innerShdw', 'glow', 'reflection', 'softEdge',
                            'gradFill', 'ln', 'scene3d', 'sp3d', 'prstTxWarp'):
                    parent = etree.QName(element.getparent()).localname
                    local[f'{parent}/{name}'] += 1
                    effects[f'{parent}/{name}'] += 1
            if item.filename.startswith('ppt/slides/slide'):
                slides.append({'part': item.filename, 'effects': dict(local),
                               'textObjects': len(root.xpath('//p:sp[p:txBody]', namespaces=namespaces))})
    return {'file': str(path), 'fonts': dict(fonts.most_common()),
            'effects': dict(effects.most_common()), 'warps': dict(warps), 'slides': slides}


if __name__ == '__main__':
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument('pptx', type=Path)
    print(json.dumps(inspect(arguments.parse_args().pptx), ensure_ascii=False, indent=2))
