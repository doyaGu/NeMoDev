"""Require CI partitions to execute the complete test inventory exactly once."""
import argparse
import json
from pathlib import Path

from CheckCTestResults import check_results


def names(path):
    tests = [test['name'] for test in json.loads(path.read_text(encoding='utf-8'))['tests']]
    if not tests or len(set(tests)) != len(tests):
        raise RuntimeError('Empty or duplicate CTest inventory: ' + str(path))
    return set(tests)


def check_partitions(directories):
    expected, completed = None, set()
    for directory in directories:
        full = names(directory / 'full-test-inventory.json')
        if expected is not None and full != expected:
            raise RuntimeError('CTest partitions used different full inventories')
        expected = full
        inventory = directory / 'test-inventory.json'
        selected = names(inventory)
        check_results(directory / 'results.xml', inventory)
        if completed & selected:
            raise RuntimeError('CTest partitions executed a test more than once')
        completed.update(selected)
    if not expected or completed != expected:
        raise RuntimeError('CTest partitions did not execute the complete inventory')
    return len(completed)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', type=Path, nargs='+')
    args = parser.parse_args()
    print(f'All {check_partitions(args.directories)} tests passed exactly once across CI partitions')
