"""Verify the bundled Noto fonts and licenses, downloading missing files."""
import argparse
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "resources/fonts")
    args = parser.parse_args()
    sources = json.loads((ROOT / "resources/fonts/sources.json").read_text(encoding="utf-8"))
    args.output.mkdir(parents=True, exist_ok=True)
    for name, source in sources.items():
        target = args.output / name
        if target.is_file() and hashlib.sha256(target.read_bytes()).hexdigest() == source["sha256"]:
            print(f"Verified {name}")
            continue
        if target.exists():
            raise RuntimeError(f"Preserve or remove the different local file before retrying: {target}")
        with urlopen(source["url"], timeout=60) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != source["sha256"]:
            raise RuntimeError(f"Font checksum mismatch: {name}")
        temporary = target.with_suffix(target.suffix + ".download")
        try:
            temporary.write_bytes(data)
            temporary.replace(target)
        finally:
            temporary.unlink(missing_ok=True)
        print(f"Downloaded {name}")


if __name__ == "__main__":
    main()
