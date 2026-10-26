"""Distribution symlinks must remain usable after moving the package."""

from pathlib import Path
import os
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from Package import check_symlinks, linux_symbol_versions, move_mac_resources


class LinuxSymbolVersions(unittest.TestCase):
    def test_required_versions_are_numeric_and_include_cpp_abi(self):
        versions = linux_symbol_versions("""
  0x0010: Name: GLIBC_2.9  Flags: none  Version: 2
  0x0020: Name: GLIBC_2.35 Flags: none  Version: 3
  0x0030: Name: GLIBCXX_3.4.9 Flags: none Version: 4
  0x0040: Name: GLIBCXX_3.4.30 Flags: none Version: 5
  0x0050: Name: CXXABI_1.3.13 Flags: none Version: 6
  0x0060: Name: CXXABI_TM_1 Flags: none Version: 7
""")
        self.assertEqual(versions, {"GLIBC": (2, 35), "GLIBCXX": (3, 4, 30), "CXXABI": (1, 3, 13)})

    def test_non_versioned_glibc_requirements_cannot_bypass_the_baseline(self):
        for name in ("GLIBC_ABI_DT_RELR", "GLIBC_PRIVATE"):
            with self.assertRaisesRegex(RuntimeError, name):
                linux_symbol_versions("Name: " + name + " Flags: none Version: 2")


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
                self.assertEqual(path.resolve(), (moved / "Contents/Resources/runtime" / name).resolve())
            self.assertEqual((moved / "Contents/Frameworks/libCK2.dylib").read_bytes(), b"Library unchanged")


if __name__ == "__main__":
    unittest.main()
