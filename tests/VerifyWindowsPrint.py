"""Verify the four A4 pages from windowsprint/pdf_pagination (Print to PDF).

Requires pypdf, pypdfium2 and Pillow only on the verification host; the editor
and normal CTests do not depend on them. Optionally compare unchanged image
tiles with a PDF captured before a page-placement fix. Use identical test
steps and font warmup for byte comparisons (glyph baking depends on them).
"""
import argparse
import hashlib
import json
from pathlib import Path

import pypdfium2 as pdfium
from PIL import Image, ImageChops
from pypdf import PdfReader
from pypdf.generic import ContentStream


def multiply(a, b):
    return (a[0]*b[0]+a[2]*b[1], a[1]*b[0]+a[3]*b[1],
            a[0]*b[2]+a[2]*b[3], a[1]*b[2]+a[3]*b[3],
            a[0]*b[4]+a[2]*b[5]+a[4], a[1]*b[4]+a[3]*b[5]+a[5])


def image_bounds(page, reader):
    matrix = (1, 0, 0, 1, 0, 0)
    stack, bounds = [], []
    objects = page['/Resources']['/XObject']
    for values, op in ContentStream(page.get_contents(), reader).operations:
        if op == b'q':
            stack.append(matrix)
        elif op == b'Q':
            matrix = stack.pop()
        elif op == b'cm':
            matrix = multiply(matrix, tuple(map(float, values)))
        elif op == b'Do':
            assert objects[values[0]]['/Subtype'] == '/Image', 'Unexpected printer PDF drawing'
            a, b, c, d, e, f = matrix
            points = [(a*x+c*y+e, b*x+d*y+f) for x, y in ((0, 0), (0, 1), (1, 0), (1, 1))]
            bounds.append([min(p[0] for p in points), min(p[1] for p in points),
                           max(p[0] for p in points), max(p[1] for p in points)])
    assert not stack and bounds
    return bounds


def check(path, output=None, reference=None):
    reader = PdfReader(path)
    document = pdfium.PdfDocument(str(path))
    assert len(reader.pages) == 4, 'Expected exactly four printed pages'
    records = []
    if output:
        output.mkdir(parents=True, exist_ok=True)
        contact = Image.new('RGB', (840, 1188), '#cccccc')
    for i, page in enumerate(reader.pages):
        width, height = float(page.mediabox.width), float(page.mediabox.height)
        assert abs(width-210*72/25.4) < 1 and abs(height-297*72/25.4) < 1, 'Expected portrait A4'
        bounds = image_bounds(page, reader)
        # GDI's 600 DPI coordinate rounding stays within one device pixel.
        margin = 5*72/25.4
        assert abs(min(b[0] for b in bounds)-margin) < 0.15, 'Left margin is not 5 mm'
        assert abs(height-max(b[3] for b in bounds)-margin) < 0.15, 'Top margin is not 5 mm'
        assert max(b[2] for b in bounds) <= width-margin+0.5
        assert min(b[1] for b in bounds) >= margin-0.5
        rendered = document[i].render(scale=1).to_pil().convert('RGB')
        difference = ImageChops.difference(rendered, Image.new('RGB', rendered.size, 'white'))
        ink = difference.convert('L').point(lambda value: 255 if value > 24 else 0).getbbox()
        if ink:
            assert ink[0] >= 13 and ink[1] >= 13, 'Printed content touches the paper edge'
        tiles = [hashlib.sha256(image.image.tobytes()).hexdigest() for image in page.images]
        if reference:
            assert tiles == reference[i], 'Page placement changed the rendered image tiles'
        records.append({'points': [width, height], 'image_bounds': bounds, 'tile_sha256': tiles,
                        'ink_bbox_at_72dpi': ink, 'render_sha256': hashlib.sha256(rendered.tobytes()).hexdigest()})
        if output:
            rendered.save(output/f'page{i+1}.png')
            contact.paste(rendered.resize((420, 594)), ((i % 2)*420, (i // 2)*594))
    assert sum(page['ink_bbox_at_72dpi'] is not None for page in records) >= 2
    assert len({page['render_sha256'] for page in records}) >= 2
    if output:
        contact.save(output/'contact.png')
    return {'file': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'pages': records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pdf', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--reference', type=Path)
    args = parser.parse_args()
    reference = None
    if args.reference:
        reference = [[hashlib.sha256(image.image.tobytes()).hexdigest() for image in page.images]
                     for page in PdfReader(args.reference).pages]
    result = check(args.pdf, args.output, reference)
    if args.output:
        (args.output/'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
