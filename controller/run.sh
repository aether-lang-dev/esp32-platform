#!/bin/sh
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"
SDK="${DATASTAR_DIR:-$HERE/../datastar-aether}"
exec ae run controller/main.ae --lib "$SDK" -- "$@"
