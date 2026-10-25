"""Reject regression drivers and ImGui Test Engine in the shipped editor."""
import argparse
from pathlib import Path
import subprocess


ENGINE_MARKERS = (
    "ImGuiTestEngine", "ImGuiTestContext", "ImGui Test Engine", "imgui_te_",
)
MARKERS = ENGINE_MARKERS + (
    "--ui-tests", "--ui-results", "--capture", "--insert-block", "--drop-block",
    "--edit-value", "--layout-command", "Edit Parameter###StartupValue",
    "Last undo step:", "page(s) written to ",
    "TopologyFingerprint", "ShownLevelRows", "ShownHierarchyRows",
    "ShownBehaviors", "ShownPins", "ShownScripts",
    "UiRenderer9ReadFrame",
    "HelpWindow12CurrentTopic", "HelpWindow9ShownText", "HelpWindow11ShownImages",
    "HelpWindow10PageLayout",
    "HelpWindow9PagePaint", "HelpWindow12PageDocument",
    "HtmlRenderer8Drawings",
)


def regression_markers(path):
    data = path.read_bytes()
    return [marker for marker in MARKERS if any(
        marker.encode(encoding) in data for encoding in ("utf-8", "utf-16-le", "utf-32-le"))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--test-binary", type=Path)
    parser.add_argument("--package", type=Path)
    args = parser.parse_args()
    found = regression_markers(args.binary)
    if found:
        raise RuntimeError(f"Regression code in {args.binary}: {', '.join(found)}")
    for option in ("--capture", "--ui-tests", "--tool", "--play"):
        result = subprocess.run([str(args.binary.resolve()), option, "argument"],
                                capture_output=True, timeout=10)
        if result.returncode != 2 or b"Invalid option:" not in result.stderr:
            raise RuntimeError(f"Production accepted the regression option {option}")
    if args.test_binary:
        control = regression_markers(args.test_binary)
        if not any(marker in control for marker in ENGINE_MARKERS) or "--ui-tests" not in control:
            raise RuntimeError("The negative control did not detect Test Engine and regression drivers")
    if args.package:
        if not args.package.is_dir():
            raise RuntimeError(f"Package directory not found: {args.package}")
        forbidden = ("VirtoolsDevTests", "VirtoolsDevUiTests", "VirtoolsDevPortableTests",
                     "VirtoolsDevFileDialogTests", "NeMoDevCommandLineTests", "NeMoDevHelpTests", "NeMoDevHtmlTests", "SchematicModelTests",
                     "ImGuiTestEngine", "imgui_test_engine", "imgui_te_",
                     "test-player", "test-help", "test-desktop", "test-windows-desktop", "virtoolsdev-ui", "local-samples")
        for path in args.package.rglob("*"):
            # Keep the existing third-party notice; it contains no engine code.
            if path == args.package / "licenses" / "ImGuiTestEngine-License.txt":
                continue
            if path.name.lower() == "tests" or any(name.lower() in path.name.lower() for name in forbidden):
                raise RuntimeError(f"Regression material in the package: {path}")
    print("Production editor contains no regression driver or ImGui Test Engine markers")


if __name__ == "__main__":
    main()
