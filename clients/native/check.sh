#!/bin/sh
set -eu
HERE="$(CDPATH= cd -- "$(dirname "$0")" && pwd -P)"
ROOT="$(cd "$HERE/../.." && pwd)"
UI="${AETHER_UI_DIR:-$ROOT/../aether-ui}"
SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/native-controller.XXXXXX")"
trap 'rm -rf "$SCRATCH"' EXIT HUP INT TERM
# This is the trusted native UI host, not a device guest or Rexx grant set.
"${AE:-ae}" build "$HERE/app.ae" --lib "$UI" --emit=csrc \
    --with=fs,os,extern -o "$SCRATCH/controller"
