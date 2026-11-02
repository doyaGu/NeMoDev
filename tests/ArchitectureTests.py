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

    def test_runtime_views_cannot_gain_native_or_mutable_state(self):
        for include in ('CKAll.h', 'core/document/Document.h', 'ui/app/EditorState.h'):
            with self.subTest(include=include):
                if '/' in include:
                    self.write(include, '#pragma once\n')
                self.write('core/model.h', '#include "' + include + '"\n')
                self.write('features/runtime/RuntimeViewData.h', '#include "core/model.h"\n')
                self.write('features/runtime/DebuggerView.cpp', '#include "features/runtime/RuntimeViewData.h"\n')
                self.manifest['source_groups'] = [
                    {'id': 'core', 'patterns': ['core/**', 'ui/**']},
                    {'id': 'views', 'patterns': ['features/**']}]
                self.manifest['features'][0]['source_groups'] = ['core', 'views']
                self.manifest['dependencies'] = {'views': ['core']}
                result = self.run_check()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('native/state access in workspace view: features/runtime/DebuggerView.cpp', result.stderr)

    def test_environment_views_cannot_gain_native_or_mutable_state(self):
        for include in ('CKAll.h', 'core/document/Document.h', 'ui/app/EditorState.h'):
            with self.subTest(include=include):
                if '/' in include:
                    self.write(include, '#pragma once\n')
                self.write('core/model.h', '#include "' + include + '"\n')
                self.write('features/environment/EnvironmentViewData.h', '#include "core/model.h"\n')
                self.write('features/environment/PathView.cpp', '#include "features/environment/EnvironmentViewData.h"\n')
                self.manifest['source_groups'] = [
                    {'id': 'core', 'patterns': ['core/**', 'ui/**']},
                    {'id': 'views', 'patterns': ['features/**']}]
                self.manifest['features'][0]['source_groups'] = ['core', 'views']
                self.manifest['dependencies'] = {'views': ['core']}
                result = self.run_check()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('native/state access in workspace view: features/environment/PathView.cpp', result.stderr)

    def test_diagnostic_views_cannot_gain_native_or_mutable_state(self):
        for include in ('CKAll.h', 'core/document/Document.h', 'ui/app/EditorState.h'):
            with self.subTest(include=include):
                if '/' in include:
                    self.write(include, '#pragma once\n')
                self.write('core/model.h', '#include "' + include + '"\n')
                self.write('features/diagnostics/DiagnosticViewData.h', '#include "core/model.h"\n')
                self.write('features/diagnostics/DiagnosticViews.cpp', '#include "features/diagnostics/DiagnosticViewData.h"\n')
                self.manifest['source_groups'] = [
                    {'id': 'core', 'patterns': ['core/**', 'ui/**']},
                    {'id': 'views', 'patterns': ['features/**']}]
                self.manifest['features'][0]['source_groups'] = ['core', 'views']
                self.manifest['dependencies'] = {'views': ['core']}
                result = self.run_check()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('native/state access in workspace view: features/diagnostics/DiagnosticViews.cpp', result.stderr)

    def test_preferences_views_cannot_gain_native_or_mutable_state(self):
        for include in ('CKAll.h', 'core/document/Document.h', 'ui/app/EditorState.h'):
            with self.subTest(include=include):
                if '/' in include:
                    self.write(include, '#pragma once\n')
                self.write('core/model.h', '#include "' + include + '"\n')
                self.write('features/preferences/PreferencesViewData.h', '#include "core/model.h"\n')
                self.write('features/preferences/PreferencesView.cpp', '#include "features/preferences/PreferencesViewData.h"\n')
                self.manifest['source_groups'] = [
                    {'id': 'core', 'patterns': ['core/**', 'ui/**']},
                    {'id': 'views', 'patterns': ['features/**']}]
                self.manifest['features'][0]['source_groups'] = ['core', 'views']
                self.manifest['dependencies'] = {'views': ['core']}
                result = self.run_check()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('native/state access in workspace view: features/preferences/PreferencesView.cpp', result.stderr)

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

    def test_setup_window_management_cannot_regain_native_access(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        for name in ("Open", "Show", "ChangeTarget", "OpenSetups", "Close", "CloseAll", "Draw", "SetupPick", "PortalPick", "DrawSetup", "Header", "Sidebar", "Picture", "PrepareFrame", "Save", "Restore", "Instances", "Navigate"):
            for body in ("m_State.document->Context();", "m_State.preferences.Bool(\"Name\");"):
                with self.subTest(name=name, body=body):
                    self.write("ui/setup/SetupWindows.cpp", "void SetupWindows::" + name + "() { " + body + " }")
                    result = self.run_check()
                    self.assertIn("native mutation or query returned to Setup window management: " + name, result.stderr)
        self.write("ui/setup/SetupWindows.cpp", "void SetupWindows::DrawSetup() { ReadSetupPage(Client(), target, 0, page); }")
        self.assertEqual(self.run_check().returncode, 0)

    def test_setup_drawing_uses_workspace(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        self.write("ui/app/EditorApp.cpp", "void EditorApp::Frame() { m_Setups.Draw(dock); }")
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Setup drawing returned to EditorApp", result.stderr)
        self.write("ui/app/EditorApp.cpp", "void EditorApp::Frame() { m_Workspace.Draw(); }")
        self.assertEqual(self.run_check().returncode, 0)

    def test_layout_drawing_uses_workspace(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        self.write("ui/app/EditorApp.cpp", "void EditorApp::Frame() { m_Layout.Draw(dock); }")
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Layout drawing returned to EditorApp", result.stderr)
        self.write("ui/app/EditorApp.cpp", "void EditorApp::Frame() { m_Workspace.Draw(); }")
        self.assertEqual(self.run_check().returncode, 0)

    def test_setup_class_pages_cannot_regain_native_access(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        for path, name in (("SetupRenderPages", "MeshPage"), ("SetupScenePages", "SoundPage"),
                           ("SetupObjectPages", "BehaviorPage"), ("SetupObjectPages", "AttributePage")):
            with self.subTest(page=name):
                file = "ui/setup/" + path + ".cpp"
                self.write(file, "void SetupWindows::" + name + "() { m_State.document->Context(); }")
                result = self.run_check()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("native mutation or query returned", result.stderr)
                self.write(file, "void SetupWindows::" + name + "() { ReadDetails(target, page, copy); }")
        self.assertEqual(self.run_check().returncode, 0)

    def test_schematic_view_cannot_gain_indirect_native_access(self):
        self.write("features/schematic/SchematicView.h", '#include "core/model.h"\n')
        self.write("features/schematic/SchematicView.cpp", '#include "features/schematic/SchematicView.h"\n')
        self.write("core/model.h", '#include "CKAll.h"\n')
        self.manifest["source_groups"].append({"id": "views", "patterns": ["features/**"]})
        self.manifest["features"][0]["source_groups"].append("views")
        self.manifest["dependencies"] = {"views": ["core"]}
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native/state access in workspace view: features/schematic/SchematicView.cpp", result.stderr)

    def test_migrated_canvas_inputs_cannot_query_or_write_native_state(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        for path, code in (
            ("ui/layout/LayoutInput.cpp", "void Move() { m_State.document->Changed(); }"),
            ("ui/layout/LayoutInput.cpp", "void Move() { m_View.Pick(1, 2); }"),
            ("ui/schematic/SchematicLinking.cpp", "void Link() { EditGroup edit; }"),
            ("ui/schematic/SchematicCanvas.cpp", "void SchematicCanvas::EndMove(bool commit) { m_State.document->Changed(); }")):
            with self.subTest(path=path, code=code):
                self.write(path, code)
                result = self.run_check()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("native mutation or query returned to migrated input", result.stderr)
                self.write(path, "// input uses copied query values\n")
        self.assertEqual(self.run_check().returncode, 0)

    def test_schematic_authoring_inputs_cannot_regain_native_access(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        for path, function in (
            ("ui/schematic/SchematicCanvas.cpp", "AddComment"),
            ("ui/schematic/SchematicCanvas.cpp", "OpenValueDialog"),
            ("ui/schematic/SchematicDialogs.cpp", "LocalParameterDialog"),
            ("ui/schematic/SchematicDialogs.cpp", "ParametersDialog"),
            ("ui/schematic/SchematicDialogs.cpp", "BlockMenus"),
            ("ui/schematic/SchematicMenus.cpp", "ParameterMenu"),
            ("ui/schematic/SchematicMenus.cpp", "ReplaceGraphDialog"),
            ("ui/schematic/SchematicMenus.cpp", "ImportBehaviorFile")):
            with self.subTest(path=path, function=function):
                self.write(path, "void SchematicCanvas::" + function + "() { m_State.document->Context(); }")
                result = self.run_check()
                self.assertIn("native mutation or query returned to migrated input", result.stderr)
                self.write(path, "// uses copied query values and commands\n")
        self.assertEqual(self.run_check().returncode, 0)

    def test_script_header_and_breakpoints_cannot_regain_native_access(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        for function in ("ScriptHeaderMenu", "SaveBehaviorsAs", "ScriptColorDialog", "SelectionBreakpoint", "BreakpointConfigDialog", "BreakpointListDialog", "ConditionParameterAt", "LoadSnapshotFor", "SetSnapshotPixels", "DropFile"):
            self.write("ui/schematic/SchematicMenus.cpp", "void SchematicCanvas::" + function + "() { m_State.document->Context(); }")
            self.assertIn("native mutation or query returned to migrated input", self.run_check().stderr)
        self.write("ui/schematic/SchematicMenus.cpp", "void SchematicCanvas::ToggleBreakpointOfSelection() { ToggleBreakpoint(doc); }")
        self.assertIn("native mutation or query returned to migrated input", self.run_check().stderr)
        self.write("ui/schematic/SchematicMenus.cpp", "// Queries and commands.\n")
        self.write("ui/schematic/SchematicInput.cpp", "void Input() { m_State.document->Reference(id); }")
        self.assertIn("native mutation or query returned to migrated input", self.run_check().stderr)
        self.write("ui/schematic/SchematicInput.cpp", "// Copied references.\n")
        self.assertEqual(self.run_check().returncode, 0)

    def test_breakpoint_data_cannot_import_native_implementation(self):
        self.write("features/schematic/SchematicScriptData.h", '#include "features/schematic/SchematicScriptCommands.h"\n')
        self.write("features/schematic/SchematicScriptCommands.h", "// Native registration.\n")
        self.manifest["source_groups"].append({"id": "features", "patterns": ["features/**"]})
        self.manifest["features"][0]["source_groups"].append("features")
        self.assertIn("native/state access in workspace view: features/schematic/SchematicScriptData.h", self.run_check().stderr)

    def test_layout_snapshot_and_fullscreen_inputs_cannot_regain_native_access(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        for path, text in (
            ("ui/layout/LayoutSnapshot.cpp", "void Take() { m_State.document->Context(); }"),
            ("ui/layout/LayoutSnapshot.cpp", "void Remove() { m_State.host.Context(); }"),
            ("ui/layout/LayoutFullScreen.cpp", "void Click() { m_View.PickInfo(0, 0); }"),
            ("ui/layout/LayoutWindow.cpp", "void LayoutWindow::ApplyPreferences() { m_State.preferences.SetBool(key, true); }"),
            ("ui/layout/LayoutWindow.cpp", "bool LayoutWindow::PlayerMode() const { return m_State.debugger->GetState(); }")):
            with self.subTest(path=path):
                self.write(path, text)
                result = self.run_check()
                self.assertIn("native mutation or query returned to", result.stderr)
                self.write(path, "// Query and command UI only.\n")
        self.assertEqual(self.run_check().returncode, 0)

    def test_schematic_picking_and_presentation_cannot_regain_native_access(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        for path, text in (
            ("ui/schematic/SchematicCanvas.cpp", "void Pick() { m_State.document->Context(); }"),
            ("ui/schematic/SchematicCanvas.cpp", "void Draw() { m_State.debugger->GetState(); }"),
            ("ui/schematic/SchematicCanvas.cpp", "void Pick() { state.objectPick[title] = callback; }"),
            ("ui/schematic/SchematicDialogs.cpp", "void Draw() { CKParameter *value; }")):
            with self.subTest(path=path, text=text):
                self.write(path, text)
                self.assertIn("native mutation or query returned to migrated input", self.run_check().stderr)
                self.write(path, "// Copied view data.\n")
        self.assertEqual(self.run_check().returncode, 0)

    def test_schematic_painter_and_icons_cannot_import_sdk(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        for path in ("ui/schematic/SchematicRenderer.cpp", "ui/controls/DevIcons.h", "ui/controls/DevIcons.cpp"):
            with self.subTest(path=path):
                self.write(path, '#include "CKAll.h"\n')
                self.assertIn("unrecorded native/state exposure: " + path, self.run_check().stderr)
                self.write(path, "// Copied drawing data.\n")
        self.assertEqual(self.run_check().returncode, 0)

    def test_parameter_dialog_and_array_control_cannot_import_native_state(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        self.write("core/model.h", '#include "CKAll.h"\n')
        for path in ("ui/schematic/SchematicParameterDialog.cpp", "ui/controls/ObjectArrayInput.cpp"):
            with self.subTest(path=path):
                self.write(path, '#include "core/model.h"\n')
                result = self.run_check()
                self.assertIn("unrecorded native/state exposure: " + path, result.stderr)
                self.write(path, "// pure copied fields\n")
        self.assertEqual(self.run_check().returncode, 0)

    def test_schematic_model_data_cannot_import_the_native_builder(self):
        self.write("core/schematic/SchematicModelBuilder.h", "// Native builder forward declarations.\n")
        self.write("features/schematic/SchematicModelData.cpp", '#include "core/schematic/SchematicModelBuilder.h"\n')
        self.manifest["source_groups"].append({"id": "views", "patterns": ["features/**"]})
        self.manifest["features"][0]["source_groups"].append("views")
        self.manifest["dependencies"] = {"views": ["core"]}
        result = self.run_check()
        self.assertIn("native/state access in workspace view: features/schematic/SchematicModelData.cpp", result.stderr)

    def test_schematic_navigation_and_search_cannot_regain_native_access(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        for function in ("RefreshStack", "ReadModel", "CapturePosition", "ValidateNavigation", "ApplyFocus", "Navigate", "FindNext", "SortScripts"):
            self.write("ui/schematic/SchematicCanvas.cpp", "void SchematicCanvas::" + function + "() { m_State.document->Context(); }")
            self.assertIn("native mutation or query returned to migrated input", self.run_check().stderr)
        self.write("ui/schematic/SchematicCanvas.cpp", "// Queries and commands.\n")
        self.write("ui/panels/EditorPanels.cpp", "void Explorer() { SearchDocument(document, query); }")
        self.assertIn("native mutation or query returned to migrated input", self.run_check().stderr)
        self.write("ui/panels/EditorPanels.cpp", "// Copied search results.\n")
        self.assertEqual(self.run_check().returncode, 0)

    def test_layout_projections_cannot_gain_native_or_mutable_state(self):
        for include in ('CKAll.h', 'core/document/Document.h', 'ui/app/EditorState.h'):
            with self.subTest(include=include):
                if '/' in include:
                    self.write(include, '#pragma once\n')
                self.write('core/model.h', '#include "' + include + '"\n')
                self.write('features/layout/LayoutSceneData.h', '#include "core/model.h"\n')
                self.write('features/layout/LayoutSceneData.cpp', '#include "features/layout/LayoutSceneData.h"\n')
                self.manifest['source_groups'] = [
                    {'id': 'core', 'patterns': ['core/**', 'ui/**']},
                    {'id': 'views', 'patterns': ['features/**']}]
                self.manifest['features'][0]['source_groups'] = ['core', 'views']
                self.manifest['dependencies'] = {'views': ['core']}
                result = self.run_check()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('native/state access in workspace view: features/layout/LayoutSceneData.cpp', result.stderr)

    def test_layout_drop_cannot_regain_native_queries_or_document_edits(self):
        self.add_ui("// Window include remains listed until its other fields migrate.\n")
        for body in ("m_State.document->Context();", "m_View.PickInfo(1, 2);",
                     "EditGroup edit(*m_State.document, label);", "m_State.objectSelection.Objects();",
                     "CKMaterial *material;", "ReplaceMeshMaterial(doc, mesh, from, to);"):
            self.write("ui/layout/LayoutDrop.cpp", "void Drop() { " + body + " }\n")
            result = self.run_check()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("native mutation or query returned to migrated input: ui/layout/LayoutDrop.cpp", result.stderr)

    def test_layout_creation_cannot_regain_native_queries(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        for body in ("m_State.document->Context();", "m_State.objectSelection.Objects();", "m_State.preferences.Bool(key);"):
            self.write("ui/app/EditorApp.cpp", "void EditorApp::CreateObject(int kind) { " + body + " }\n")
            self.assertIn("native mutation or query returned to application display: CreateObject", self.run_check().stderr)

    def test_native_canvas_commands_do_not_reintroduce_editor_state(self):
        self.write('ui/app/EditorState.h', '#pragma once\n')
        self.write('features/schematic/SchematicGraphCommands.cpp', '#include "ui/app/EditorState.h"\n')
        self.manifest['source_groups'] = [{'id': 'core', 'patterns': ['core/**', 'ui/**']},
                                          {'id': 'commands', 'patterns': ['features/**']}]
        self.manifest['features'][0]['source_groups'] = ['core', 'commands']
        self.manifest['dependencies'] = {'commands': ['core']}
        self.manifest['legacy_exposures'] = [{'file': 'ui/app/EditorState.h', 'access': ['editor-state'], 'remove_by': 5}]
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('feature depends on legacy EditorState: features/schematic/SchematicGraphCommands.cpp', result.stderr)

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

    def test_application_menu_status_and_title_use_projections(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        for method in ("CreateObject", "StatusBar", "MainMenu", "UpdateTitle"):
            with self.subTest(method=method):
                self.write("ui/app/EditorApp.cpp", f"void EditorApp::{method}() {{ m_State.document->Context(); }}")
                self.assertIn("native mutation or query returned to application display: " + method,
                              self.run_check().stderr)
        self.write("ui/app/EditorApp.cpp", "// Application assembly.\n")
        self.assertEqual(self.run_check().returncode, 0)

    def test_application_displays_reject_indirect_native_and_mutable_state(self):
        self.manifest["source_groups"].append({"id": "ui", "patterns": ["ui/**"]})
        self.manifest["features"][0]["source_groups"].append("ui")
        self.write("core/model.h", '#include "CKAll.h"\n')
        for path in ("ui/app/StatusBar.h", "ui/app/StatusBar.cpp", "ui/app/DuplicateNames.h", "ui/app/DuplicateNames.cpp"):
            with self.subTest(path=path):
                self.write(path, '#include "core/model.h"\n')
                self.assertIn("native/state access in application display: " + path, self.run_check().stderr)
                self.write(path, "// Copied application display data.\n")
        self.assertEqual(self.run_check().returncode, 0)

    def test_shared_test_headers_preserve_ui_and_production_boundaries(self):
        checker = CHECKER.with_name("CheckLayering.cmake")
        cases = [
            ("tests/Example.cpp", ["tests/ResourceFiles.h"], None),
            ("tests/ui/Example.cpp", ["tests/ResourceFiles.h", "tests/ui/UiTests.h",
                                      "imgui_te_context.h"], None),
            ("core/Example.cpp", ["tests/ResourceFiles.h"], "only the tests include"),
            ("ui/Example.cpp", ["tests/ResourceFiles.h"], "only the tests include"),
            ("tests/Example.cpp", ["tests/ui/UiTests.h"], "only the UI tests include"),
            ("tests/Example.cpp", ["imgui_te_context.h"], "only the UI tests include"),
        ]
        for index, (source, headers, error) in enumerate(cases):
            with self.subTest(source=source, headers=headers):
                root = self.folder / f"includes-{index}"
                for name in ["CMakeLists.txt", "core/CMakeLists.txt", "ui/CMakeLists.txt",
                             "tests/CMakeLists.txt", ".gitmodules"]:
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text("", encoding="utf-8")
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("".join(f'#include "{header}"\n' for header in headers),
                                encoding="utf-8")
                result = subprocess.run(["cmake", f"-DVIRTOOLSDEV_SOURCE_DIR={root}",
                                         "-P", str(checker)], capture_output=True, text=True)
                if error:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(error, result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
