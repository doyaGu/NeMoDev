"""Start the installed Linux editor on X11, check its window and close it.

This runs outside the product and needs no SDK, build tree or test-enabled editor.
Use Xvfb with a 1920x1080x24 screen and Mesa software Vulkan in headless CI.
"""

import argparse
import ctypes as C
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


class ClientData(C.Union):
    _fields_ = [("b", C.c_char * 20), ("s", C.c_short * 10), ("l", C.c_long * 5)]


class ClientMessage(C.Structure):
    _fields_ = [("type", C.c_int), ("serial", C.c_ulong), ("send_event", C.c_int),
                ("display", C.c_void_p), ("window", C.c_ulong), ("message_type", C.c_ulong),
                ("format", C.c_int), ("data", ClientData)]


class Event(C.Union):
    _fields_ = [("client", ClientMessage), ("pad", C.c_long * 24)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=Path("/opt/nemodev"))
    parser.add_argument("--executable", type=Path, help="Use a package launcher or its symlink")
    args = parser.parse_args()
    package = args.package.resolve()
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("LD_") and key not in ("LIBRARY_PATH", "CMAKE_PREFIX_PATH", "VIRTOOLS_PATH")}
    env.update(PATH="/usr/bin:/bin", SDL_VIDEO_DRIVER="x11", SDL_AUDIO_DRIVER="dummy", SDL_GPU_DRIVER="vulkan")
    x = C.CDLL("libX11.so.6")
    x.XOpenDisplay.argtypes = [C.c_char_p]
    x.XOpenDisplay.restype = C.c_void_p
    x.XInternAtom.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
    x.XInternAtom.restype = C.c_ulong
    x.XGetImage.argtypes = [C.c_void_p, C.c_ulong, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_ulong, C.c_int]
    x.XGetImage.restype = C.c_void_p
    x.XGetPixel.argtypes = [C.c_void_p, C.c_int, C.c_int]
    x.XGetPixel.restype = C.c_ulong
    x.XDestroyImage.argtypes = [C.c_void_p]
    x.XSendEvent.argtypes = [C.c_void_p, C.c_ulong, C.c_int, C.c_long, C.c_void_p]
    x.XSendEvent.restype = C.c_int
    x.XFlush.argtypes = [C.c_void_p]
    x.XCloseDisplay.argtypes = [C.c_void_p]
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError("X11 display unavailable")
    try:
        with tempfile.TemporaryDirectory(prefix="nemodev-installed-") as temporary:
            env.update(XDG_CONFIG_HOME=temporary + "/config", XDG_DATA_HOME=temporary + "/data")
            with tempfile.TemporaryFile(mode="w+") as log:
                executable = args.executable.absolute() if args.executable else package / "Bin/NeMoDev"
                process = subprocess.Popen([str(executable)], env=env, cwd=temporary,
                                           stdout=log, stderr=subprocess.STDOUT)
                try:
                    window = None
                    deadline = time.monotonic() + 60
                    while time.monotonic() < deadline and process.poll() is None:
                        listing = subprocess.run(["xwininfo", "-root", "-tree"], env=env,
                                                 capture_output=True, text=True, check=True).stdout
                        for identifier, title in re.findall(r'(0x[0-9a-f]+) "(NeMoDev[^"\n]*)"', listing):
                            pid = subprocess.run(["xprop", "-id", identifier, "_NET_WM_PID"], env=env,
                                                 capture_output=True, text=True, check=True).stdout
                            if re.search(r"=\s*" + str(process.pid) + r"\s*$", pid) and "New Composition" in title:
                                if title.endswith(" *"):
                                    raise RuntimeError("The initial composition is unexpectedly modified")
                                window = int(identifier, 16)
                                break
                        if window:
                            break
                        time.sleep(.25)
                    if not window:
                        raise RuntimeError(f"Initialized composition window absent; exit={process.poll()}")
                    # The document title is ready before the first GPU frame.
                    # Cold software Vulkan pipelines can take several seconds.
                    colors = set()
                    deadline = time.monotonic() + 60
                    while time.monotonic() < deadline and process.poll() is None:
                        image = x.XGetImage(display, window, 20, 80, 500, 300, C.c_ulong(-1).value, 2)
                        if not image:
                            raise RuntimeError("Cannot read the initialized application window")
                        try:
                            colors = {x.XGetPixel(image, px, py) for py in range(0, 300, 3) for px in range(0, 500, 3)}
                        finally:
                            x.XDestroyImage(image)
                        if len(colors) > 24:
                            break
                        time.sleep(.25)
                    if len(colors) <= 24:
                        raise RuntimeError(f"The editor did not draw its interface: {len(colors)} colors; exit={process.poll()}")
                    maps = Path(f"/proc/{process.pid}/maps").read_text()
                    for library in ("libCK2.so", "libVxMath.so", "libCK2_3D.so", "libhpdf", "libSDL3"):
                        loaded = [line for line in maps.splitlines() if library in line]
                        if not loaded or any(str(package) not in line for line in loaded):
                            raise RuntimeError(f"{library} was not loaded exclusively from the installed package")
                    event = Event()
                    event.client.type = 33  # ClientMessage
                    event.client.display = display
                    event.client.window = window
                    event.client.message_type = x.XInternAtom(display, b"WM_PROTOCOLS", False)
                    event.client.format = 32
                    event.client.data.l[0] = x.XInternAtom(display, b"WM_DELETE_WINDOW", False)
                    if not x.XSendEvent(display, window, False, 0, C.byref(event)):
                        raise RuntimeError("Could not request normal window closure")
                    x.XFlush(display)
                    if process.wait(timeout=20) != 0:
                        raise RuntimeError("The installed editor failed during shutdown")
                    print(f"Installed editor: new composition, {len(colors)} colors, bundled libraries, clean shutdown")
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                    log.seek(0)
                    print(log.read())
    finally:
        x.XCloseDisplay(display)


if __name__ == "__main__":
    main()
