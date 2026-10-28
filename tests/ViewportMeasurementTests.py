"""Check viewport statistics and comparison gates without a graphics device."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from MeasureViewport import compare, parse_measurement, summarize


def measurement(median=100):
    return {"version": 1, "scenario": "cubes64-v1", "scope": "editor_frame_gpu_drained",
            "driver": "test", "gpu_device": "test", "gpu_driver_info": "", "sdl_version": 3000,
            "viewport": [640, 480], "window": [1280, 800], "window_pixels": [1280, 800],
            "ui_scale": 1, "warmup_frames": 480, "requested_frames": 100,
            "minimum_warmup_ms": 15000, "minimum_group_ms": 1000,
            "stable_readbacks": 0, "stable_uploads": 0, "stable_target_creations": 0,
            "stable_path_selections": 0, "stable_target_releases": 0,
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

    def test_other_measurement_conditions_cannot_be_compared(self):
        baseline = report()
        for kind in ("driver", "viewport", "sdk", "dirty", "harness", "hardware"):
            with self.subTest(kind=kind):
                candidate = copy.deepcopy(baseline)
                if kind in ("driver", "viewport"): candidate["measurement"][kind] = "other"
                if kind == "sdk": candidate["environment"]["sdk_revisions"] = {"root": "other"}
                if kind == "dirty": candidate["environment"]["sdk_dirty"] = True
                if kind == "harness": candidate["environment"]["harness_sha256"] = "other"
                if kind == "hardware": candidate["environment"]["hardware"] = "other"
                with self.assertRaises(ValueError): compare(candidate, baseline)


if __name__ == "__main__":
    unittest.main()
