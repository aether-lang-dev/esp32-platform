/* Signed bundle delivery over the FNK0114B USB UART. */
#include "cyd_console.h"
#include "cyd_touch.h"
#include "runner_store.h"
#include "runner_peer.h"
#include "runner_switch.h"
#include "runner_settings.h"
#include "driver/uart.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { USB_UART = UART_NUM_0, HEADER_LIMIT = 32, RX_BUFFER_SIZE = 2048 };
static unsigned char trust[138];
static RunnerTrustedKey key;
extern const char labels_service[] asm("_binary_labels_rexx_start");
static unsigned char *labels_bytes;
static const char *labels_source;

const char *runner_labels_source(void) { return labels_source; }

static void mount_labels(unsigned char *data, const RunnerBundle *bundle) {
    ((unsigned char *)bundle->source)[bundle->source_length] = 0;
    free(labels_bytes);
    labels_bytes = data;
    labels_source = (const char *)bundle->source;
}

static bool restore_labels(void) {
    if (runner_store_init_at(RUNNER_STORE_LABELS) != 0) return false;
    RunnerStoreInfo info;
    runner_store_info_at(RUNNER_STORE_LABELS, &info);
    if (!info.active_version) {
        labels_source = labels_service;
        return true;
    }
    RunnerBundle bundle;
    unsigned char *data = runner_store_load_active_at(RUNNER_STORE_LABELS, &key, 1, &bundle);
    if (!data) return false;
    mount_labels(data, &bundle);
    return true;
}

static bool load_trust(void) {
    const esp_partition_t *slot = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x43, "usb_trust");
    if (!slot || esp_partition_read(slot, 0, trust, sizeof trust) != ESP_OK ||
        memcmp(trust, "RXTRST01", 8) || trust[72] || trust[73] != 4) return false;
    const char *id = (const char *)trust + 8;
    if (!*id || strspn(id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != strlen(id)) {
        return false;
    }
    key = (RunnerTrustedKey){id, trust + 73};
    return true;
}

static void report(RunnerStoreId id, const char *kind, const char *outcome, int exit_code) {
    RunnerStoreInfo info;
    runner_store_info_at(id, &info);
    printf("RX %s%s {\"outcome\":\"%s\",\"exitCode\":%d,\"activeVersion\":%" PRIu32
           ",\"highestVersion\":%" PRIu32 ",\"trialVersion\":%" PRIu32
           ",\"storeFault\":%s}\n", id == RUNNER_STORE_LABELS ? "peer-" : "", kind, outcome, exit_code, info.active_version,
           info.highest_version, info.trial_version, info.fault ? "true" : "false");
    fflush(stdout);
}

static int run_bundle(const RunnerBundle *bundle, const CydTouch *tap) {
    ((unsigned char *)bundle->source)[bundle->source_length] = 0;
    RunnerStats stats;
    return runner_execute_touch((const char *)bundle->source, bundle->grants, &stats, tap, bundle->version);
}

static bool run_active(const CydTouch *tap, bool timeout) {
    RunnerStoreInfo info;
    runner_store_info(&info);
    if (!info.active_version) return true;
    RunnerBundle bundle;
    unsigned char *data = runner_store_load_active(&key, 1, &bundle);
    if (!data) return false;
    int result = 0;
    if (timeout ? (bundle.grants & RUNNER_GRANT_SWITCH) : (!tap || (bundle.grants & RUNNER_GRANT_TOUCH))) {
        result = run_bundle(&bundle, tap);
        printf("SIGNED USB %s v%" PRIu32 " exit=%d\n", timeout ? "switch timer" : tap ? "tap" : "restored", bundle.version, result);
        fflush(stdout);
    }
    free(data);
    return result == 0;
}

static bool receive(unsigned char *data, size_t length) {
    int64_t deadline = esp_timer_get_time() + 5000000;
    while (length && esp_timer_get_time() < deadline) {
        int count = uart_read_bytes(USB_UART, data, length, pdMS_TO_TICKS(50));
        if (count < 0) return false;
        data += count;
        length -= (size_t)count;
    }
    return length == 0;
}

