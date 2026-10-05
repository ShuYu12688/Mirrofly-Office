"""Verify facts and new native drawing styles in the actual model-created four-page deck."""
from pathlib import Path
from xml.etree import ElementTree as ET
import json
import sys
import zipfile

directory = Path(sys.argv[1])
files = list(directory.glob('*.pptx'))
assert len(files) == 1, f'Expected exactly one PPTX, found {len(files)}'
NS = {'a': 'http://schemas.openxmlformats.org/drawingml/2006/main',
      'p': 'http://schemas.openxmlformats.org/presentationml/2006/main'}
required = [
    ['校园微型湿地观察计划', '先观察，再干预'],
    ['记录生境', '记录植物、水位与人类活动', '证据边界', '本演示没有现场测量数据',
     '核对来源', '将观察笔记与可靠资料交叉核对'],
    ['先观察', '先建立连续记录，再决定是否干预', '立即改造', '缺少证据时可能打扰既有生境'],
    ['建立记录', '使用同一观察路线和记录表', '回访复盘', '比较不同天气下的现场记录',
     '尊重边界', '不采集生物，不改变水流']]
new_dashes = {'lgDash', 'lgDashDot', 'lgDashDotDot', 'sysDot', 'sysDash', 'sysDashDot', 'sysDashDotDot'}
patterns = {'pct5', 'pct10', 'pct25', 'pct50', 'pct75', 'pct90', 'horz', 'vert', 'dnDiag',
            'upDiag', 'cross', 'diagCross', 'smGrid', 'lgGrid', 'smCheck', 'lgCheck'}
native_style = []
geometry = []
with zipfile.ZipFile(files[0]) as package:
    presentation = ET.fromstring(package.read('ppt/presentation.xml'))
    assert len(presentation.findall('p:sldIdLst/p:sldId', NS)) == 4
    for index, phrases in enumerate(required, 1):
        root = ET.fromstring(package.read(f'ppt/slides/slide{index}.xml'))
        text = ''.join(node.text or '' for node in root.findall('.//a:t', NS))
        assert all(phrase in text for phrase in phrases), f'Page {index} omitted a supplied fact: {text}'
        boxes = []
        for shape in root.findall('.//p:sp', NS):
            fill = shape.find('p:spPr/a:pattFill', NS)
            dash = shape.find('p:spPr/a:ln/a:prstDash', NS)
            if fill is not None and fill.get('prst') in patterns and dash is not None:
                if dash.get('val') in new_dashes:
                    assert fill.find('a:fgClr', NS) is not None and fill.find('a:bgClr', NS) is not None
                    native_style.append((index, fill.get('prst'), dash.get('val')))
            if shape.find('p:txBody', NS) is not None:
                transform = shape.find('p:spPr/a:xfrm', NS)
                if transform is not None:
                    boxes.append(ET.tostring(transform).decode())
        geometry.append(boxes)
assert native_style, 'No actual shape has a supported pattern and newly editable preset dash'
assert len({tuple(boxes) for boxes in geometry}) >= 3, 'The deck repeats one geometry for all content'
print(json.dumps({'file': str(files[0]), 'pages': 4, 'allSuppliedFactsPresent': True,
                  'nativePatternAndNewDash': native_style, 'distinctPageGeometry': len({tuple(g) for g in geometry})},
                 ensure_ascii=False))
