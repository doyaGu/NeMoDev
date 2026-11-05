"""Check viewport statistics and comparison gates without a graphics device."""
import copy
import json
import math
from pathlib import Path
import sys
import subprocess
import tempfile
import time
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from MeasureViewport import ViewportProcess, compare, measurement_report, measurement_timeout, parse_measurement, summarize, validate_request


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


def conditioned_measurement():
    value = measurement()
    value.update(version=3, group_control="paired", cpu_policy="user-initiated-activity", thermal_policy="nominal",
                 minimum_cooldown_ms=30000, maximum_cooldown_ms=180000, cooldown_ms=30500)
    for sample in value["samples"]:
        sample.update(cpu_usage_available=True, thermal_before=0, thermal_after=0,
                      low_power_before=False, low_power_after=False, task_role_before=2, task_role_after=2)
    return value


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

    def test_both_programs_must_meet_the_original_variation_limit(self):
        for side in ("candidate", "baseline"):
            with self.subTest(side=side):
                baseline, candidate = report(), report()
                noisy = candidate if side == "candidate" else baseline
                for sample, value in zip(noisy["measurement"]["samples"],
                                         [90, 91, 92, 93, 94, 100, 106, 107, 108, 109, 110]):
                    sample["elapsed_ms"] = value * sample["frames"]
                if side == "baseline":
                    with self.assertRaisesRegex(ValueError, "variation exceeds 3 percent"):
                        compare(candidate, baseline)
                else:
                    self.assertFalse(compare(candidate, baseline)["passed"])

    def test_slow_groups_are_kept_and_idle_or_cpu_time_is_never_subtracted(self):
        value = measurement()
        for index, sample in enumerate(value["samples"]):
            sample.update(elapsed_ms=(200 if index < 6 else 100) * sample["frames"],
                          idle_ms=5000, drain_ms=20, thread_cpu_ms=100)
        self.assertEqual(summarize(value)["median_ms"], 200)
        self.assertEqual(len(value["samples"]), 11)

    def test_longer_groups_keep_the_original_minima_and_all_frame_time(self):
        value = measurement()
        value.update(minimum_warmup_ms=45000, minimum_group_ms=5000)
        self.assertEqual(summarize(value)["median_ms"], 100)
        value["samples"][0]["elapsed_ms"] = 4999
        with self.assertRaises(ValueError): summarize(value)
        for field, minimum in (("minimum_warmup_ms", 15000), ("minimum_group_ms", 1000)):
            value = measurement()
            value[field] = minimum - 1
            with self.assertRaises(ValueError): summarize(value)

    def test_longer_measurements_keep_a_finite_timeout_for_both_programs(self):
        for paired in (False, True):
            deadline = measurement_timeout(11, 45000, 5000, paired)
            self.assertTrue(math.isfinite(deadline))
            self.assertGreaterEqual(deadline, (2 if paired else 1) * 100 + 120)
        self.assertEqual(measurement_timeout(11, 15000, 1000, False), 180)
        self.assertEqual(measurement_timeout(11, 15000, 1000, True), 360)
        self.assertGreaterEqual(measurement_timeout(11, 15000, 1000, True, 180000), 532)

    def test_cli_cannot_request_shorter_warmup_or_groups(self):
        script = Path(__file__).resolve().parent.parent / "tools/MeasureViewport.py"
        for flag, value in (("--warmup-ms", "14999"), ("--group-ms", "999")):
            with self.subTest(flag=flag), tempfile.TemporaryDirectory() as folder:
                result = subprocess.run([sys.executable, str(script), "--build", folder,
                    "--binary", "unused", "--output", str(Path(folder) / "unused.json"), flag, value],
                    capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertIn("warmup-ms must be 15000..120000, group-ms 1000..10000", result.stderr)

    def test_native_program_must_honor_every_requested_timing_condition(self):
        args = SimpleNamespace(samples=11, warmup=480, frames=100, warmup_ms=45000,
                               group_ms=5000, reference_binary=True)
        value = measurement()
        value.update(version=3, minimum_warmup_ms=45000, minimum_group_ms=5000, group_control="paired",
                     thermal_policy="nominal" if sys.platform == "darwin" else "system-default",
                     minimum_cooldown_ms=30000 if sys.platform == "darwin" else 0,
                     maximum_cooldown_ms=180000 if sys.platform == "darwin" else 0)
        validate_request(value, args)
        for key in ("samples", "warmup_frames", "requested_frames", "minimum_warmup_ms", "minimum_group_ms", "group_control",
                    "version", "thermal_policy", "minimum_cooldown_ms", "maximum_cooldown_ms"):
            with self.subTest(key=key):
                changed = copy.deepcopy(value)
                if key == "samples": changed[key].pop()
                elif key in ("group_control", "thermal_policy"): changed[key] = "other"
                else: changed[key] -= 1
                with self.assertRaises(ValueError): validate_request(changed, args)

    def test_nominal_thermal_preparation_is_required_before_sampling(self):
        self.assertEqual(summarize(conditioned_measurement())["median_ms"], 100)
        for key, invalid in (("minimum_cooldown_ms", 29999), ("maximum_cooldown_ms", 29999),
                             ("cooldown_ms", 29999), ("cooldown_ms", float("nan")), ("thermal_policy", "unknown")):
            with self.subTest(key=key, invalid=invalid):
                value = conditioned_measurement()
                value[key] = invalid
                with self.assertRaises(ValueError): summarize(value)

    def test_a_thermal_or_power_transition_invalidates_the_entire_capture(self):
        for index in (0, 10):
            for key, invalid in (("thermal_before", 1), ("thermal_after", 1), ("low_power_before", True),
                                 ("low_power_after", True), ("cpu_usage_available", False)):
                with self.subTest(index=index, key=key):
                    value = conditioned_measurement()
                    value["samples"][index][key] = invalid
                    result = measurement_report({}, value)
                    self.assertIs(result["measurement"], value)
                    self.assertEqual(len(result["measurement"]["samples"]), 11)
                    self.assertIsNone(result["statistics"])
                    self.assertIn("thermal or power state", result["validation_error"])

    def test_changed_or_mismatched_process_roles_are_rejected(self):
        value = conditioned_measurement()
        value["samples"][-1]["task_role_after"] = 1
        with self.assertRaises(ValueError): summarize(value)
        baseline, candidate = report(), report()
        baseline["measurement"], candidate["measurement"] = conditioned_measurement(), conditioned_measurement()
        for sample in candidate["measurement"]["samples"]:
            sample.update(task_role_before=1, task_role_after=1)
        with self.assertRaises(ValueError): compare(candidate, baseline)

    def test_empty_conditioned_capture_is_rejected_before_reading_its_role(self):
        baseline, candidate = report(), report()
        baseline["measurement"], candidate["measurement"] = conditioned_measurement(), conditioned_measurement()
        for side in (baseline, candidate):
            original = side["measurement"]["samples"]
            side["measurement"]["samples"] = []
            with self.assertRaisesRegex(ValueError, "at least 11 sample groups"):
                compare(candidate, baseline)
            side["measurement"]["samples"] = original

    def test_thermal_preparation_cannot_bypass_the_three_percent_gates(self):
        baseline, candidate = report(), report(104)
        for value in (baseline, candidate):
            original = value["measurement"]["samples"]
            value["measurement"] = conditioned_measurement()
            for target, source in zip(value["measurement"]["samples"], original):
                target.update(frames=source["frames"], elapsed_ms=source["elapsed_ms"])
        self.assertFalse(compare(candidate, baseline)["passed"])

    def test_verification_readback_is_required_outside_stable_frames(self):
        for count in (None, 0, 2):
            with self.subTest(count=count):
                value = measurement()
                value["verification_readbacks"] = count
                with self.assertRaises(ValueError): summarize(value)

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

    def test_unchanged_sdk_measurements_require_identical_runtime_images(self):
        baseline, candidate = report(), report()
        candidate["environment"]["sdk_runtime_sha256"] = {"CK2": "changed"}
        with self.assertRaisesRegex(ValueError, "sdk_runtime"):
            compare(candidate, baseline)

    def test_changed_sdk_binary_is_not_a_same_binary_control(self):
        baseline, candidate = report(), report()
        candidate["environment"]["sdk_runtime_sha256"] = {"CK2": "changed"}
        self.assertFalse(compare(candidate, baseline, sdk_change=True)["same_binary_control"])

    def test_other_measurement_conditions_cannot_be_compared(self):
        baseline = report()
        for kind in ("driver", "viewport", "minimum_warmup_ms", "minimum_group_ms", "sdk", "dirty", "harness", "hardware", "launch_policy"):
            with self.subTest(kind=kind):
                candidate = copy.deepcopy(baseline)
                if kind in ("driver", "viewport"): candidate["measurement"][kind] = "other"
                if kind in ("minimum_warmup_ms", "minimum_group_ms"): candidate["measurement"][kind] *= 2
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
