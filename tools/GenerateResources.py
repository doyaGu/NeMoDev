"""Compile the existing resource manifest into portable RGBA image data."""
import argparse
from pathlib import Path
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument("--root", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
images = []
for line in (args.root / "images.txt").read_text(encoding="utf-8").splitlines():
    fields = line.split("#", 1)[0].split()
    if not fields:
        continue
    group, kind, identifier = fields[:3]
    size = int(fields[3]) if len(fields) > 3 else 0
    filename = args.root / "images" / group / f"{kind}-{identifier}.{ 'bmp' if kind == 'bitmap' else 'ico'}"
    with Image.open(filename) as source:
        if kind != "bitmap" and size and hasattr(source, "ico"):
            sizes = source.ico.sizes()
            chosen = min(sizes, key=lambda pair: abs(pair[0] - size) + abs(pair[1] - size))
            image = source.ico.getimage(chosen).convert("RGBA")
            if image.size != (size, size):
                image = image.resize((size, size), Image.Resampling.NEAREST)
        else:
            image = source.convert("RGBA")
        images.append((group, kind, int(identifier), image.width, image.height, image.tobytes()))
result = ['#include "core/runtime/Resources.h"', '#include <cstring>', 'namespace vdev { namespace {']
for index, (*_, data) in enumerate(images):
    result.append(f"const unsigned char pixels_{index}[] = {{")
    result.extend(",".join(str(v) for v in data[offset:offset+96]) + "," for offset in range(0, len(data), 96))
    result.append("};")
result.append('const EmbeddedImage images[] = {')
for index, (group, kind, identifier, width, height, _) in enumerate(images):
    result.append(f'{{"{group}","{kind}",{identifier},{width},{height},pixels_{index}}},')
result += ['}; }', 'const EmbeddedImage *FindEmbeddedImage(const char *group, const char *kind, int id) {',
           'for (const auto &image : images) if (image.id == id && std::strcmp(image.group, group) == 0 && std::strcmp(image.kind, kind) == 0) return &image;',
           'return nullptr; } }']
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text("\n".join(result) + "\n", encoding="utf-8")
