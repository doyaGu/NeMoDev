"""Exercise the real editor's module-directory startup and failed assembly."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    args = parser.parse_args()
    args.results.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nemodev-module-startup-") as temporary:
        root = Path(temporary) / "中文 modules"
        root.mkdir()
        module = root / args.module.name
        shutil.copy2(args.module, module)

        def run(name, *options):
            report = args.results / (name + ".xml")
            command = [str(args.binary), "--ui-tests", "discovered_module_uses_builtin_workspace_and_queries",
                       "--ui-results", str(report), *map(str, options)]
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
            (args.results / (name + ".log")).write_bytes(result.stdout)
            return result, report

        # The same canonical module is supplied by directory and explicit file.
        result, report = run("discovered", "--editor-module-dir", root, "--editor-module", module)
        if result.returncode != 0 or not report.is_file():
            raise RuntimeError("Discovered module did not start: " + result.stdout.decode(errors="replace"))
        selected = [case for case in ET.parse(report).getroot().iter("testcase") if case.find("skipped") is None]
        if len(selected) != 1 or selected[0].get("status") != "Success":
            raise RuntimeError("Discovered module view was not exercised successfully")

        for name, location, expected in (
            ("missing", root / "absent", b"Expected a module directory"),
            ("broken", root, b"Cannot load module entry"),
            ("duplicate", root, b"Duplicate module identifier"),
        ):
            broken = root / ("a-broken" + module.suffix)
            duplicate = root / ("another-copy" + module.suffix)
            if name == "broken":
                broken.write_bytes(b"Not a native module")
            if name == "duplicate":
                shutil.copy2(module, duplicate)
            result, _ = run(name, "--editor-module-dir", location)
            if result.returncode != 1 or expected not in result.stdout:
                raise RuntimeError(f"Unexpected {name} startup result {result.returncode}: " +
                                   result.stdout.decode(errors="replace"))
            broken.unlink(missing_ok=True)
            duplicate.unlink(missing_ok=True)
    print("Real editor discovered, opened and closed the public example; failed startups cleaned up")


if __name__ == "__main__":
    main()
