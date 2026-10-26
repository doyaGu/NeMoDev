#!/bin/bash
set -euo pipefail

# Check a copy of the actual DMG bundle with an external core regression driver.
# The verified distribution itself never receives test programs or Test Engine.
test_binary="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
dmg="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
temporary="$(mktemp -d)"
mount="$temporary/mount"
cleanup() {
    if mount | grep -Fq "on $mount "; then hdiutil detach "$mount"; fi
    rm -rf "$temporary"
}
trap cleanup EXIT
hdiutil attach -readonly -nobrowse -mountpoint "$mount" "$dmg"
ditto "$mount/NeMoDev.app" "$temporary/Original.app"
hdiutil detach "$mount"
app="$temporary/Relocated Package [test].app"
mv "$temporary/Original.app" "$app"
contents="$app/Contents"
checks="$(cd "$(dirname "$0")" && pwd)"
codesign --verify --deep --strict "$app"
python3 "$checks/CheckProductionBinary.py" --binary "$contents/MacOS/NeMoDev" --package "$app"
xcrun clang -fobjc-arc "$checks/InstalledMacAppSmoke.m" \
    -framework AppKit -framework CoreGraphics -o "$temporary/MacStartupCheck"
"$temporary/MacStartupCheck" "$app"
cp "$test_binary" "$contents/MacOS/RuntimeCheck"

# Adjust only the external driver. An environment override would also hide
# incorrect install names in the shipped plugins and libraries.
while IFS= read -r rpath; do
    if [[ "$rpath" == /* ]]; then install_name_tool -delete_rpath "$rpath" "$contents/MacOS/RuntimeCheck"; fi
done < <(otool -l "$contents/MacOS/RuntimeCheck" | awk '/cmd LC_RPATH/{getline; getline; sub(/^ *path /, ""); sub(/ \(offset.*$/, ""); print}')
install_name_tool -add_rpath '@loader_path/../Frameworks' "$contents/MacOS/RuntimeCheck"
codesign --force --sign - --timestamp=none "$contents/MacOS/RuntimeCheck"
env -u VIRTOOLS_PATH -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH -u DYLD_FRAMEWORK_PATH \
    SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
    "$contents/MacOS/RuntimeCheck" --runtime auto
