"""Start a relocated production editor, check its window and DLLs, then close it.

Runs on an interactive Windows desktop with Pillow. No product
test hooks, SDK installation or developer DLL search paths are used.
"""

import argparse
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import subprocess
import tempfile
import time

from PIL import ImageGrab


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    args = parser.parse_args()
    package = args.package.resolve(strict=True)
    user = C.WinDLL("user32", use_last_error=True)
    kernel = C.WinDLL("kernel32", use_last_error=True)
    callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    user.EnumWindows.argtypes = [callback_type, W.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
    user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
    user.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
    user.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
    kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    kernel.OpenProcess.restype = W.HANDLE
    kernel.CloseHandle.argtypes = [W.HANDLE]
    kernel.K32EnumProcessModules.argtypes = [W.HANDLE, C.POINTER(W.HMODULE), W.DWORD, C.POINTER(W.DWORD)]
    kernel.K32GetModuleFileNameExW.argtypes = [W.HANDLE, W.HMODULE, W.LPWSTR, W.DWORD]
    environment = {key: value for key, value in os.environ.items()
                   if key.upper() not in ("VIRTOOLS_PATH", "LIBRARY_PATH", "CMAKE_PREFIX_PATH")
                   and not key.upper().startswith(("SDL_", "LD_", "DYLD_"))}
    system = Path(os.environ["SystemRoot"])
    environment.update(PATH=str(system / "System32") + os.pathsep + str(system), SDL_AUDIO_DRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="NeMoDev launch directory ") as directory:
        with tempfile.TemporaryFile(mode="w+") as log:
            process = subprocess.Popen([str(package / "Bin/NeMoDev.exe")], env=environment,
                                       cwd=directory, stdout=log, stderr=subprocess.STDOUT)
            try:
                windows = []

                @callback_type
                def find_window(window, _):
                    pid = W.DWORD()
                    user.GetWindowThreadProcessId(window, C.byref(pid))
                    if pid.value == process.pid:
                        title = C.create_unicode_buffer(1024)
                        user.GetWindowTextW(window, title, len(title))
                        if "NeMoDev" in title.value and "New Composition" in title.value:
                            windows.append((window, title.value))
                    return True

                deadline = time.monotonic() + 60
                while time.monotonic() < deadline and process.poll() is None:
                    user.EnumWindows(find_window, 0)
                    if windows:
                        break
                    time.sleep(.25)
                if not windows:
                    raise RuntimeError(f"Initialized composition window absent; exit={process.poll()}")
                window, title = windows[0]
                if title.endswith(" *"):
                    raise RuntimeError("The initial composition is unexpectedly modified")
                # GPU swapchains are not exposed through PrintWindow. Bring only
                # this test process's window above others and sample the desktop.
                if not user.SetWindowPos(window, W.HWND(-1), 20, 20, 1200, 800, 0x40):
                    raise C.WinError(C.get_last_error())
                colors = []
                deadline = time.monotonic() + 60
                while time.monotonic() < deadline and process.poll() is None:
                    rectangle = W.RECT()
                    if not user.GetWindowRect(window, C.byref(rectangle)):
                        raise C.WinError(C.get_last_error())
                    image = ImageGrab.grab(bbox=(rectangle.left + 20, rectangle.top + 80,
                                                rectangle.left + 520, rectangle.top + 380)).convert("RGB")
                    colors = image.getcolors(image.width * image.height) or []
                    if len(colors) > 24:
                        break
                    time.sleep(.25)
                if len(colors) <= 24:
                    raise RuntimeError(f"The editor did not draw its interface: {len(colors)} colors")
                handle = kernel.OpenProcess(0x410, False, process.pid)  # QUERY_INFORMATION | VM_READ
                if not handle:
                    raise C.WinError(C.get_last_error())
                try:
                    modules = (W.HMODULE * 1024)()
                    required = W.DWORD()
                    if not kernel.K32EnumProcessModules(handle, modules, C.sizeof(modules), C.byref(required)):
                        raise C.WinError(C.get_last_error())
                    if required.value > C.sizeof(modules):
                        raise RuntimeError("The module list exceeds the inspection buffer")
                    loaded = {}
                    for module in modules[:required.value // C.sizeof(W.HMODULE)]:
                        name = C.create_unicode_buffer(32768)
                        if not kernel.K32GetModuleFileNameExW(handle, module, name, len(name)):
                            raise C.WinError(C.get_last_error())
                        path = Path(name.value).resolve(strict=True)
                        loaded[path.name.lower()] = path
                    for name in ("ck2.dll", "vxmath.dll", "sdl3.dll", "ck2_3d.dll", "msvcp140.dll", "vcruntime140.dll"):
                        try:
                            loaded[name].relative_to(package)
                        except (KeyError, ValueError):
                            raise RuntimeError(f"{name} was not loaded from the relocated package")
                finally:
                    kernel.CloseHandle(handle)
                if not user.PostMessageW(window, 0x10, 0, 0):  # WM_CLOSE
                    raise C.WinError(C.get_last_error())
                if process.wait(timeout=20) != 0:
                    raise RuntimeError("The installed editor failed during shutdown")
                print(f"Relocated Windows editor: new composition, {len(colors)} colors, bundled DLLs, clean shutdown")
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
                log.seek(0)
                print(log.read())


if __name__ == "__main__":
    main()
