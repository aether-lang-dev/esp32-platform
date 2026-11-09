#!/bin/bash
# Runs inside espressif/idf. Each argument is <name>[:<host>]. Build the
# generated C and optional C host using the ESP32_TARGET configuration,
# boot in Espressif QEMU, and save build/size/console output under /out.
# QEMU stops when app_main returns, a panic is printed, or QEMU_TIMEOUT elapses.
set -eu
. "$IDF_PATH/export.sh" > /dev/null 2>&1
Q=$(ls -d "$IDF_TOOLS_PATH"/tools/qemu-xtensa/*/qemu/bin | head -1)
"$Q/qemu-system-xtensa" --version | head -1 > /out/qemu.version
# Build dirs live in the container; sdkconfig is regenerated per run from
# sdkconfig.defaults, so the mounted project stays read-only.
cp -r /project /tmp/project
cd /tmp/project
case "${ESP32_TARGET:-esp32s3}" in
    esp32s3) chip=esp32s3; flash=16MB; psram=yes ;;
    esp32s3-no-psram) chip=esp32s3; flash=16MB; psram=no ;;
    esp32) chip=esp32; flash=4MB; psram=no ;;
    *) echo "Unknown ESP32_TARGET" >&2; exit 1 ;;
esac
cp "sdkconfig.$ESP32_TARGET" sdkconfig.defaults
for arg in "$@"; do
    p="${arg%%:*}"
    host=""
    [ "$arg" != "$p" ] && host="${arg#*:}"
    cp "sdkconfig.$ESP32_TARGET" sdkconfig.defaults
    signed=no
    psk=no
    case "$p" in runner-signed|runner-signed-psk|runner-cyd-wifi) signed=yes ;; esac
    case "$p" in psk-probe|runner-signed-psk|runner-cyd-wifi) psk=yes ;; esac
    if [ "$p" = runner ] || [ "$p" = runner-cyd-usb ] || [ "$p" = runner-http ] || [ "$signed" = yes ] || [ "$p" = psk-probe ]; then
        cat >> sdkconfig.defaults <<'CONFIG'
# CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE is not set
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.runner.csv"
CONFIG
    fi
    if [ "$p" != runner-cyd-wifi ] &&
       { [ "$p" = runner-http ] || [ "$signed" = yes ] || [ "$p" = psk-probe ]; }; then
        cat >> sdkconfig.defaults <<'CONFIG'
CONFIG_ETH_USE_OPENETH=y
CONFIG_ETH_USE_ESP32_EMAC=n
CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024
CONFIG
    fi
    if [ "$p" = runner-signed-psk ] &&
       ! "${REXX_TEST_PYTHON:-python3}" -c 'import ssl; raise SystemExit(not getattr(ssl, "HAS_PSK", False))'; then
        echo "PSK test needs the PSK-capable Python image described in README.md" > "/out/$p.log"
        echo 1 > "/out/$p.harness-status"
        continue
    fi
    if [ "$signed" = yes ] && [ "$p" != runner-cyd-wifi ]; then
        python3 /tools/make_signed_fixtures.py /tmp/signed-fixtures /tmp/project/main/trusted_test_keys.h
    fi
    if [ "$p" = runner-cyd-usb ]; then
        cp partitions.runner.csv partitions.usb.csv
        printf '%s\n' 'usb_trust, data, 0x43, 0x332000, 0x1000' \
            'labels_a, data, 0x40, 0x333000, 0x3000' \
            'labels_b, data, 0x40, 0x336000, 0x3000' >> partitions.usb.csv
        sed -i 's/partitions.runner.csv/partitions.usb.csv/' sdkconfig.defaults
    fi
    if [ "$psk" = yes ]; then
        cp partitions.runner.csv partitions.psk.csv
        printf '%s\n' 'device_key, data, 0x41, 0x330000, 0x1000' >> partitions.psk.csv
        if [ "$p" = runner-cyd-wifi ]; then
            printf '%s\n' 'device_wifi, data, 0x42, 0x331000, 0x1000' >> partitions.psk.csv
        fi
        sed -i 's/partitions.runner.csv/partitions.psk.csv/' sdkconfig.defaults
        cat >> sdkconfig.defaults <<'CONFIG'
CONFIG_ESP_TLS_PSK_VERIFICATION=y
CONFIG_MBEDTLS_SSL_PROTO_TLS1_3=y
# CONFIG_MBEDTLS_SSL_TLS1_3_KEXM_PSK is not set
CONFIG_MBEDTLS_SSL_TLS1_3_KEXM_PSK_EPHEMERAL=y
CONFIG
    fi
    rm -rf build sdkconfig
    if ! idf.py -DIDF_TARGET="$chip" -DAE_PROG="$p" -DAE_HOST="$host" \
            build > "/out/$p.build.log" 2>&1; then
        echo "BUILD FAILED" > "/out/$p.log"
        continue
    fi
    idf.py size > "/out/$p.size.log" 2>&1
    idf.py size-components > "/out/$p.size-components.log" 2>&1
    idf.py size-files > "/out/$p.size-files.log" 2>&1
    cp build/aether_s3.elf "/out/$p.elf" 2>/dev/null
    # Development provisioning only: the source is placed in the data slot,
    # independently of the application ELF, and read through esp_partition.
    if [ "$p" != runner-cyd-wifi ] &&
       { [ "$p" = runner ] || [ "$p" = runner-http ] || [ "$signed" = yes ]; }; then
        cp /scripts/runner.rexx build/runner.script
        printf '\000' >> build/runner.script
        printf '\n0x310000 runner.script\n' >> build/flash_args
    fi
    if [ "$psk" = yes ] && [ "$p" != runner-cyd-wifi ]; then
        python3 /tools/rexx_psk.py build/device.json build/device.psk --generate device-test
        printf '\n0x330000 device.psk\n' >> build/flash_args
    fi
    (cd build && esptool.py --chip "$chip" merge_bin --fill-flash-size "$flash" \
        -o flash.bin @flash_args > /dev/null 2>&1)
    case "$p" in runner-cyd-wifi|runner-cyd-usb)
        cp build/flash.bin "/out/$p.flash.bin" ;;
    esac
    if [ "$p" = runner ] && { [ "${RUNNER_CYD_CONSOLE:-}" = 1 ] ||
       [ "${RUNNER_TOIT_IO:-}" = 1 ] || [ -n "${RUNNER_TOIT_WAKE:-}" ]; }; then
        cp build/flash.bin /out/runner.flash.bin
        printf 'HARDWARE IMAGE BUILT\n' > /out/runner.log
        continue
    fi
    if [ "$p" = runner ] && { [ -n "${RUNNER_LED_GPIO:-}" ] || [ "${RUNNER_BOARD_SMOKE:-}" = 1 ]; }; then
        cp build/flash.bin /out/runner.flash.bin
    fi
    if [ "$p" = psk-probe ]; then
        if python3 /project/test_psk_probe.py "$Q/qemu-system-xtensa" build/flash.bin build/device.json > "/out/$p.log" 2>&1; then
            echo 0 > "/out/$p.harness-status"
        else
            echo "$?" > "/out/$p.harness-status"
        fi
        continue
    fi
    # kill-ok: timeout runs only inside the Linux espressif/idf container
    set -- -machine "$chip"
    if [ "$psram" = yes ]; then
        set -- "$@" -m "${QEMU_PSRAM:-8M}" -global ssi_psram.is_octal=true
    fi
    if [ "$p" != runner-cyd-wifi ] && { [ "$p" = runner-http ] || [ "$signed" = yes ]; }; then
        set --
        [ "$signed" = no ] || set -- /tmp/signed-fixtures
        [ "$p" != runner-signed-psk ] || export REXX_TEST_PSK="$PWD/build/device.json"
        if [ "$p" = runner-signed-psk ]; then cp build/flash.bin build/fleet.bin; fi
        if { "${REXX_TEST_PYTHON:-python3}" /project/test_http_delivery.py "$Q/qemu-system-xtensa" build/flash.bin "$@" &&
             { [ "$p" != runner-signed-psk ] ||
               "${REXX_TEST_PYTHON:-python3}" /project/test_fleet_sync.py "$Q/qemu-system-xtensa" build/fleet.bin /tmp/signed-fixtures/fleet-current.pem; }; } \
                > "/out/$p.log" 2>&1; then
            echo 0 > "/out/$p.harness-status"
        else
            echo "$?" > "/out/$p.harness-status"
        fi
        unset REXX_TEST_PSK
        continue
    fi
    # Python reads UART lines immediately and terminates QEMU after the return
    # marker. awk in the image block-buffers its input and used to stall here.
    python3 - "$Q/qemu-system-xtensa" "$@" \
        -drive file=build/flash.bin,if=mtd,format=raw -nic none \
        > "/out/$p.log" 2>&1 <<'PYTHON' || true
import os
import subprocess
import sys

command = ["timeout", os.environ.get("QEMU_TIMEOUT", "120"), sys.argv[1], "-nographic", *sys.argv[2:]]
process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
try:
    for line in process.stdout:
        print(line, end="", flush=True)
        if any(marker in line for marker in ("Returned from app_main", "Guru Meditation", "abort() was called")):
            break
finally:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
PYTHON
done
