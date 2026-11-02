"""Restore saved module/provider choices in separate real editor processes."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "module", "providers", "replacement", "results"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    args.results.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nemodev-profile-startup-") as temporary:
        root = Path(temporary).resolve() / "中文 profile"
        root.mkdir()
        profile = root / "settings.json"
        module = root / args.module.name
        providers = root / args.providers.name
        replacement = root / args.replacement.name
        for source, target in ((args.module, module), (args.providers, providers), (args.replacement, replacement)):
            shutil.copy2(source, target)

        def save(data):
            profile.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")

        def run(name, *, expected=None, options=(), failure=None):
            report = args.results / (name + ".xml")
            report.unlink(missing_ok=True)
            env = dict(os.environ)
            for key in list(env):
                if key.startswith("NEMODEV_PROFILE_"):
                    env.pop(key)
            env["NEMODEV_PROFILE_FILE"] = str(profile)
            env.update({"NEMODEV_PROFILE_" + key.upper(): value for key, value in (expected or {}).items()})
            command = [str(args.binary), "--editor-profile", str(profile), "--ui-tests", "profile_startup_state",
                       "--ui-results", str(report), *map(str, options)]
            before = profile.read_bytes() if profile.exists() else None
            result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
            (args.results / (name + ".log")).write_bytes(result.stdout)
            output = result.stdout.decode(errors="replace")
            if failure is not None:
                if result.returncode != 1 or failure not in output or profile.read_bytes() != before:
                    raise RuntimeError(f"{name}: failed startup did not preserve the profile ({result.returncode})\n" + output)
                return
            if result.returncode != 0 or not report.exists():
                raise RuntimeError(f"{name}: startup failed ({result.returncode})\n" + output)
            cases = [case for case in ET.parse(report).getroot().iter("testcase") if case.find("skipped") is None]
            if len(cases) != 1 or cases[0].get("status") != "Success":
                raise RuntimeError(f"{name}: startup state assertions failed\n" + output)

        # First process changes a provider and a pending module toggle through
        # the ordinary workspace/command entry points. Only the first process
        # receives module discovery arguments.
        save({"version": 1, "future": {"keep": 1234567890123456789},
              "modules": [{"id": "help.reference", "enabled": False},
                          {"id": "example.document-info", "enabled": True}],
              "bindings": [{"action": "help.open", "chord": "Ctrl+Shift+F1", "module": "help.reference"}]})
        run("publication-failure", expected={"write_failure": "yes", "help": "disabled"},
            options=("--editor-module-dir", root, "--editor-module", providers))
        run("save", expected={"save": "yes", "help": "disabled"})
        saved = json.loads(profile.read_text(encoding="utf-8"))
        assert saved["future"]["keep"] == 1234567890123456789
        assert {p["kind"] for p in saved["providers"]} == {"view", "command", "query", "editor"}
        assert saved["bindings"][0]["chord"] == "Ctrl+Shift+F1"
        run("restore", expected={"layout": "external", "help": "disabled", "example": "disabled"})

        # Disabled, now broken libraries must be skipped before their entry.
        module.write_bytes(b"disabled broken module")
        run("disabled-broken", expected={"layout": "external", "help": "disabled", "example": "disabled"})
        module.unlink()
        run("disabled-missing", expected={"layout": "external", "help": "disabled", "example": "disabled"})

        # Disabling a provider's owning module suspends all four selections,
        # retains them on disk, and restores them when the module returns.
        suspended = json.loads(profile.read_text(encoding="utf-8"))
        suspended["modules"].append({"id": "example.objects", "enabled": False, "path": providers.name})
        save(suspended)
        run("suspend-providers", expected={"help": "disabled", "example": "disabled"})
        resumed = json.loads(profile.read_text(encoding="utf-8"))
        assert {p["kind"] for p in resumed["providers"]} == {"view", "command", "query", "editor"}
        next(m for m in resumed["modules"] if m["id"] == "example.objects")["enabled"] = True
        save(resumed)
        run("resume-providers", expected={"layout": "external", "help": "disabled", "example": "disabled"})

        # Replacing a linked module uses its actual logical module ID. Forcing
        # the builtin implementation ignores the same discovered replacement.
        save({"version": 1, "modules": [{"id": "help.reference", "enabled": True, "path": replacement.name}]})
        run("replace-builtin", expected={"help": "replacement"})
        save({"version": 1, "module-files": [replacement.name],
              "modules": [{"id": "help.reference", "enabled": True, "source": "builtin"}],
              "bindings": [{"action": "help.open", "chord": "Ctrl+Shift+F1", "module": "help.reference"},
                           {"action": "help.open", "chord": "Ctrl+Alt+Shift+F1", "module": "help.reference"}],
              "menus": [{"id": "help.reference", "location": "help.reference", "order": 567, "module": "help.reference"}]})
        run("restore-builtin", expected={"help": "builtin", "bindings": "yes"})
        save({"version": 1, "modules": [{"id": "layout.window", "enabled": False},
                                       {"id": "schematic.views", "enabled": False},
                                       {"id": "runtime.views", "enabled": False}]})
        run("disable-default-windows", expected={"layout": "disabled", "schematic": "disabled"}, options=("--new",))

        for name, data, message in (
            ("future-version", {"version": 2}, "Unsupported profile version"),
            ("missing-module", {"version": 1, "modules": [{"id": "missing.feature", "enabled": True}]}, "Configured module is missing"),
            ("wrong-module", {"version": 1, "modules": [{"id": "wrong.feature", "enabled": True, "path": replacement.name}]}, "different identifier"),
            ("missing-provider", {"version": 1, "providers": [{"kind": "view", "id": "layout.viewport", "provider": "missing.provider"}]}, "Unknown provider selection"),
        ):
            save(data)
            run(name, failure=message)
        loop = root / "loop"
        try:
            loop.symlink_to(loop.name)
        except OSError:
            pass  # Symlink creation is not available to every Windows runner.
        else:
            save({"version": 1, "module-files": [loop.name]})
            run("invalid-module-path", failure="Editor module paths:")
        profile.write_bytes(b'{"version":1,"version":1}')
        run("duplicate-json", failure="Duplicate JSON member")
        profile.write_bytes(b'{')
        run("truncated-json", failure="parse error")
    print("Saved module/provider choices restored across startup; failed startups preserved original JSON")


if __name__ == "__main__":
    main()
