"""The portable launcher retains arguments, working directory and exit status."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class PortableLauncher(unittest.TestCase):
    def test_launcher_after_relocation_and_through_a_symlink(self):
        with tempfile.TemporaryDirectory(prefix="NeMoDev launcher ") as directory:
            root = Path(directory)
            original = root / "original"
            (original / "Bin").mkdir(parents=True)
            launcher = Path(__file__).resolve().parents[1] / "tools/packaging/NeMoDev.sh"
            (original / "NeMoDev").write_text(launcher.read_text())
            (original / "NeMoDev").chmod(0o755)
            binary = original / "Bin/NeMoDev"
            binary.write_text('#!/bin/sh\nprintf "%s\\0" "$PWD" "$@" > "$RESULT"\nexit 23\n')
            binary.chmod(0o755)
            moved = root / "Moved Package [test]'s"
            original.rename(moved)
            alias = root / "entry point"
            alias.symlink_to(moved / "NeMoDev")
            caller = root / "unrelated directory"
            caller.mkdir()
            output = root / "arguments"
            arguments = ["a composition.cmo", "semi;colon", "quote'\"", "$HOME", ""]
            for entry in (moved / "NeMoDev", alias):
                result = subprocess.run([str(entry), *arguments], cwd=caller,
                                        env={**os.environ, "RESULT": str(output)}, timeout=10)
                self.assertEqual(result.returncode, 23)
                self.assertEqual(output.read_bytes().split(b"\0"),
                                 [str(caller).encode(), *(arg.encode() for arg in arguments), b""])


if __name__ == "__main__":
    unittest.main()
