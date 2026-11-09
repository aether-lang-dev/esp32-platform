#!/bin/sh
# Aether and RexxAE on ESP32 / ESP32-S3, under emulation.
#
#   ./lane.sh                 hello + rexxae
#   ./lane.sh hello           just one of them
#   ./lane.sh runner          flash script and runner boundary checks
#
# Builds the pinned Aether compiler in scratch storage, generates C, then
# uses ESP-IDF and Espressif QEMU in a container to check the serial console.
# Nothing ESP-side is installed on the host.
#
# Environment:
#   AETHER_DIR   source repository holding the pinned release (default ../aether)
#   AETHER_REF   explicit override of the committed AETHER_REF pin
#                warns on a version mismatch; AETHER_STRICT=1 fails
#   ESP32_TARGET esp32s3 (default), esp32s3-no-psram, or esp32
#   REXXAE_DIR   RexxAE checkout with quota cleanup     (default ../RexxAE)
#   ESP32_IDF_IMAGE, ESP32_ENGINE   image / container engine
#   ESP32_KEEP   where the run's generated C, logs and ELFs are copied after
#                the run (default: not kept). The
#                run itself works under $TMPDIR: podman machine only shares
#                /Users and /private/var/folders with its VM, not /Volumes.
#                The ESP-IDF build directory stays inside the container.
#   RUNNER_CYD_SWITCH_LED=1  use the FNK0114B red LED as the signed USB switch output
#   RUNNER_CYD_CONSOLE=1  build the Freenove FNK0114B display console image
#   RUNNER_BOARD_SMOKE=1  run the short board smoke test and retain its flash image
#   RUNNER_TOIT_IO=1  build the wired ESP32 example image without booting QEMU
#   RUNNER_TOIT_WAKE=timer|gpio  build a dedicated classic ESP32 wake demo image
#   QEMU_PSRAM   -m for QEMU (default 8M, the N16R8's PSRAM)
#   QEMU_TIMEOUT seconds per boot (default 120)
#   AE_TRACE_ALLOC=1  diagnostic build: rexx_host logs every allocation of
#                4 KB or more with its caller (ALLOC lines in the log)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
AETHER_DIR="$(cd "${AETHER_DIR:-$HERE/../aether}" && pwd)"
REXXAE_DIR="$(cd "${REXXAE_DIR:-$HERE/../RexxAE}" && pwd)"

IMAGE="${ESP32_IDF_IMAGE:-docker.io/espressif/idf:v6.1}"
# Use one release for both the compiler and the runtime, without changing
# the sibling checkout. AETHER_REF is an explicit development override.
AETHER_REF="${AETHER_REF:-$(cat "$HERE/AETHER_REF")}"
AETHER_WANT="0.891.0"
TARGET="${ESP32_TARGET:-esp32s3}"
case "$TARGET" in
    esp32s3|esp32s3-no-psram|esp32) ;;
    *) echo "[FAIL] unknown ESP32_TARGET: $TARGET"; exit 1 ;;
esac

ENGINE="${ESP32_ENGINE:-}"
[ -n "$ENGINE" ] || for e in podman docker; do
    if command -v "$e" > /dev/null 2>&1; then ENGINE="$e"; break; fi
done
[ -n "$ENGINE" ] || { echo "[FAIL] neither podman nor docker is installed"; exit 1; }


tmp="$(mktemp -d "${TMPDIR:-/tmp}/esp32-lane.XXXXXX")"
keep() {
    if [ -n "${ESP32_KEEP:-}" ]; then
        mkdir -p "$ESP32_KEEP"
        rm -rf "$tmp/aether" "$tmp/rexxae" "$tmp/source.git"
        cp -R "$tmp/." "$ESP32_KEEP/" 2>/dev/null
    fi
    rm -rf "$tmp"
}
trap keep EXIT
mkdir -p "$tmp/gen" "$tmp/out"
chmod 777 "$tmp/out"
# git archive reads committed source only, including when the checkout is dirty.
source_repo="$AETHER_DIR"
if ! git -C "$source_repo" cat-file -e "$AETHER_REF^{commit}" 2>/dev/null; then
    # Obtain a missing release in scratch storage, never fetch into the sibling.
    source_repo="$tmp/source.git"
    git init --bare -q "$source_repo"
    git -C "$source_repo" fetch --depth=1 https://github.com/aether-lang-dev/aether.git "$AETHER_REF"
    revision="$(git -C "$source_repo" rev-parse 'FETCH_HEAD^{commit}')"
    # Full commit pins must resolve byte-for-byte to the requested object.
    case "$AETHER_REF" in
        ????????????????????????????????????????)
            [ "$revision" = "$AETHER_REF" ] || { echo "[FAIL] Aether pin mismatch"; exit 1; } ;;
    esac
