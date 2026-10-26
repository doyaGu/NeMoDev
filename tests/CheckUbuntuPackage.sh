#!/bin/bash
set -euo pipefail

# Run in a fresh ubuntu:24.04 container. Only the DEB and external checks are
# mounted, so build tools, SDK trees and developer library paths cannot help it.
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y /packages/NeMoDev-*.deb python3 xvfb xauth x11-utils mesa-vulkan-drivers desktop-file-utils
desktop-file-validate /usr/share/applications/nemodev.desktop
test -x /usr/bin/nemodev
python3 /checks/CheckProductionBinary.py --binary /opt/nemodev/Bin/NeMoDev --package /opt/nemodev
export VK_DRIVER_FILES
VK_DRIVER_FILES="$(dpkg -L mesa-vulkan-drivers | grep '/lvp_icd[^/]*\.json$')"
test -f "$VK_DRIVER_FILES"
xvfb-run -a -s '-screen 0 1920x1080x24' python3 /checks/InstalledAppSmoke.py
