#!/bin/sh
# Build a macOS .app, ad-hoc sign it, and wrap it in a .dmg.
# Notarization is a separate step and needs an Apple Developer ID. See PACKAGING.md.
set -eu
cd "$(dirname "$0")/.."
cmake -S . -B build-package -G Ninja -DCMAKE_BUILD_TYPE=Release ${WAVEFORM_MACOS_UNIVERSAL:+-DWAVEFORM_MACOS_UNIVERSAL=ON}
cmake --build build-package
APP="build-package/bin/WaveformStudio.app"
codesign --force --deep --sign - "$APP"
cmake --install build-package --prefix build-package/install
cpack --config build-package/CPackConfig.cmake -B build-package/package
echo "Ad-hoc signed app: $APP"
echo "Disk image: build-package/package/"