else
    revision="$(git -C "$source_repo" rev-parse "$AETHER_REF^{commit}")"
fi
mkdir -p "$tmp/aether"
git -C "$source_repo" archive "$revision" > "$tmp/aether.tar"
tar -C "$tmp/aether" -xf "$tmp/aether.tar"
rm "$tmp/aether.tar"
if ! make -C "$tmp/aether" compiler -j "${ESP32_BUILD_JOBS:-4}" > "$tmp/compiler.build.log" 2>&1; then
    echo "[FAIL] pinned aetherc build failed"
    tail -20 "$tmp/compiler.build.log"
    exit 1
fi
AETHERC="$tmp/aether/build/aetherc"
# The compiler otherwise consults the caller's installation for std imports.
export AETHER_HOME="$tmp/aether"
have="$("$AETHERC" --version 2>&1 | head -1 | sed 's/.* v\{0,1\}//')"
if [ "$have" != "$AETHER_WANT" ]; then
    echo "[WARN] aetherc is $have; the lane is verified against $AETHER_WANT"
    [ "${AETHER_STRICT:-}" != 1 ] || exit 1
fi
printf '%s\n' "$revision" > "$tmp/out/aether.revision"
cp "$tmp/aether/VERSION" "$tmp/out/aether.version"
printf '%s\n' "$TARGET" > "$tmp/out/target"
git -C "$REXXAE_DIR" rev-parse HEAD > "$tmp/out/rexxae.revision"
echo "aetherc: $("$AETHERC" --version 2>&1 | head -1)  ($AETHER_REF @ $revision)"
echo "RexxAE:  $(git -C "$REXXAE_DIR" log -1 --format='%h %s' 2>/dev/null)"
echo "target:  $TARGET"
echo "results: ${ESP32_KEEP:-(not kept)}"

want="${*:-hello rexxae}"
case " $want " in
    *" runner-http "*|*" runner-signed "*|*" runner-signed-psk "*|*" runner-cyd-wifi "*|*" runner-cyd-usb "*|*" psk-probe "*)
        [ "$TARGET" = esp32 ] || { echo "[FAIL] network runners require ESP32_TARGET=esp32"; exit 1; } ;;
esac
case " $want " in
    *" runner "*|*" runner-http "*|*" runner-signed "*|*" runner-signed-psk "*|*" runner-cyd-wifi "*|*" runner-cyd-usb "*)
        # RexxAE must release allocations on memory and deadline interruptions.
        if ! git -C "$REXXAE_DIR" merge-base --is-ancestor ef7a744ef8cb1076224159b76c4d8488354fee57 HEAD; then
            echo "[FAIL] runner requires RexxAE ef7a744 or a descendant"
            exit 1
        fi ;;
esac
pass=0
fail=0
ok() { echo "[PASS] $1"; pass=$((pass + 1)); }
bad() { echo "[FAIL] $1"; fail=$((fail + 1)); }

