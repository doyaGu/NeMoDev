"""Distribution symlinks must remain usable after moving the package."""

from pathlib import Path
import os
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from Package import check_symlinks, move_mac_resources


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

    def test_mac_resources_keep_sdk_lookup_paths_after_relocation(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / "NeMoDev.app"
            contents = app / "Contents"
            for name in ("Frameworks/BallancedRuntime.txt", "PlugIns/RenderEngines/CK2_3D.ini"):
                path = contents / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"Original SDK data\n")
            library = contents / "Frameworks/libCK2.dylib"
            library.write_bytes(b"Library unchanged")
            move_mac_resources(contents)
            app.rename(Path(directory) / "Moved App.app")
            moved = Path(directory) / "Moved App.app"
            check_symlinks(moved)
            for name in ("Frameworks/BallancedRuntime.txt", "PlugIns/RenderEngines/CK2_3D.ini"):
                path = moved / "Contents" / name
                self.assertTrue(path.is_symlink())
                self.assertFalse(Path(os.readlink(path)).is_absolute())
                self.assertEqual(path.read_bytes(), b"Original SDK data\n")
                self.assertEqual(path.resolve(), moved / "Contents/Resources/runtime" / name)
            self.assertEqual((moved / "Contents/Frameworks/libCK2.dylib").read_bytes(), b"Library unchanged")


if __name__ == "__main__":
    unittest.main()
