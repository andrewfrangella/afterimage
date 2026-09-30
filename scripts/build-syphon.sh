#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
source_dir="$root/build-syphon-source"
output_dir="$root/build-syphon"
# Pinned official source, including Apple Silicon and current OpenGL server APIs.
revision=f4761677a45b8034a3c2069ec0f3d2553da81fba
if [[ ! -d "$source_dir/.git" ]]; then
  git clone https://github.com/Syphon/Syphon-Framework.git "$source_dir"
fi
git -C "$source_dir" fetch origin "$revision"
git -C "$source_dir" checkout --detach "$revision"
xcodebuild -project "$source_dir/Syphon.xcodeproj" -scheme Syphon \
  -configuration Release -derivedDataPath "$output_dir" \
  ARCHS=arm64 ONLY_ACTIVE_ARCH=NO MACOSX_DEPLOYMENT_TARGET=26.0 \
  CODE_SIGNING_ALLOWED=NO build
printf 'Syphon framework: %s\n' "$output_dir/Build/Products/Release/Syphon.framework"
