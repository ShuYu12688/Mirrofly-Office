"""Compare headless slide PNGs against reference PDFs; pixel differences are not a compatibility score."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

import numpy as np
from PIL import Image, ImageDraw, ImageFilter


def compare(actual, reference):
    a = np.asarray(actual.convert('RGB'), dtype=np.int16)
    b = np.asarray(reference.convert('RGB'), dtype=np.int16)
    difference = np.abs(a - b)
    background = np.median(np.stack((b[0, 0], b[0, -1], b[-1, 0], b[-1, -1])), axis=0)
    ink_a = np.max(np.abs(a - background), axis=2) > 40
    ink_b = np.max(np.abs(b - background), axis=2) > 40
    dilated_a = np.asarray(Image.fromarray(ink_a.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(7))) > 0
    dilated_b = np.asarray(Image.fromarray(ink_b.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(7))) > 0
    missing = float(np.logical_and(ink_b, ~dilated_a).sum() / max(1, ink_b.sum()))
    extra = float(np.logical_and(ink_a, ~dilated_b).sum() / max(1, ink_a.sum()))
    return {'meanChannelDifference': float(difference.mean()),
            'differentPixelsOver48': float((difference.max(axis=2) > 48).mean()),
            'referenceInkMissingWithin3px': missing, 'extraInkBeyond3px': extra}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe_directory', type=Path)
    parser.add_argument('reference_pdfs', type=Path)
    parser.add_argument('original_directory', type=Path)
    parser.add_argument('--pdftoppm', default=shutil.which('pdftoppm'))
    args = parser.parse_args()
    report = json.loads((args.probe_directory / 'report.json').read_text(encoding='utf-8'))
    comparison = []
    for entry in report:
        if entry['error']:
            continue
        directory = args.probe_directory / entry['directory']
        originals = list(args.original_directory.glob(entry['directory'] + '.*'))
        if len(originals) != 1:
            raise ValueError(f'Expected one original for {entry["directory"]}')
        with zipfile.ZipFile(originals[0]) as original, zipfile.ZipFile(directory / 'roundtrip.pptx') as saved:
            original_parts = {entry.filename for entry in original.infolist() if not entry.is_dir()}
            saved_parts = {entry.filename for entry in saved.infolist() if not entry.is_dir()}
            preserved = original_parts == saved_parts and all(
                original.read(name) == saved.read(name) for name in original_parts)
        pdf = args.reference_pdfs / (entry['directory'] + '.pdf')
        for index, slide in enumerate(entry['slides'], 1):
            actual_path = directory / slide['image']
            actual = Image.open(actual_path).convert('RGB')
            prefix = directory / f'reference-{index:03}'
            subprocess.run([str(args.pdftoppm), '-f', str(index), '-l', str(index), '-singlefile',
                            '-scale-to-x', str(actual.width), '-scale-to-y', str(actual.height),
                            '-png', str(pdf), str(prefix)], check=True, capture_output=True)
            reference = Image.open(prefix.with_suffix('.png')).convert('RGB')
            if reference.size != actual.size:
                raise ValueError(f'Reference dimensions disagree for {actual_path}')
            comparison.append({'source': entry['source'], 'directory': entry['directory'], 'page': index,
                               'partsByteExact': preserved, **compare(actual, reference)})
    comparison.sort(key=lambda item: item['differentPixelsOver48'], reverse=True)
    (args.probe_directory / 'comparison.json').write_text(
        json.dumps({'note': 'Diagnostic pixel differences, not an Office compatibility percentage.',
                    'pages': comparison}, ensure_ascii=False, indent=2), encoding='utf-8')
    for offset in range(0, len(comparison), 5):
        rows = comparison[offset:offset + 5]
        contact = Image.new('RGB', (1000, len(rows) * 310), '#eeeeee')
        draw = ImageDraw.Draw(contact)
        for row, item in enumerate(rows):
            directory = args.probe_directory / item['directory']
            draw.text((10, row * 310 + 2), item['directory'] + f' page {item["page"]}', fill='black')
            draw.text((10, row * 310 + 20), 'LibreOffice reference', fill='black')
            draw.text((510, row * 310 + 20), 'Mirrorfly', fill='black')
            for column, path in enumerate((directory / f'reference-{item["page"]:03}.png',
                                           directory / f'slide-{item["page"]:03}.png')):
                image = Image.open(path).convert('RGB')
                image.thumbnail((480, 268))
                contact.paste(image, (column * 500 + 10, row * 310 + 38))
        contact.save(args.probe_directory / f'comparison-{offset // 5 + 1:02}.png')
    print(json.dumps({'pages': len(comparison), 'byteExactPackages': all(p['partsByteExact'] for p in comparison),
                      'largestDifferences': comparison[:5]}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
