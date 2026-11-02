"""Check source ownership, dependency growth and architecture test coverage."""
import argparse
import fnmatch
import io
import json
from pathlib import Path
import posixpath
import re
import subprocess
import sys
import tarfile


CODE_SUFFIXES = {".h", ".hpp", ".c", ".cpp", ".cxx", ".mm", ".def"}
RESTRICTED = {"ui/app/EditorState.h": "editor-state",
              "ui/native/EditorSession.h": "editing-session",
              "core/document/Document.h": "mutable-document",
              "core/document/ScriptSession.h": "mutable-document",
              "core/schematic/SchematicModelBuilder.h": "native-sdk",
              "features/schematic/SchematicScriptCommands.h": "native-sdk"}
# These headers define values, constants and arithmetic, never CK object or
# Context implementations. Setup translation units also compile with a fence
# that rejects complete native object/Context types.
VALUE_HEADERS = {"CKDefines.h", "CKEnums.h", "VxDefines.h", "VxVector.h", "Vx2dVector.h",
                 "VxMatrix.h", "VxBbox.h", "VxColor.h", "VxRect.h"}
HOST_FILES = {"ui/app/EditorApp.h", "ui/app/EditorApp.cpp", "ui/app/Main.cpp", "ui/app/EditorStartup.cpp",
              "ui/native/EditorSession.h", "ui/native/EditorSession.cpp"}


def source_files(source):
    result = subprocess.run(["git", "-C", str(source), "ls-files", "--cached", "--others",
                             "--exclude-standard", "-z"], check=True, capture_output=True)
    paths = set(result.stdout.decode("utf-8").rstrip("\0").split("\0")) - {""}
    return sorted(path for path in paths if (source / path).exists())


def matches(path, patterns):
    return (any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns if not pattern.startswith("!"))
            and not any(fnmatch.fnmatchcase(path, pattern[1:]) for pattern in patterns if pattern.startswith("!")))


def include_graph(source, files, contents=None, all_access=False):
    code = {path for path in files if Path(path).suffix in CODE_SUFFIXES}
    graph, access = {}, {}
    for path in sorted(code):
        text = contents[path] if contents is not None else (source / path).read_text(encoding="utf-8-sig")
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        dependencies, capabilities = set(), set()
        if path in RESTRICTED:
            capabilities.add(RESTRICTED[path])
        for name in re.findall(r'^\s*#\s*(?:include|import)\s*[<"]([^>"\n]+)[>"]', text, flags=re.M):
            if re.match(r"^(CK|Vx)[^/]*\.h$", name) and name not in VALUE_HEADERS:
                capabilities.add("native-sdk")
            relative = posixpath.normpath(posixpath.join(posixpath.dirname(path), name))
            if relative in code:
                dependencies.add(relative)
            elif name in code:
                dependencies.add(name)
        graph[path], access[path] = dependencies, capabilities
    changed = True
    while changed:
        changed = False
        for path, dependencies in graph.items():
            before = len(access[path])
            for dependency in dependencies:
                access[path].update(access[dependency])
            changed |= len(access[path]) != before
    return graph, {path: sorted(values) for path, values in access.items()
                   if (all_access or path.startswith("ui/")) and values}



