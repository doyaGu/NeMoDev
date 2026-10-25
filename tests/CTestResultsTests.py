"""Check that incomplete and skipped CI regressions cannot produce a pass."""
import json
from pathlib import Path
import tempfile
import unittest

from CheckCTestResults import check_results


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


if __name__ == "__main__":
    unittest.main()
