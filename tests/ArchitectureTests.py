"""Exercise the architecture gate through its command-line interface."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


CHECKER = Path(__file__).with_name("CheckArchitecture.py")


class ArchitectureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.source = self.folder / "source"
        self.source.mkdir()
        subprocess.run(["git", "init", "-q", str(self.source)], check=True)
        self.write("core/model.h", "#pragma once\n")
        self.manifest = {
            "version": 1, "completed_phase": 0,
            "source_groups": [{"id": "core", "patterns": ["core/**"]}],
            "external_submodules": [],
            "dependencies": {}, "legacy_exposures": [],
            "features": [{"id": "edit", "name": "Editing", "owner": "Document",
                          "source_groups": ["core"], "tests": ["EditTest"],
                          "ui_tests": [], "gaps": []}],
            "states": [{"id": "content", "owner": "Document", "lifetime": "document",
                        "features": ["edit"]}],
        }
        self.inventory = {"tests": [{"name": "EditTest"}]}

    def write(self, name, text):
        path = self.source / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def run_check(self, *extra):
        manifest, inventory = self.folder / "coverage.json", self.folder / "tests.json"
        manifest.write_text(json.dumps(self.manifest), encoding="utf-8")
        inventory.write_text(json.dumps(self.inventory), encoding="utf-8")
        return subprocess.run([sys.executable, str(CHECKER), "--source", str(self.source),
                               "--manifest", str(manifest), "--inventory", str(inventory), *extra],
                              capture_output=True, text=True)

    def test_unowned_new_file_is_rejected(self):
        self.write("new_feature/edit.cpp", "// Not assigned to a module.\n")
        result = self.run_check()
        self.assertEqual(result.returncode, 1)
        self.assertIn("unowned file: new_feature/edit.cpp", result.stderr)

    def add_ui(self, text):
        self.write("ui/view.cpp", text)
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")

    def test_complete_source_and_test_inventory(self):
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["files"], 1)

    def test_duplicate_ownership_is_rejected(self):
        self.manifest["source_groups"].append({"id": "other", "patterns": ["core/**"]})
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("multiple owners for core/model.h", result.stderr)

    def test_new_dependency_requires_explicit_review(self):
        self.add_ui('#include "core/model.h"\n')
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("new dependency: ui -> core", result.stderr)

    def test_objective_cpp_imports_are_checked(self):
        self.add_ui('// Platform-independent view.\n')
        self.write("ui/view.mm", '#import "core/model.h"\n')
        self.write("core/model.h", '#import "CKAll.h"\n')
        result = self.run_check()
        self.assertIn("new dependency: ui -> core", result.stderr)
        self.assertIn("unrecorded native/state exposure: ui/view.mm: native-sdk", result.stderr)
        self.manifest["dependencies"] = {"ui": ["core"]}
        self.manifest["legacy_exposures"] = [
            {"file": "ui/view.mm", "access": ["native-sdk"], "remove_by": 5}]
        self.assertEqual(self.run_check().returncode, 0)

    def test_indirect_state_exposure_is_rejected(self):
        self.write("core/document/Document.h", "#pragma once\n")
        self.write("core/model.h", '#include "core/document/Document.h"\n')
        self.add_ui('#include "core/model.h"\n')
        self.manifest["dependencies"] = {"ui": ["core"]}
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unrecorded native/state exposure: ui/view.cpp: mutable-document", result.stderr)
        self.manifest["legacy_exposures"] = [
            {"file": "ui/view.cpp", "access": ["mutable-document"], "remove_by": 5}]
        self.assertEqual(self.run_check().returncode, 0)

    def test_scene_views_cannot_gain_indirect_native_access(self):
        self.write("features/scene/SceneViewData.h", '#include "core/model.h"\n')
        self.write("features/scene/SceneTreeView.cpp", '#include "features/scene/SceneViewData.h"\n')
        self.write("core/model.h", '#include "CKAll.h"\n')
        self.manifest["source_groups"].append({"id": "views", "patterns": ["features/**"]})
        self.manifest["features"][0]["source_groups"].append("views")
        self.manifest["dependencies"] = {"views": ["core"]}
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native/state access in workspace view: features/scene/SceneTreeView.cpp", result.stderr)

    def test_resource_views_cannot_gain_indirect_native_access(self):
        self.write("features/resources/BuildingBlockViews.h", '#include "core/model.h"\n')
        self.write("features/resources/BuildingBlockViews.cpp", '#include "features/resources/BuildingBlockViews.h"\n')
        self.write("core/model.h", '#include "CKAll.h"\n')
        self.manifest["source_groups"].append({"id": "views", "patterns": ["features/**"]})
        self.manifest["features"][0]["source_groups"].append("views")
        self.manifest["dependencies"] = {"views": ["core"]}
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native/state access in workspace view: features/resources/BuildingBlockViews.cpp", result.stderr)

    def test_data_resource_views_cannot_gain_indirect_native_access(self):
        self.write("features/resources/DataResourceViews.h", '#include "core/model.h"\n')
        self.write("features/resources/DataResourceViews.cpp", '#include "features/resources/DataResourceViews.h"\n')
        self.write("core/model.h", '#include "CKAll.h"\n')
        self.manifest["source_groups"].append({"id": "views", "patterns": ["features/**"]})
        self.manifest["features"][0]["source_groups"].append("views")
        self.manifest["dependencies"] = {"views": ["core"]}
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native/state access in workspace view: features/resources/DataResourceViews.cpp", result.stderr)

    def test_array_table_cannot_gain_indirect_native_access(self):
        self.write("features/arrays/ArrayTable.h", '#include "core/model.h"\n')
        self.write("features/arrays/ArrayTable.cpp", '#include "features/arrays/ArrayTable.h"\n')
        self.write("core/model.h", '#include "CKAll.h"\n')
        self.manifest["source_groups"].append({"id": "views", "patterns": ["features/**"]})
        self.manifest["features"][0]["source_groups"].append("views")
        self.manifest["dependencies"] = {"views": ["core"]}
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native/state access in workspace view: features/arrays/ArrayTable.cpp", result.stderr)

    def test_setup_view_cannot_gain_indirect_native_access(self):
        self.write("features/setup/SetupView.h", '#include "core/model.h"\n')
        self.write("features/setup/SetupView.cpp", '#include "features/setup/SetupView.h"\n')
        self.write("core/model.h", '#include "CKAll.h"\n')
        self.manifest["source_groups"].append({"id": "views", "patterns": ["features/**"]})
        self.manifest["features"][0]["source_groups"].append("views")
        self.manifest["dependencies"] = {"views": ["core"]}
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native/state access in workspace view: features/setup/SetupView.cpp", result.stderr)

    def test_definition_include_is_checked(self):
        self.write("core/properties.def", '#include "CKAll.h"\n')
        self.add_ui('#include "core/properties.def"\n')
        self.manifest["dependencies"] = {"ui": ["core"]}
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unrecorded native/state exposure: ui/view.cpp: native-sdk", result.stderr)

    def test_default_workspace_cannot_gain_indirect_native_access(self):
        self.write("features/defaults/DefaultWorkspace.h", '#include "core/model.h"\n')
        self.write("features/defaults/DefaultWorkspace.cpp", '#include "features/defaults/DefaultWorkspace.h"\n')
        self.write("core/model.h", '#include "CKAll.h"\n')
        self.manifest["source_groups"].append({"id": "views", "patterns": ["features/**"]})
        self.manifest["features"][0]["source_groups"].append("views")
        self.manifest["dependencies"] = {"views": ["core"]}
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native/state access in workspace view: features/defaults/DefaultWorkspace.cpp", result.stderr)

    def test_value_editor_rejects_indirect_native_access(self):
        self.write("features/values/ValueEditors.h", '#include "core/model.h"\n')
        self.write("features/values/ValueEditors.cpp", '#include "features/values/ValueEditors.h"\n')
        self.write("core/model.h", '#include "CKAll.h"\n')
        self.manifest["source_groups"].append({"id": "views", "patterns": ["features/**"]})
        self.manifest["features"][0]["source_groups"].append("views")
        self.manifest["dependencies"] = {"views": ["core"]}
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native/state access in workspace view: features/values/ValueEditors.cpp", result.stderr)

    def test_stale_and_expired_legacy_exposure_is_rejected(self):
        self.add_ui('#include "CKAll.h"\n')
        self.manifest["legacy_exposures"] = [
            {"file": "ui/view.cpp", "access": ["native-sdk"], "remove_by": 5}]
        self.assertEqual(self.run_check().returncode, 0)
        self.manifest["completed_phase"] = 5
        self.assertIn("expired legacy exposure", self.run_check().stderr)
        self.manifest["completed_phase"] = 0
        self.write("ui/view.cpp", "// This view no longer reaches CK.\n")
        self.assertIn("stale legacy exposure", self.run_check().stderr)

    def test_new_permission_cannot_be_hidden_in_legacy_allowlist(self):
        # Freeze the original source, then try to bless a new UI exposure by
        # editing only the migration exception list.
        self.write("ui/view.cpp", "// No runtime access in the original source.\n")
        subprocess.run(["git", "-C", str(self.source), "add", "."], check=True)
        subprocess.run(["git", "-C", str(self.source), "-c", "user.name=Architecture Test",
                        "-c", "user.email=test@example.invalid", "-c", "commit.gpgsign=false",
                        "-c", "core.hooksPath=" + str(self.folder / "no-hooks"),
                        "commit", "-qm", "Baseline"], check=True)
        self.manifest["baseline_revision"] = subprocess.check_output(
            ["git", "-C", str(self.source), "rev-parse", "HEAD"], text=True).strip()
        self.add_ui('#include "CKAll.h"\n')
        self.manifest["legacy_exposures"] = [
            {"file": "ui/view.cpp", "access": ["native-sdk"], "remove_by": 5}]
        result = self.run_check()
        self.assertIn("legacy exposure exceeds fixed baseline", result.stderr)

    def test_checks_and_owners_cannot_disappear(self):
        self.inventory["tests"].clear()
        self.assertIn("missing registered test: edit: EditTest", self.run_check().stderr)
        self.manifest["states"][0]["owner"] = ""
        self.assertIn("state has no owner/lifetime", self.run_check().stderr)
        self.manifest["features"][0]["owner"] = ""
        self.assertIn("feature has no name/owner", self.run_check().stderr)

    def test_verification_gaps_expire_at_the_declared_phase(self):
        self.manifest["features"][0]["gaps"] = [{"reason": "Failure recovery", "due_phase": 2}]
        self.assertEqual(self.run_check().returncode, 0)
        self.manifest["completed_phase"] = 2
        self.assertIn("overdue verification gap", self.run_check().stderr)

    def test_ui_test_anchors_are_checked_when_ui_is_enabled(self):
        self.manifest["features"][0]["ui_tests"] = ["UiTest"]
        self.assertEqual(self.run_check().returncode, 0)
        self.assertIn("missing registered test: edit: UiTest", self.run_check("--ui-tests").stderr)

    def test_baseline_core_cases_cannot_be_removed_from_the_registry(self):
        self.write("tests/VirtoolsDevCases.def", "VIRTOOLSDEV_CASE(Original)\n")
        self.manifest["source_groups"].append({"id": "tests", "patterns": ["tests/**"]})
        self.manifest["features"][0]["source_groups"].append("tests")
        self.manifest["required_core_cases"] = ["Original"]
        self.inventory["tests"].append({"name": "VirtoolsDev.Original"})
        self.assertEqual(self.run_check().returncode, 0)
        self.write("tests/VirtoolsDevCases.def", "VIRTOOLSDEV_CASE(Other)\n")
        self.assertIn("missing baseline core case: Original", self.run_check().stderr)


if __name__ == "__main__":
    unittest.main()