def input_access(source, files, errors):
    forbidden = re.compile(r"\bm_State\.(?:document|objectSelection|host|debugger|catalog|recentBlocks|clipboard)\b|\b(?:CKContext|CKObject|CKBehavior|CKParameter(?:In|Out|Local|Operation|Manager)?|ScriptSession|CK(?:BeObject|3dEntity|2dEntity|Material|Mesh|ObjectDeclaration)|DropBuildingBlockOn|DropBuildingBlockInto|ReplaceMeshMaterial|EditScriptData|EditGroup|SearchDocument|ToggleBreakpoint|FindBreakpoint|SetBreakpointConditions|RemoveBreakpoints|SetScriptSnapshot|LoadScriptSnapshot|LoadSnapshotBitmap|objectPick)\b|"
                           r"\bm_View\.(?:Context|Pick|PickInfo|RectPick|Camera|Scene)\s*\(")
    migrated = {"ui/app/StatusBar.cpp", "ui/app/DuplicateNames.cpp", "ui/layout/LayoutDrop.cpp", "ui/layout/LayoutInput.cpp", "ui/layout/LayoutFullScreen.cpp", "ui/layout/LayoutSnapshot.cpp", "ui/schematic/SchematicLinking.cpp",
                "ui/schematic/SchematicCanvas.cpp", "ui/schematic/SchematicMenus.cpp",
                "ui/schematic/SchematicDialogs.cpp", "ui/schematic/SchematicInput.cpp",
                "ui/panels/EditorPanels.cpp", "ui/setup/SetupRenderPages.cpp", "ui/setup/SetupScenePages.cpp", "ui/setup/SetupObjectPages.cpp", "ui/setup/SetupWindows.cpp"}
    for path in set(files) & migrated:
        text = (source / path).read_text(encoding="utf-8-sig")
        text = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', '', text, flags=re.S)
        if forbidden.search(text):
            errors.append(f"native mutation or query returned to migrated input: {path}")

    if "ui/app/EditorApp.cpp" in files:
        text = (source / "ui/app/EditorApp.cpp").read_text(encoding="utf-8-sig")
        if re.search(r"\bm_Setups\.Draw\s*\(", text):
            errors.append("Setup drawing returned to EditorApp")
        if re.search(r"\bm_Layout\.Draw\s*\(", text):
            errors.append("Layout drawing returned to EditorApp")
        if re.search(r"\bEditorApp::(?:MessageDialog|ShowMessage)\s*\(", text):
            errors.append("application message queue or drawing returned to EditorApp")

    # These methods assemble UI from copied Layout state. The window's private
    # GPU target management is intentionally outside this list.
    methods = (
        ("ui/layout/LayoutWindows.cpp", "LayoutWindows", ("Register",)),
        ("ui/layout/LayoutWindow.cpp", "LayoutWindow", ("SaveWindow", "RestoreWindow", "PlayerMode", "FullScreen", "ApplyPreferences", "SyncComposition", "LeftBar", "Command",
             "TogglePreference", "ToggleGuide", "RightClick", "DebugData", "SelectionText", "Steps", "TransformStepsOf", "Snap2DOf")),
        ("ui/setup/SetupWindows.cpp", "SetupWindows", ("Client", "Register", "ApplyPreferences", "Open", "Show",
            "ChangeTarget", "OpenSetups", "Close", "CloseAll", "Draw", "SetupPick", "PortalPick", "DrawSetup", "PrepareFrame", "Save", "Restore", "Instances", "Navigate", "Header", "Sidebar", "Picture", "DrawObjectPage", "ReplaceFile", "ReadDetails", "ReadProperties", "PreviewProperty", "PreviewIndexedProperty", "SetProperty", "EditObjectList")),
        ("ui/setup/SetupObjectPages.cpp", "SetupWindows", ("AnimationPage", "ObjectAnimationPage", "MidiSoundPage", "SpriteTextPage", "KinematicChainPage", "LayerPage", "ParameterPage", "ParameterInPage", "OperationPage", "BehaviorPage", "BehaviorIOPage", "BehaviorLinkPage", "SynchroPage", "StatePage", "CriticalSectionPage", "InterfaceObjectsPage", "ParameterUsers", "ParameterValue", "ShowInSchematic", "OtherPage")),
        ("ui/app/EditorApp.cpp", "EditorApp", ("CreateObject", "StatusBar", "MainMenu", "UpdateTitle")),
        ("ui/layout/LayoutMenu.cpp", "LayoutWindow", ("CreatedObject",)))
    for path, owner, names in methods:
        if path not in files:
            continue
        text = (source / path).read_text(encoding="utf-8-sig")
        text = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', '', text, flags=re.S)
        for match in re.finditer(owner + r"::(" + "|".join(names) + r")\([^)]*\)[^{]*\{", text):
            begin = match.end(); end = begin; depth = 1
            while end < len(text) and depth:
                depth += (text[end] == '{') - (text[end] == '}'); end += 1
            body = text[begin:end]
            if forbidden.search(body) or re.search(r"\bm_State\.(?:preferences|workspace)\b", body):
                area = {"SetupWindows": "Setup window management", "EditorApp": "application display"}.get(owner, "Layout presentation")
                errors.append(f"native mutation or query returned to {area}: {match.group(1)}")