args=""
for p in $want; do
    case "$p" in
    psk-probe)
        : > "$tmp/gen/psk-probe.c"
        args="$args psk-probe:psk_probe" ;;
    hello)
        "$AETHERC" "$tmp/aether/examples/01-language/01-hello.ae" "$tmp/gen/hello.c" \
            > "$tmp/hello.gen.log" 2>&1 && args="$args hello" ||
            { bad "hello: aetherc failed"; head "$tmp/hello.gen.log"; } ;;
    rexxae|runner|runner-http|runner-signed|runner-signed-psk|runner-cyd-wifi|runner-cyd-usb)
        # The engine as a C library (--emit=lib): no main, no app_main; the
        # C host in project/main/rexx_host.c supplies app_main. RexxAE's
        # imports are src.*, so aetherc runs from the RexxAE root.
        (cd "$REXXAE_DIR" && "$AETHERC" --emit=lib src/engine.ae "$tmp/gen/$p.c") \
            > "$tmp/$p.gen.log" 2>&1 && {
                host=rexx_host
                [ "$p" != runner ] || host=runner_host
                case "$p" in runner-http|runner-signed|runner-signed-psk|runner-cyd-wifi) host=runner_http_host ;; runner-cyd-usb) host=runner_usb_host ;; esac
                args="$args $p:$host"
            } || { bad "$p: aetherc failed"; head "$tmp/$p.gen.log"; } ;;
    *) bad "unknown program $p" ;;
    esac
done
[ -n "$args" ] || exit 1

# Stage RexxAE's header beside the pinned Aether snapshot. Neither source
# checkout is mounted in the container.
mkdir -p "$tmp/rexxae"
tar -C "$REXXAE_DIR" -cf - include | tar -C "$tmp/rexxae" -xf -

# Freeze lane inputs too: an edit during a long build must not change the
# running shell script or its project halfway through a container run.
cp -R "$HERE/project" "$HERE/scripts" "$HERE/tools" "$tmp/"
cp "$HERE/in_container.sh" "$tmp/in_container.sh"
"$ENGINE" run --rm \
    -e ESP32_TARGET="$TARGET" -e QEMU_PSRAM="${QEMU_PSRAM:-8M}" \
    -e RUNNER_CYD_SWITCH_LED="${RUNNER_CYD_SWITCH_LED:-}" \
    -e RUNNER_CYD_CONSOLE="${RUNNER_CYD_CONSOLE:-}" \
    -e RUNNER_BOARD_SMOKE="${RUNNER_BOARD_SMOKE:-}" \
    -e RUNNER_TOIT_WAKE="${RUNNER_TOIT_WAKE:-}" \
    -e RUNNER_TOIT_IO="${RUNNER_TOIT_IO:-}" \
    -e RUNNER_LED_GPIO="${RUNNER_LED_GPIO:-}" \
    -e QEMU_TIMEOUT="${QEMU_TIMEOUT:-120}" -e AE_TRACE_ALLOC="${AE_TRACE_ALLOC:-}" \
    -v "$tmp/aether:/aether:ro" -v "$tmp/rexxae:/rexxae:ro" \
    -v "$tmp/tools:/tools:ro" -v "$tmp/project:/project:ro" -v "$tmp/scripts:/scripts:ro" \
    -v "$tmp/in_container.sh:/in_container.sh:ro" \
    -v "$tmp/gen:/gen:ro" -v "$tmp/out:/out" \
    "$IMAGE" bash /in_container.sh $args > "$tmp/engine.log" 2>&1 ||
    { bad "the $ENGINE run of $IMAGE failed"; tail -20 "$tmp/engine.log"; }
echo "qemu:    $(cat "$tmp/out/qemu.version" 2>/dev/null)"

