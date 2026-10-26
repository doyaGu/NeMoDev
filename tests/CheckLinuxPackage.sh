#!/bin/bash
set -euo pipefail

# Runtime-only packages in clean distribution containers. No compiler, SDK,
# developer library paths or test-enabled editor can satisfy dependencies here.
. /etc/os-release
printf 'Checking Linux distribution: %s\n' "$PRETTY_NAME"
case "$ID" in
    ubuntu|debian)
        bash /checks/CheckDebianPackage.sh
        apt-get install -y xz-utils
        ;;
    fedora)
        dnf install -y --setopt=install_weak_deps=False gtk3 libpng zlib libstdc++ systemd-libs dbus-libs libdecor alsa-lib pulseaudio-libs \
            vulkan-loader mesa-vulkan-drivers zenity python3 xorg-x11-server-Xvfb xorg-x11-xauth \
            xwininfo xprop tar xz
        ;;
    opensuse-tumbleweed)
        zypper --non-interactive install --no-recommends libgtk-3-0 libpng16-16 libz1 libstdc++6 \
            libudev1 libdbus-1-3 libdecor-0-0 libasound2 libpulse0 libvulkan1 libvulkan_lvp zenity python3 \
            xorg-x11-server-Xvfb xauth xwininfo xprop tar xz
        ;;
    arch)
        pacman -Syu --noconfirm --needed gtk3 libpng zlib gcc-libs systemd-libs dbus libdecor alsa-lib libpulse vulkan-icd-loader \
            vulkan-swrast zenity python xorg-server-xvfb xorg-xauth xorg-xwininfo xorg-xprop tar xz
        ;;
    *) printf 'No runtime test setup for %s\n' "$ID" >&2; exit 1 ;;
esac

export VK_DRIVER_FILES
VK_DRIVER_FILES="$(find /usr/share/vulkan/icd.d -name '*lvp*json' -print -quit)"
test -f "$VK_DRIVER_FILES"
export VK_ICD_FILENAMES="$VK_DRIVER_FILES"
checks=/checks
temporary="$(mktemp -d)"
Xvfb -displayfd 3 -screen 0 1920x1080x24 -nolisten tcp \
    3> "$temporary/display" > "$temporary/xvfb.log" 2>&1 &
xvfb=$!
cleanup() {
    kill "$xvfb" 2>/dev/null || true
    wait "$xvfb" 2>/dev/null || true
    rm -rf "$temporary"
}
trap cleanup EXIT
for attempt in {1..50}; do
    if test -s "$temporary/display"; then break; fi
    if ! kill -0 "$xvfb" 2>/dev/null; then cat "$temporary/xvfb.log"; exit 1; fi
    sleep .1
done
test -s "$temporary/display"
export DISPLAY=":$(cat "$temporary/display")"
xwininfo -root >/dev/null
cd /packages
sha256sum --check NeMoDev-*.tar.xz.sha256
tar -xJf NeMoDev-*.tar.xz -C "$temporary"
package="$temporary/NeMoDev"
test -x "$package/NeMoDev"
test -f "$package/data/interface.nmo"
test ! -e "$package/Bin/data"
python3 "$checks/CheckProductionBinary.py" --binary "$package/Bin/NeMoDev" --package "$package"
python3 "$checks/InstalledAppSmoke.py" \
    --package "$package" --executable "$package/NeMoDev"
mv "$package" "$temporary/Relocated Package [test]"
package="$temporary/Relocated Package [test]"
ln -s "$package/NeMoDev" "$temporary/nemodev"
python3 "$checks/InstalledAppSmoke.py" \
    --package "$package" --executable "$temporary/nemodev"
timeout 30 python3 "$checks/LinuxPrintBackends.py" --package "$package"
