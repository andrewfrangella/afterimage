#!/usr/bin/env python3
"""Fail packaging if a Mach-O depends on libraries outside the app or macOS."""
import pathlib
import plistlib
import subprocess
import sys


def verify(app):
    app = pathlib.Path(app).resolve()
    with (app / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    assert info.get("NSCameraUsageDescription"), "Missing camera permission description"
    assert info.get("CFBundleIdentifier") == "studio.afterimage.app"
    executable = app / "Contents/MacOS" / info["CFBundleExecutable"]
    assert executable.is_file(), f"Missing executable: {executable}"
    assert (app / "Contents/PlugIns/platforms/libqcocoa.dylib").is_file(), "Missing Cocoa platform plugin"
    checked = 0
    for binary in app.rglob("*"):
        if not binary.is_file() or binary.is_symlink():
            continue
        if "Mach-O" not in subprocess.check_output(["file", "-b", str(binary)], text=True):
            continue
        architectures = subprocess.check_output(["lipo", "-archs", str(binary)], text=True).split()
        assert "arm64" in architectures, f"Not arm64: {binary}"
        dependencies = subprocess.check_output(["otool", "-L", str(binary)], text=True).splitlines()[1:]
        for line in dependencies:
            dependency = line.strip().split(" (", 1)[0]
            if dependency.startswith(("/System/Library/", "/usr/lib/")):
                continue
            if dependency.startswith("@executable_path/"):
                resolved = executable.parent / dependency.removeprefix("@executable_path/")
            elif dependency.startswith("@loader_path/"):
                resolved = binary.parent / dependency.removeprefix("@loader_path/")
            elif dependency.startswith("@rpath/"):
                # BundleUtilities should rewrite dependencies to explicit bundle-relative paths.
                raise AssertionError(f"Unresolved rpath dependency: {binary}: {dependency}")
            else:
                resolved = pathlib.Path(dependency)
            resolved = resolved.resolve()
            assert resolved.is_relative_to(app) and resolved.is_file(), f"External/missing dependency: {binary}: {dependency}"
        checked += 1
    assert checked, "No Mach-O binaries checked"
    print(f"Verified {checked} arm64 binaries, bundled dependencies, Cocoa plugin and camera metadata")


if __name__ == "__main__":
    verify(sys.argv[1])
