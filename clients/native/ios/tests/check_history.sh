#!/bin/sh
set -eu
HERE="$(CDPATH= cd -- "$(dirname "$0")" && pwd -P)"
SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/controller-history.XXXXXX")"
trap 'rm -rf "$SCRATCH"' EXIT HUP INT TERM
SDK="$(xcrun --sdk macosx --show-sdk-path)"
xcrun --sdk macosx clang -fobjc-arc -target "$(uname -m)-apple-ios15.0-macabi" \
    -isysroot "$SDK" -iframework "$SDK/System/iOSSupport/System/Library/Frameworks" \
    -I"$HERE/.." "$HERE/../ControllerHistory.m" "$HERE/history_test.m" \
    -framework UIKit -framework Foundation -o "$SCRATCH/test"
"$SCRATCH/test"
