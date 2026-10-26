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
ditto "$mount/NeMoDev.app" "$temporary/NeMoDev.app"
hdiutil detach "$mount"
contents="$temporary/NeMoDev.app/Contents"
cp "$test_binary" "$contents/MacOS/RuntimeCheck"
env -u VIRTOOLS_PATH SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
    DYLD_LIBRARY_PATH="$contents/Frameworks" \
    "$contents/MacOS/RuntimeCheck" --runtime "$contents/Resources"
