"""Check that incomplete and skipped CI regressions cannot produce a pass."""
import json
from pathlib import Path
import tempfile
import unittest

from CheckCTestResults import check_results
from CheckCTestPartitions import check_partitions


class CTestResultsTests(unittest.TestCase):
    def check_report(self, names, cases):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inventory, report = root / "inventory.json", root / "results.xml"
            inventory.write_text(json.dumps({"tests": [{"name": name} for name in names]}), encoding="utf-8")
            report.write_text("<testsuite>" + cases + "</testsuite>", encoding="utf-8")
            return check_results(report, inventory)

    def test_complete_results(self):
        self.assertEqual(self.check_report(["save", "undo"],
                         '<testcase name="undo"/><testcase name="save"/>'), 2)

    def test_skips_and_failures(self):
        for result in ("skipped", "failure", "error"):
            with self.subTest(result=result), self.assertRaises(RuntimeError):
                self.check_report(["save"], f'<testcase name="save"><{result}/></testcase>')

    def test_incomplete_or_wrong_inventory(self):
        for expected, cases in (([], ""), (["save"], ""),
                                (["save"], '<testcase name="undo"/>'),
                                (["save"], '<testcase name="save"/><testcase name="save"/>')):
            with self.subTest(expected=expected, cases=cases), self.assertRaises(RuntimeError):
                self.check_report(expected, cases)

    def partition_report(self, full, partitions, result=''):
        with tempfile.TemporaryDirectory() as directory:
            roots = []
            for i, selected in enumerate(partitions):
                root = Path(directory) / str(i)
                root.mkdir()
                roots.append(root)
                for file, names in (('full-test-inventory.json', full[i]), ('test-inventory.json', selected)):
                    (root / file).write_text(json.dumps({'tests': [{'name': name} for name in names]}), encoding='utf-8')
                (root / 'results.xml').write_text('<testsuite>' + ''.join(
                    f'<testcase name="{name}">{result}</testcase>' for name in selected) + '</testsuite>', encoding='utf-8')
            return check_partitions(roots)

    def test_complete_partitions(self):
        self.assertEqual(self.partition_report([['save', 'undo']] * 2, [['save'], ['undo']]), 2)

    def test_incomplete_overlapping_or_changed_partitions(self):
        for full, partitions in (([['save', 'undo']], [['save']]),
                                 ([['save']] * 2, [['save'], ['save']]),
                                 ([['save'], ['undo']], [['save'], ['undo']]),
                                 ([['save']], [['unexpected']]),
                                 ([['save', 'save']], [['save']]),
                                 ([['save']], [[]])):
            with self.subTest(full=full, partitions=partitions), self.assertRaises(RuntimeError):
                self.partition_report(full, partitions)

    def test_partition_results_must_pass(self):
        for result in ('<skipped/>', '<failure/>', '<error/>'):
            with self.subTest(result=result), self.assertRaises(RuntimeError):
                self.partition_report([['save']], [['save']], result)


if __name__ == "__main__":
    unittest.main()
