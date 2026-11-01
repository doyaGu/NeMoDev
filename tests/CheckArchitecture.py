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
              "core/document/Document.h": "mutable-document",
              "core/document/ScriptSession.h": "mutable-document"}


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
            if re.match(r"^(CK|Vx)[^/]*\.h$", name):
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
    if not isinstance(phase, int) or not 0 <= phase <= 6:
        raise ValueError("completed_phase must be between 0 and 6")
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

    graph, all_exposures = include_graph(source, files, all_access=True)
    exposures = {path: access for path, access in all_exposures.items() if path.startswith("ui/")}
    for path, access in all_exposures.items():
        if path.startswith("features/") and "editor-state" in access:
            errors.append(f"feature depends on legacy EditorState: {path}")
        if path.startswith(("workspace/", "features/scene/", "features/resources/", "features/runtime/", "features/environment/", "features/diagnostics/", "features/preferences/", "features/defaults/", "features/values/", "features/arrays/", "features/setup/", "features/schematic/", "features/layout/")) and not re.match(
                r"features/(?:scene/Scene(?:Commands|Queries)|resources/(?:BuildingBlockQueries|DataResourceCommands)|environment/EnvironmentCommands|diagnostics/DiagnosticCommands|preferences/PreferencesCommands|values/ParameterCommands|arrays/ArrayCommands|setup/SetupCommands|schematic/Schematic(?:Commands|GraphCommands|Geometry|Connections|Copying)|layout/LayoutCommands)\.(?:h|cpp)$", path):
            errors.append(f"native/state access in workspace view: {path}: {', '.join(access)}")
    observed = set()
    for path, dependencies in graph.items():
        for dependency in dependencies:
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
        if entry["remove_by"] <= phase:
            errors.append(f"expired legacy exposure: {path}")
    for path, access in exposures.items():
        if access != legacy.get(path):
            errors.append(f"unrecorded native/state exposure: {path}: {', '.join(access)}")
    for path in legacy.keys() - exposures.keys():
        errors.append(f"stale legacy exposure: {path}")

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
            elif gap["due_phase"] <= phase:
                errors.append(f"overdue verification gap: {identity}: {gap['reason']}")
        if available is not None:
            for anchor in anchors:
                if anchor not in available:
                    errors.append(f"missing registered test: {identity}: {anchor}")
    for group in groups.keys() - covered:
        errors.append(f"source group has no feature owner: {group}")
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
            "legacy_exposures": len(exposures), "completed_phase": phase,
            "test_registration_checked": inventory is not None,
            "open_gaps": sum(len(feature["gaps"]) for feature in features.values())}


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
