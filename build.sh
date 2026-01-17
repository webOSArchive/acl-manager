#!/bin/bash
#
# Build and package ACL Manager for WebOS
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PALM_PACKAGE="${PALM_PACKAGE:-/opt/PalmSDK/Current/bin/palm-package}"

echo "Building ACL Manager..."
make -C "$SCRIPT_DIR" clean
make -C "$SCRIPT_DIR"

echo "Packaging..."
"$PALM_PACKAGE" "$SCRIPT_DIR"

IPK=$(ls -t "$SCRIPT_DIR"/*.ipk 2>/dev/null | head -1)
if [ -n "$IPK" ]; then
    echo "Package created: $IPK"
    echo "Install with: palm-install $IPK"
else
    echo "ERROR: Package creation failed"
    exit 1
fi
