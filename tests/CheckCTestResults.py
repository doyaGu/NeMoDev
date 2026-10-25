"""Require every selected CTest test to finish without skips or failures."""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def check_results(report, inventory):
    expected = {test["name"] for test in json.loads(inventory.read_text(encoding="utf-8"))["tests"]}
    cases = list(ET.parse(report).getroot().iter("testcase"))
    names = [case.attrib["name"] for case in cases]
    if not expected or len(names) != len(set(names)) or set(names) != expected:
        raise RuntimeError("CTest results do not match the selected test inventory")
    unsuccessful = [case.attrib["name"] for case in cases
                    if any(case.find(tag) is not None for tag in ("skipped", "failure", "error"))]
    if unsuccessful:
        raise RuntimeError("CTest tests did not pass: " + ", ".join(unsuccessful))
    return len(cases)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("inventory", type=Path)
    args = parser.parse_args()
    print(f"All {check_results(args.report, args.inventory)} selected tests passed without skips")