static void upload(RunnerStoreId id, size_t length) {
    unsigned char *data = malloc(length + 1);
    if (!data) {
        report(id, "result", "busy", -1);
        return;
    }
    puts(id == RUNNER_STORE_LABELS ? "RX peer-ready" : "RX ready");
    fflush(stdout);
    if (!receive(data, length)) {
        uart_flush_input(USB_UART);
        report(id, "result", "incomplete", -1);
    } else {
        RunnerBundle bundle;
        if (runner_store_stage_at(id, data, length, &key, 1, &bundle) != 0) {
            report(id, "result", "rejected", -1);
        } else if (id == RUNNER_STORE_LABELS) {
            ((unsigned char *)bundle.source)[bundle.source_length] = 0;
            bool healthy = runner_peer_check((const char *)bundle.source);
            if (runner_store_finish_at(id, healthy) != 0) {
                labels_source = NULL;
                report(id, "result", "store-error", -1);
            } else if (healthy) {
                mount_labels(data, &bundle);
                data = NULL;
                report(id, "result", "activated", 0);
            } else {
                report(id, "result", "rolled-back", -1);
            }
        } else {
            /* Every admitted replacement starts from a safe output state. */
            bool safe = !runner_switch_on() || runner_switch_finish(0, true);
            int result = safe ? run_bundle(&bundle, NULL) : -1;
            if (runner_store_finish(result == 0) != 0) {
                report(id, "result", "store-error", -1);
            } else if (result != 0) {
                bool restored = run_active(NULL, false);
                report(id, "result", restored ? "rolled-back" : "restore-error", result);
            } else {
                report(id, "result", "activated", 0);
            }
        }
    }
    free(data);
}

static void command(const char *line) {
    RunnerStoreId id = RUNNER_STORE_APP;
    const char *verb = "";
    if (!strncmp(line, "RXPEER", 6)) {
        id = RUNNER_STORE_LABELS;
        verb = line + 6;
    } else if (!strncmp(line, "RX", 2)) {
        verb = line + 2;
    }
    if (!strcmp(verb, "STATUS")) {
        report(id, "status", "idle", 0);
        return;
    }
    if (!strncmp(verb, "PUT ", 4)) {
        const char *digits = verb + 4;
        size_t count = strspn(digits, "0123456789");
        if (count && count <= 5 && !digits[count]) {
            unsigned long length = strtoul(digits, NULL, 10);
            if (length > 0 && length <= RUNNER_BUNDLE_LIMIT) {
                upload(id, length);
                return;
            }
        }
    }
    report(id, "result", "invalid-command", -1);
}

void app_main(void) {
    if (!runner_switch_init()) puts("SIGNED USB switch output unavailable");
    if (!load_trust()) {
        puts("SIGNED USB provisioning missing or invalid");
        return;
    }
    if (runner_store_init() != 0) {
        puts("SIGNED USB store unavailable");
        return;
    }
    if (uart_driver_install(USB_UART, RX_BUFFER_SIZE, 0, 0, NULL, 0) != ESP_OK) {
        puts("SIGNED USB UART unavailable");
        return;
    }
    if (!restore_labels()) puts("SIGNED USB LABELS peer unavailable");
    (void)cyd_console_init();
    cyd_console_line("RexxAE signed USB");
    bool touch_ready = cyd_touch_init();
    puts(touch_ready ? "SIGNED USB touch ready" : "SIGNED USB touch unavailable");
    if (!runner_settings_init()) puts("SIGNED USB settings unavailable");
    if (!run_active(NULL, false)) puts("SIGNED USB active release could not be restored");
    puts("SIGNED USB ready");
    fflush(stdout);

    char line[HEADER_LIMIT + 1];
    size_t used = 0;
    bool discard = false;
    for (;;) {
        if (runner_switch_take_timeout()) {
            puts(runner_switch_available() ? "SIGNED USB SWITCH OFF (timer)" : "SIGNED USB SWITCH output fault");
            (void)run_active(NULL, true);
        }
        unsigned char byte;
        if (uart_read_bytes(USB_UART, &byte, 1, pdMS_TO_TICKS(20)) != 1) {
            CydTouch tap;
            if (cyd_touch_take(&tap)) (void)run_active(&tap, false);
            continue;
        }
        if (byte == '\n') {
            if (discard) report(RUNNER_STORE_APP, "result", "invalid-command", -1);
            else if (used) {
                line[used] = 0;
                command(line);
            }
            used = 0;
            discard = false;
        } else if (byte < ' ' || byte > '~' || used == HEADER_LIMIT) {
            discard = true;
        } else if (!discard) {
            line[used++] = (char)byte;
        }
    }
}
