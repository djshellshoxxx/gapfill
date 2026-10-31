#!/usr/bin/env python3
"""Run native GUI checks with the standard X11 desktop protocol atoms present.

A bare Xvfb has no window manager to intern WM_PROTOCOLS. JUCE 8.0.15 queries
that atom with only_if_exists, then uses it when creating a native peer. Keep
this X connection open until the command exits so the server cannot reset the
atoms between setup and JUCE initialization. No X errors are suppressed.
Usage: xvfb-run -a python3 tools/qa/x11_test_session.py command [arguments...]
"""

from __future__ import annotations

import argparse
import ctypes
import subprocess
import sys


DESKTOP_ATOMS = (
    b"WM_PROTOCOLS", b"WM_TAKE_FOCUS", b"WM_DELETE_WINDOW", b"_NET_WM_PING",
    b"WM_CHANGE_STATE", b"WM_STATE", b"_NET_WM_STATE", b"_NET_WM_STATE_HIDDEN",
    b"_NET_WM_STATE_MAXIMIZED_HORZ", b"_NET_WM_STATE_MAXIMIZED_VERT",
    b"_NET_WM_WINDOW_TYPE", b"_NET_WM_WINDOW_TYPE_NORMAL", b"_NET_WM_WINDOW_TYPE_COMBO",
)


def run_in_session(command, *, platform=sys.platform, library_loader=ctypes.CDLL, runner=subprocess.run):
    if not command:
        raise ValueError("A command is required")
    if platform != "linux":
        return runner(command, check=False).returncode

    library = library_loader("libX11.so.6")
    library.XOpenDisplay.argtypes = [ctypes.c_char_p]
    library.XOpenDisplay.restype = ctypes.c_void_p
    library.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    library.XInternAtom.restype = ctypes.c_ulong
    library.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
    library.XCloseDisplay.argtypes = [ctypes.c_void_p]
    display = library.XOpenDisplay(None)
    if not display:
        raise RuntimeError("Cannot open DISPLAY; run this command inside xvfb-run or an X11 session")

    try:
        for name in DESKTOP_ATOMS:
            if library.XInternAtom(display, name, 0) == 0:
                raise RuntimeError("Could not establish the X11 desktop protocol atom " + name.decode("ascii"))
        library.XSync(display, 0)
        return runner(command, check=False).returncode
    finally:
        library.XCloseDisplay(display)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a command is required")
    try:
        return run_in_session(command)
    except (OSError, RuntimeError) as error:
        print("X11 test session: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
