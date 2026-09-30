#!/bin/bash
set -euo pipefail
if [[ "$(uname -s)" != Darwin ]]; then
  echo "This script requires macOS. Use CMake or make for a Linux build." >&2
  exit 1
fi
if [[ "$(uname -m)" != arm64 ]]; then
  echo "Run from a native Apple Silicon terminal, without Rosetta." >&2
  exit 1
fi
for tool in brew cmake codesign ditto; do
  command -v "$tool" >/dev/null || { echo "Missing $tool. Install Homebrew and run: brew install cmake qt opencv" >&2; exit 1; }
done
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${AFTERIMAGE_BUILD_DIR:-$root/build-macos}"
stage="${AFTERIMAGE_STAGE_DIR:-$root/dist/macos-arm64}"
qt_prefix="$(brew --prefix qt)"
opencv_prefix="$(brew --prefix opencv)"
"$root/scripts/build-syphon.sh"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  -DCMAKE_PREFIX_PATH="$qt_prefix;$opencv_prefix" -DAFTERIMAGE_DEPLOY_MACOS=ON -DAFTERIMAGE_ENABLE_SYPHON=ON \
  -DSYPHON_ROOT="$root/build-syphon/Build/Products/Release"
cmake --build "$build" --parallel 3
ctest --test-dir "$build" --output-on-failure --timeout 30
cmake --install "$build" --prefix "$stage"
app="$stage/Afterimage.app"
# Re-sign every embedded Mach-O after install_name_tool has rewritten load paths.
while IFS= read -r -d '' binary; do
  if file -b "$binary" | /usr/bin/grep -q 'Mach-O'; then
    codesign --force --sign - "$binary"
  fi
done < <(find "$app/Contents" -type f -print0)
while IFS= read -r -d '' framework; do
  codesign --force --sign - "$framework"
done < <(find "$app/Contents" -depth -type d -name '*.framework' -print0)
codesign --force --sign - "$app"
codesign --verify --deep --strict --verbose=2 "$app"
python3 "$root/scripts/verify-macos-bundle.py" "$app"
# Test the deployed Cocoa plugin; offscreen tests already ran before packaging.
env -u DYLD_LIBRARY_PATH -u DYLD_FRAMEWORK_PATH -u QT_PLUGIN_PATH \
  -u DYLD_FALLBACK_LIBRARY_PATH -u DYLD_FALLBACK_FRAMEWORK_PATH \
  -u DYLD_INSERT_LIBRARIES -u QT_QPA_PLATFORM_PLUGIN_PATH \
  QT_QPA_PLATFORM=cocoa QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion \
  "$app/Contents/MacOS/Afterimage" --smoke-test --require-gpu
archive="$root/dist/Afterimage-macos-arm64.zip"
mkdir -p "$root/dist"
ditto -c -k --sequesterRsrc --keepParent "$app" "$archive"
echo "Built: $archive"
