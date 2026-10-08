#!/usr/bin/env bash
# Packs an installed Voxwright.app into a compressed disk image with a link
# to /Applications, so users drag the app across to install it.
#
#   installer/macos/make-dmg.sh <path/to/Voxwright.app> <version> <output dir>
#
# The app must already contain the Qt frameworks (cmake --install runs
# macdeployqt). The image is not signed or notarized: that needs an Apple
# Developer ID, which CI does not have.
set -euo pipefail

app="$1"
version="$2"
out="$3"

if [[ ! -d "$app/Contents/MacOS" ]]; then
    echo "error: $app is not an application bundle" >&2
    exit 1
fi

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT
cp -R "$app" "$staging/"
ln -s /Applications "$staging/Applications"
mkdir -p "$out"
dmg="$out/Voxwright-$version-macos.dmg"
rm -f "$dmg"
hdiutil create -volname "Voxwright $version" -srcfolder "$staging" -fs HFS+ \
    -format UDZO -imagekey zlib-level=9 "$dmg"
echo "$dmg"
