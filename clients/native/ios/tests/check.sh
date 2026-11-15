#!/bin/sh
set -eu
HERE="$(CDPATH= cd -- "$(dirname "$0")" && pwd -P)"
SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/controller-native-test.XXXXXX")"
trap 'rm -rf "$SCRATCH"' EXIT HUP INT TERM
clang -fobjc-arc -fblocks -Wall -Wextra -Werror -I"$HERE/.." \
    "$HERE/../ControllerCredential.m" "$HERE/credential_test.m" \
    -framework Foundation -framework Network -framework Security -o "$SCRATCH/test"
"$SCRATCH/test"
clang -fobjc-arc -fblocks -Wall -Wextra -Werror -I"$HERE/.." \
    "$HERE/../ControllerConsole.m" "$HERE/console_bounds_test.m" \
    -framework Foundation -o "$SCRATCH/console-test"
"$SCRATCH/console-test"
clang -fobjc-arc -fblocks -Wall -Wextra -Werror -I"$HERE/.." \
    "$HERE/../ControllerConsole.m" "$HERE/console_session_test.m" \
    -framework Foundation -o "$SCRATCH/session-test"
"$SCRATCH/session-test"
clang -fobjc-arc -fblocks -Wall -Wextra -Werror -I"$HERE/.." \
    "$HERE/../ControllerActions.m" "$HERE/actions_test.m" \
    -framework Foundation -o "$SCRATCH/actions-test"
"$SCRATCH/actions-test"
FIRMWARE="$HERE/../../../../firmware/esp-idf/main"
clang -Wall -Wextra -Werror -I"$FIRMWARE" \
    "$FIRMWARE/runner_outlet.c" "$FIRMWARE/runner_switch.c" "$FIRMWARE/runner_apps.c" \
    "$HERE/named_grants_test.c" -o "$SCRATCH/grants-test"
"$SCRATCH/grants-test"