def baseline_exposures(source, revision):
    if not re.fullmatch(r"[0-9a-fA-F]{7,64}", revision):
        raise ValueError("baseline_revision must be a fixed Git commit")
    roots = subprocess.check_output(["git", "-C", str(source), "ls-tree", "--name-only", revision,
                                     "--", "core", "ui", "html"], text=True).splitlines()
    if not roots:
        raise ValueError("baseline commit has no editor source")
    archive = subprocess.check_output(["git", "-C", str(source), "archive", revision, "--", *roots])
    with tarfile.open(fileobj=io.BytesIO(archive)) as entries:
        contents = {entry.name: entries.extractfile(entry).read().decode("utf-8-sig")
                    for entry in entries if entry.isfile() and Path(entry.name).suffix in CODE_SUFFIXES}
    return include_graph(source, contents, contents)[1]


def indexed(items, kind, errors):
    result = {}
    for item in items:
        identity = item.get("id", "")
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", identity) or identity in result:
            errors.append(f"invalid or duplicate {kind}: {identity!r}")
        result[identity] = item
    return result


def check(source, manifest, inventory=None, ui_tests=False):
    errors = []
    if manifest.get("version") != 1:
        raise ValueError("unsupported architecture manifest version")
    phase = manifest.get("completed_phase")
    functional_phase = manifest.get("functional_phase", phase)
    if not isinstance(phase, int) or not 0 <= phase <= 6:
        raise ValueError("completed_phase must be between 0 and 6")
    if not isinstance(functional_phase, int) or not phase <= functional_phase <= 6:
        raise ValueError("functional_phase must be between completed_phase and 6")
    groups = indexed(manifest["source_groups"], "source group", errors)
    external = set(manifest["external_submodules"])
    files = [path for path in source_files(source) if path not in external]
    owners = {}
    for path in files:
        found = [identity for identity, group in groups.items() if matches(path, group["patterns"])]
        if not found:
            errors.append(f"unowned file: {path}")
        elif len(found) != 1:
            errors.append(f"multiple owners for {path}: {', '.join(found)}")
        else:
            owners[path] = found[0]
    for identity in groups:
        if identity not in owners.values():
            errors.append(f"empty source group: {identity}")

    input_access(source, files, errors)
    graph, all_exposures = include_graph(source, files, all_access=True)
    exposures = {path: access for path, access in all_exposures.items() if path.startswith("ui/")}
    for path, access in all_exposures.items():
        if path.startswith("examples/"):
            errors.append(f"native/state access in example module: {path}: {', '.join(access)}")
        if path.startswith("features/") and ({"editor-state", "editing-session"} & set(access)):
            errors.append(f"feature depends on legacy EditorState: {path}")
        if path.startswith(("workspace/", "features/scene/", "features/resources/", "features/runtime/", "features/environment/", "features/diagnostics/", "features/preferences/", "features/defaults/", "features/values/", "features/arrays/", "features/setup/", "features/schematic/", "features/layout/")) and not re.match(
                r"features/(?:scene/Scene(?:Commands|Queries)|resources/(?:BuildingBlockQueries|DataResourceCommands)|environment/EnvironmentCommands|diagnostics/DiagnosticCommands|preferences/PreferencesCommands|values/ParameterCommands|arrays/ArrayCommands|setup/Setup(?:Commands|Queries|ObjectQueries|SceneQueries|RenderQueries|Actions)|schematic/Schematic(?:Commands|GraphCommands|Geometry|Connections|Copying|Authoring|ParameterEdits|ModelCommands|ScriptCommands)|layout/(?:Layout(?:Commands|Gestures|Presentation|Authoring)|SnapshotCommands))\.(?:h|cpp)$", path):
            errors.append(f"native/state access in workspace view: {path}: {', '.join(access)}")
    for path in ("ui/app/StatusBar.h", "ui/app/StatusBar.cpp", "ui/app/DuplicateNames.h", "ui/app/DuplicateNames.cpp",
                 "ui/app/MessageDialogs.h", "ui/app/MessageDialogs.cpp"):
        if all_exposures.get(path):
            errors.append(f"native/state access in application display: {path}")
    observed = set()
    for path, dependencies in graph.items():
        for dependency in dependencies:
            if path.startswith("examples/") and not dependency.startswith(("examples/", "editor/")):
                errors.append(f"example module includes private header: {path}: {dependency}")
            if path in owners and dependency in owners and owners[path] != owners[dependency]:
                observed.add((owners[path], owners[dependency]))
    permitted = {(origin, target) for origin, targets in manifest["dependencies"].items() for target in targets}
    for origin, target in sorted(observed - permitted):
        errors.append(f"new dependency: {origin} -> {target}")
    for origin, target in sorted(permitted - observed):
        errors.append(f"stale dependency exception: {origin} -> {target}")
    legacy = {}
    baseline = baseline_exposures(source, manifest["baseline_revision"]) if "baseline_revision" in manifest else None
    for entry in manifest["legacy_exposures"]:
        path = entry["file"]
        if path in legacy or not 1 <= entry["remove_by"] <= 5:
            errors.append(f"invalid legacy exposure: {path}")
        legacy[path] = sorted(entry["access"])
        if baseline is not None and not set(entry["access"]).issubset(baseline.get(path, [])):
            errors.append(f"legacy exposure exceeds fixed baseline: {path}")
        if entry["remove_by"] <= functional_phase:
            errors.append(f"expired legacy exposure: {path}")
        if functional_phase >= 5:
            errors.append(f"legacy UI exposure after phase five: {path}")
    hosts = {}
    for entry in manifest.get("native_hosts", []):
        path = entry["file"]
        access = sorted(entry["access"])
        if (path not in HOST_FILES or path in hosts or path in legacy or not entry.get("purpose") or
                not set(access) <= {"editing-session", "mutable-document", "native-sdk"}):
            errors.append(f"invalid native host: {path}")
        hosts[path] = access
    value_imports = {}
    for path in files:
        if path.startswith("ui/") and Path(path).suffix in CODE_SUFFIXES:
            text = (source / path).read_text(encoding="utf-8-sig")
            names = set(re.findall(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', text, flags=re.M)) & VALUE_HEADERS
            if names:
                value_imports[path] = sorted(names)
    declared_values = {}
    for entry in manifest.get("value_headers", []):
        path, headers = entry["file"], entry["headers"]
        if path in declared_values or not headers or len(set(headers)) != len(headers) or not set(headers) <= VALUE_HEADERS:
            errors.append(f"invalid value headers: {path}")
        declared_values[path] = sorted(headers)
    if declared_values != value_imports:
        for path in sorted(declared_values.keys() | value_imports.keys()):
            if declared_values.get(path) != value_imports.get(path): errors.append(f"unrecorded value headers: {path}")
    adapters = {}
    for entry in manifest.get("native_adapters", []):
        path = entry["file"]
        access = sorted(entry["access"])
        if (not path.startswith("ui/native/") or path in adapters or path in legacy or not entry.get("purpose") or
                not set(access) <= {"mutable-document", "native-sdk"}):
            errors.append(f"invalid native adapter: {path}")
        adapters[path] = access
        if path in files:
            body = (source / path).read_text(encoding="utf-8-sig")
            body = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', '', body, flags=re.S)
            if re.search(r"\bImGui\s*::", body): errors.append(f"drawing in native adapter: {path}")
    for path, dependencies in graph.items():
        if path.startswith(("ui/", "features/", "workspace/")) and path not in adapters and path not in hosts:
            for dependency in dependencies:
                if dependency in adapters or dependency in hosts:
                    errors.append(f"private native adapter included outside its implementation: {path} -> {dependency}")
    for path, access in exposures.items():
        expected = hosts.get(path) if path in hosts else adapters.get(path) if path in adapters else legacy.get(path)
        if access != expected:
            errors.append(f"unrecorded native/state exposure: {path}: {', '.join(access)}")
    for path in legacy.keys() - exposures.keys():
        errors.append(f"stale legacy exposure: {path}")
    for path in adapters.keys() - exposures.keys():
        errors.append(f"stale native adapter: {path}")
    for path in hosts.keys() - exposures.keys():
        errors.append(f"stale native host: {path}")

    features = indexed(manifest["features"], "feature", errors)
    covered = set()
    available = {test["name"] for test in inventory["tests"]} if inventory is not None else None
    if "required_core_cases" in manifest:
        declared = set(re.findall(r"VIRTOOLSDEV_CASE\((\w+)\)",
                                   (source / "tests/VirtoolsDevCases.def").read_text(encoding="utf-8")))
        for case in manifest["required_core_cases"]:
            if case not in declared or (available is not None and "VirtoolsDev." + case not in available):
                errors.append(f"missing baseline core case: {case}")
    for identity, feature in features.items():
        if not feature.get("name") or not feature.get("owner", "").strip():
            errors.append(f"feature has no name/owner: {identity}")
        covered.update(feature["source_groups"])
        for group in feature["source_groups"]:
            if group not in groups:
                errors.append(f"unknown source group: {identity}: {group}")
        anchors = feature["tests"] + (feature["ui_tests"] if ui_tests else [])
        if not feature["tests"] and not feature["ui_tests"] and not feature["gaps"]:
            errors.append(f"feature has no tests or declared gap: {identity}")
        for gap in feature["gaps"]:
            if not gap.get("reason", "").strip() or not isinstance(gap.get("due_phase"), int):
                errors.append(f"invalid verification gap: {identity}")
            elif gap["due_phase"] <= functional_phase:
                errors.append(f"overdue verification gap: {identity}: {gap['reason']}")
        if available is not None:
            for anchor in anchors:
                if anchor not in available:
                    errors.append(f"missing registered test: {identity}: {anchor}")
    for group in groups.keys() - covered:
        errors.append(f"source group has no feature owner: {group}")
    separate_work = indexed(manifest.get("separate_work", []), "separate work", errors)
    for identity, item in separate_work.items():
        if item.get("feature") not in features or not item.get("reason", "").strip() or not item.get("name", "").strip():
            errors.append(f"invalid separate work: {identity}")
    states = indexed(manifest["states"], "state", errors)
    for identity, state in states.items():
        if not state.get("owner", "").strip() or not state.get("lifetime", "").strip():
            errors.append(f"state has no owner/lifetime: {identity}")
        if not state.get("features"):
            errors.append(f"state has no feature: {identity}")
        for feature in state.get("features", []):
            if feature not in features:
                errors.append(f"unknown feature for state: {identity}: {feature}")
    if not groups or not features or not states:
        errors.append("source groups, features and states must not be empty")
    if errors:
        raise ValueError("\n".join(errors))
    return {"files": len(files), "groups": len(groups), "features": len(features), "states": len(states),
            "legacy_exposures": len(legacy), "native_hosts": len(hosts), "native_adapters": len(adapters),
            "value_header_users": len(value_imports), "completed_phase": phase, "functional_phase": functional_phase,
            "test_registration_checked": inventory is not None,
            "open_gaps": sum(len(feature["gaps"]) for feature in features.values()),
            "separate_work": len(separate_work)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--inventory", type=Path)
    parser.add_argument("--build", type=Path)
    parser.add_argument("--config", default="")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--ui-tests", action="store_true")
    args = parser.parse_args()
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        inventory = json.loads(args.inventory.read_text(encoding="utf-8")) if args.inventory else None
        if args.build:
            command = [args.ctest, "--test-dir", str(args.build), "--show-only=json-v1"]
            if args.config:
                command += ["-C", args.config]
            inventory = json.loads(subprocess.check_output(command, text=True))
        result = check(args.source.resolve(), manifest, inventory, args.ui_tests)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        print(f"Architecture check failed:\n{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
