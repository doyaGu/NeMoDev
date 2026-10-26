"""Distribution symlinks must remain usable after moving the package."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from Package import check_symlinks


class PackageLinks(unittest.TestCase):
    def test_relative_library_chain_and_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory)
            (package / "lib.so.1.0").touch()
            (package / "lib.so.1").symlink_to("lib.so.1.0")
            (package / "lib.so").symlink_to("lib.so.1")
            (package / "data").mkdir()
            (package / "shared-data").symlink_to("data", target_is_directory=True)
            check_symlinks(package)

    def test_developer_file_and_directory_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "installed"
            package.mkdir()
            (root / "documentation.chm").touch()
            link = package / "documentation.chm"
            link.symlink_to(root / "documentation.chm")
            with self.assertRaises(RuntimeError):
                check_symlinks(package)
            link.unlink()
            link.symlink_to(root, target_is_directory=True)
            with self.assertRaises(RuntimeError):
                check_symlinks(package)

    def test_missing_and_cyclic_targets_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory)
            link = package / "lib.so"
            link.symlink_to("absent.so")
            with self.assertRaises(RuntimeError):
                check_symlinks(package)
            link.unlink()
            link.symlink_to("lib.so")
            with self.assertRaises(RuntimeError):
                check_symlinks(package)


if __name__ == "__main__":
    unittest.main()
