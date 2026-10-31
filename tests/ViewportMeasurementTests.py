"""Check viewport statistics and comparison gates without a graphics device."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from MeasureViewport import ViewportProcess, compare, parse_measurement, summarize


def measurement(median=100):
    return {"version": 1, "scenario": "cubes64-v1", "scope": "editor_frame_gpu_drained",
            "driver": "test", "gpu_device": "test", "gpu_driver_info": "", "sdl_version": 3000,
            "viewport": [640, 480], "window": [1280, 800], "window_pixels": [1280, 800],
            "ui_scale": 1, "warmup_frames": 480, "requested_frames": 100,
            "minimum_warmup_ms": 15000, "minimum_group_ms": 1000,
            "stable_readbacks": 0, "stable_uploads": 0, "stable_target_creations": 0,
            "stable_path_selections": 0, "stable_target_releases": 0,
            "verification_readbacks": 1,
            "samples": [{"frames": 100, "elapsed_ms": value * 100} for value in
                        [median - 2, median - 1, median - 1, median - 1, median, median,
                         median, median + 1, median + 1, median + 1, median + 2]]}


def report(median=100):
    return {"measurement": measurement(median), "environment": {
        "platform": "test", "machine": "test", "hardware": "test", "build_type": "Release",
        "compiler": "test", "sdk_revisions": {"root": "revision"}, "sdk_dirty": False,
        "compiler_flags": {}, "ui_dependencies": "test",
        "harness_sha256": "harness", "binary_sha256": "binary-" + str(median)}}


class ViewportMeasurementTests(unittest.TestCase):
    def test_native_console_color_does_not_hide_the_measurement(self):
        self.assertEqual(parse_measurement('\x1b[0mNEMODEV_VIEWPORT_BASELINE {"version":1}\n'), {"version": 1})
        with self.assertRaises(ValueError): parse_measurement("No complete native result\n")

    def test_worked_statistics(self):
        self.assertEqual(summarize(measurement()), {
            "median_ms": 100, "p10_ms": 99, "p90_ms": 101, "mad_ms": 1, "mad_percent": 1})

    def test_incomplete_invalid_or_readback_samples_are_rejected(self):
        for kind in ("short", "nan", "empty", "readback"):
            with self.subTest(kind=kind):
                value = measurement()
                if kind == "short": value["samples"].pop()
                if kind == "nan": value["samples"][0]["elapsed_ms"] = float("nan")
                if kind == "empty": value["samples"][0]["frames"] = 0
                if kind == "readback": value["stable_readbacks"] = 1
                with self.assertRaises(ValueError): summarize(value)

    def test_three_percent_regression_limit(self):
        self.assertTrue(compare(report(103), report())["passed"])
        self.assertFalse(compare(report(104), report())["passed"])

    def test_same_binary_drift_is_not_an_architecture_improvement(self):
        baseline, control = report(), report(80)
        control["environment"]["binary_sha256"] = baseline["environment"]["binary_sha256"]
        result = compare(control, baseline)
        self.assertTrue(result["same_binary_control"])
        self.assertFalse(result["passed"])

    def test_intentional_sdk_change_retains_other_comparison_gates(self):
        baseline, candidate = report(), report(102)
        candidate["environment"]["sdk_dirty"] = True
        candidate["environment"]["sdk_revisions"] = {"root": "changed"}
        with self.assertRaises(ValueError): compare(candidate, baseline)
        self.assertTrue(compare(candidate, baseline, sdk_change=True)["passed"])
        candidate["measurement"]["viewport"] = [800, 600]
        with self.assertRaises(ValueError): compare(candidate, baseline, sdk_change=True)

    def test_changed_sdk_binary_is_not_a_same_binary_control(self):
        baseline, candidate = report(), report()
        candidate["environment"]["sdk_runtime_sha256"] = {"CK2": "changed"}
        self.assertFalse(compare(candidate, baseline, sdk_change=True)["same_binary_control"])

    def test_other_measurement_conditions_cannot_be_compared(self):
        baseline = report()
        for kind in ("driver", "viewport", "sdk", "dirty", "harness", "hardware", "launch_policy"):
            with self.subTest(kind=kind):
                candidate = copy.deepcopy(baseline)
                if kind in ("driver", "viewport"): candidate["measurement"][kind] = "other"
                if kind == "sdk": candidate["environment"]["sdk_revisions"] = {"root": "other"}
                if kind == "dirty": candidate["environment"]["sdk_dirty"] = True
                if kind == "harness": candidate["environment"]["harness_sha256"] = "other"
                if kind == "hardware": candidate["environment"]["hardware"] = "other"
                if kind == "launch_policy": candidate["environment"]["launch_policy"] = "other"
                with self.assertRaises(ValueError): compare(candidate, baseline)

    def test_paired_and_continuous_groups_are_distinct(self):
        baseline, candidate = report(), report()
        for value in (baseline, candidate):
            value["measurement"].update(version=2, group_control="paired", cpu_policy="system-default")
        self.assertTrue(compare(candidate, baseline)["passed"])
        candidate["measurement"]["group_control"] = "continuous"
        with self.assertRaises(ValueError): compare(candidate, baseline)

    def test_unknown_group_control_is_rejected(self):
        value = measurement()
        value.update(version=2, group_control="unknown")
        with self.assertRaises(ValueError): summarize(value)

    def test_different_cpu_policies_cannot_be_compared(self):
        baseline, candidate = report(), report()
        for value in (baseline, candidate):
            value["measurement"].update(version=2, group_control="paired", cpu_policy="user-initiated-activity")
        candidate["measurement"]["cpu_policy"] = "system-default"
        with self.assertRaises(ValueError): compare(candidate, baseline)

    def test_viewport_waits_between_groups_and_keeps_its_result(self):
        value = measurement()
        value.update(version=2, group_control="paired", cpu_policy="system-default")
        script = '''import json, sys
for group in range(2):
    print("\\x1b[0mNEMODEV_VIEWPORT_READY " + str(group), flush=True)
    if sys.stdin.readline() != "\\n": sys.exit(1)
print("NEMODEV_VIEWPORT_BASELINE " + sys.argv[1], flush=True)
'''
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "viewport.json"
            process = ViewportProcess([sys.executable, "-u", "-c", script, json.dumps(value)], output, None)
            try:
                deadline = time.monotonic() + 10
                process.ready(0, deadline)
                self.assertIsNone(process.process.poll())
                process.render_group()
                process.ready(1, deadline)
                process.render_group()
                self.assertEqual(process.finish(deadline), value)
            finally:
                process.close()
            self.assertIn("NEMODEV_VIEWPORT_BASELINE", output.with_suffix(".log").read_text())

    def test_bad_group_or_timeout_stops_the_waiting_viewport(self):
        for marker in ("NEMODEV_VIEWPORT_READY 9", "Still starting"):
            with self.subTest(marker=marker), tempfile.TemporaryDirectory() as folder:
                script = 'import sys; print(sys.argv[1], flush=True); sys.stdin.read()'
                process = ViewportProcess([sys.executable, "-u", "-c", script, marker],
                                          Path(folder) / "viewport.json", None)
                try:
                    with self.assertRaises(ValueError): process.ready(0, time.monotonic() + 0.2)
                finally:
                    process.close()
                self.assertIsNotNone(process.process.poll())

    def test_early_exit_does_not_become_a_partial_measurement(self):
        with tempfile.TemporaryDirectory() as folder:
            process = ViewportProcess([sys.executable, "-c", 'print("Native initialization failed")'],
                                      Path(folder) / "viewport.json", None)
            try:
                with self.assertRaises(ValueError): process.ready(0, time.monotonic() + 10)
            finally:
                process.close()


if __name__ == "__main__":
    unittest.main()
