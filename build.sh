#!/bin/bash
#
# Build and package ACL Manager for WebOS.
#
# After palm-package creates the base .ipk, this script repacks it to
# inject postinst/prerm control scripts.  These scripts run as root
# when the package is installed via Preware/ipkg, and handle copying
# device components (acl-helper, acl-watchd) to their system locations.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PALM_PACKAGE="${PALM_PACKAGE:-/opt/PalmSDK/Current/bin/palm-package}"

# ── 1. Build binaries ────────────────────────────────────────────────
echo "Building..."
make -C "$SCRIPT_DIR" clean
make -C "$SCRIPT_DIR"

# ── 2. Palm-package creates the base .ipk ────────────────────────────
echo "Packaging (palm-package)..."
"$PALM_PACKAGE" "$SCRIPT_DIR"

IPK=$(ls -t "$SCRIPT_DIR"/*.ipk 2>/dev/null | head -1)
if [ -z "$IPK" ]; then
    echo "ERROR: palm-package did not produce an .ipk"
    exit 1
fi
echo "Base package: $IPK"

# ── 3. Inject postinst/prerm into the .ipk ───────────────────────────
#
# An .ipk is an 'ar' archive with three members:
#   debian-binary   – contains "2.0\n"
#   control.tar.gz  – package metadata + install scripts
#   data.tar.gz     – the actual files to install
#
# palm-package generates a minimal control.tar.gz with only a 'control'
# file.  We extract it, add our scripts, and repack.
#
echo "Injecting control scripts..."

# Use WORK_DIR, not TMPDIR — the shell uses $TMPDIR internally (e.g. in mktemp)
# and overwriting it mid-script breaks things.
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

# Extract the ar archive.
(cd "$WORK_DIR" && ar x "$IPK")

# Extract the existing control.tar.gz.
mkdir -p "$WORK_DIR/ctrl"
tar -xzf "$WORK_DIR/control.tar.gz" -C "$WORK_DIR/ctrl"

# Add postinst and prerm.
cp "$SCRIPT_DIR/postinst" "$WORK_DIR/ctrl/postinst"
cp "$SCRIPT_DIR/prerm"    "$WORK_DIR/ctrl/prerm"
chmod 755 "$WORK_DIR/ctrl/postinst" "$WORK_DIR/ctrl/prerm"

# Repack control.tar.gz.
(cd "$WORK_DIR/ctrl" && tar -czf "$WORK_DIR/control.tar.gz" .)

# Rebuild the ar archive into a fresh file, then replace the original.
# Don't run 'ar r' on the existing $IPK — behavior on an already-existing
# archive is implementation-defined and can produce duplicate members.
(cd "$WORK_DIR" && ar rc repacked.ipk debian-binary control.tar.gz data.tar.gz)
mv "$WORK_DIR/repacked.ipk" "$IPK"

echo "Package ready: $IPK"
echo ""
echo "Install via Preware or WebOS Quick Install (NOT palm-install):"
echo "  The postinst script requires root to set up device components."