check() { # name, line it must print
    name="$1"; must="$2"; log="$tmp/out/$name.log"
    finish="Returned from app_main"
    harness_ok=1
    case "$name" in runner-http|runner-signed|runner-signed-psk|psk-probe)
        finish="HTTP CHECKS COMPLETE"
        [ "$(cat "$tmp/out/$name.harness-status" 2>/dev/null)" = 0 ] || harness_ok=0 ;;
    esac
    if [ ! -f "$log" ]; then bad "$name: no console log"; return; fi
    out="$(tr -d '\r' < "$log" 2>/dev/null | sed -n '/Calling app_main/,$p')"
    if [ "$(head -1 "$log" 2>/dev/null)" = "BUILD FAILED" ]; then
        bad "$name: the ESP-IDF build failed (${ESP32_KEEP:-$tmp}/out/$name.build.log)"
        grep -E 'error:|undefined reference|overflow|does not fit' "$tmp/out/$name.build.log" | head -10
    elif [ "$harness_ok" != 1 ]; then
        bad "$name: integration harness failed or did not report completion"
        tail -20 "$log"
    elif printf '%s\n' "$out" | grep -qxF "$must" &&
         ! printf '%s\n' "$out" | grep -qE 'Guru Meditation|abort\(\) was called|^REXXAE LANE FAILED$|^RUNNER LANE FAILED$' &&
         printf '%s\n' "$out" | grep -qF "$finish"; then
        size="$(grep -o 'aether_s3.bin binary size 0x[0-9a-f]*' "$tmp/out/$name.build.log" | awk '{print $4}')"
        ok "$name runs on the emulated $TARGET ($(printf '%d' "$size") bytes of firmware): $must"
    elif [ -z "$out" ]; then
        bad "$name did not reach app_main"
        tr -d '\r' < "$log" 2>/dev/null | tail -25 | sed 's/^/    |/'
    else
        bad "$name did not print \"$must\" and return from app_main"
        printf '%s\n' "$out" | tail -25 | sed 's/^/    |/'
    fi
}
for p in $want; do
    case "$p" in
    runner-cyd-usb)
        check "$p" "SIGNED USB provisioning missing or invalid"
        [ -s "$tmp/out/$p.flash.bin" ] || bad "$p hardware image was not retained" ;;
    runner-cyd-wifi)
        check "$p" "SIGNED CYD Wi-Fi provisioning missing or invalid"
        [ -s "$tmp/out/$p.flash.bin" ] || bad "$p hardware image was not retained" ;;
    psk-probe) check psk-probe "PSK PROBE OK" ;;
    hello) check hello "Hello, Aether!" ;;
    runner-signed|runner-signed-psk)
        check "$p" "SIGNED RUNNER LANE OK"
        [ ! -f "$tmp/out/$p.log" ] || tr -d '\r' < "$tmp/out/$p.log" | grep -E '^(SIGNED|HTTP|RUNNER|WATCH|FLEET|alpha:|beta:)' ;;
    runner-http)
        check runner-http "HTTP RUNNER LANE OK"
        tr -d '\r' < "$tmp/out/runner-http.log" | grep -E '^(HTTP|RUNNER)' ;;
    runner)
        if { [ "${RUNNER_CYD_CONSOLE:-}" = 1 ] || [ "${RUNNER_TOIT_IO:-}" = 1 ] ||
             [ -n "${RUNNER_TOIT_WAKE:-}" ]; } &&
           [ "$(cat "$tmp/out/runner.log" 2>/dev/null)" = "HARDWARE IMAGE BUILT" ] &&
           [ -s "$tmp/out/runner.flash.bin" ]; then
            ok "runner hardware example image built"
        else
            check runner "RUNNER LANE OK"
            if [ -f "$tmp/out/runner.log" ]; then
                serial="$(tr -d '\r' < "$tmp/out/runner.log")"
                if printf '%s\n' "$serial" | grep -q '^RUNNER Toit I2C_PROBE PASS$'; then
                    for expected in '  address 60' '  address 104' 'Found: 2 devices'; do
                        printf '%s\n' "$serial" | grep -qxF "RUNNER> $expected" ||
                            bad "runner: missing I2C fixture output $expected"
                    done
                fi
                printf '%s\n' "$serial" | grep '^RUNNER' || :
            fi
        fi ;;
    rexxae)
        check rexxae "REXXAE LANE OK"
        [ -f "$tmp/out/rexxae.log" ] || continue
        tr -d '\r' < "$tmp/out/rexxae.log" 2>/dev/null | grep -E '^(REXX|HOST|demo:|bench:)' | sed 's/^/    /'
        tr -d '\r' < "$tmp/out/rexxae.log" 2>/dev/null | grep '^MEASURE' | sed 's/^MEASURE /    /' ;;
    esac
done
# What the boot reported about PSRAM (the emulator either finds it or not).
for p in $want; do
    [ -f "$tmp/out/$p.log" ] || continue
    tr -d '\r' < "$tmp/out/$p.log" 2>/dev/null | grep -iE 'psram|spiram' | head -4 | sed "s/^/    [$p boot] /"
done

echo ""
echo "$TARGET: $pass passed, $fail failed  (logs: ${ESP32_KEEP:-$tmp}/out)"
[ "$fail" -eq 0 ] && [ "$pass" -gt 0 ]
