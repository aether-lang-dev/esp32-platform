#!/bin/sh
set -eu
CLIENT="$(CDPATH= cd -- "$(dirname "$0")" && pwd -P)"
HERE="$(cd "$CLIENT/../.." && pwd)"
cd "$HERE"
SDK="${DATASTAR_DIR:-$HERE/../datastar-aether}"
exec ae run clients/web/main.ae --lib "$SDK" -- "$@"
