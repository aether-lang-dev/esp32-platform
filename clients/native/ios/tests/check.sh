#!/bin/sh
set -eu
HERE="$(CDPATH= cd -- "$(dirname "$0")" && pwd -P)"
SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/controller-native-test.XXXXXX")"
trap 'rm -rf "$SCRATCH"' EXIT HUP INT TERM
clang -fobjc-arc -fblocks -Wall -Wextra -Werror -I"$HERE/.." \
    "$HERE/../ControllerCredential.m" "$HERE/credential_test.m" \
    -framework Foundation -framework Network -framework Security -o "$SCRATCH/test"
"$SCRATCH/test"
