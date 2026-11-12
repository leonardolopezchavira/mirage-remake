#!/bin/sh
# Package the macOS build: tools/package.sh <build dir> <output .zip>
set -e
BUILD=$1; OUT=$2
case $OUT in /*) ;; *) OUT="$PWD/$OUT" ;; esac
ROOT=$(cd "$(dirname "$0")/.." && pwd)
STAGE=$(mktemp -d)/Mirage
mkdir -p "$STAGE"
for b in Mirage.app Mirage.component Mirage.vst3 Mirage.clap; do
    cp -R "$BUILD/bin/$b" "$STAGE/"
    # Ad-hoc signature over the whole bundle (not a Developer ID: remove the quarantine flag
    # after downloading, see README).
    codesign --force --deep --sign - --timestamp=none "$STAGE/$b"
    codesign --verify --strict "$STAGE/$b"
done
cp "$ROOT/README.md" "$ROOT/LICENSE.md" "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/"
(cd "$(dirname "$STAGE")" && zip -qr -X -y "$OUT" Mirage)
echo "wrote $OUT"
