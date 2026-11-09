/* Copyright (C) 2023 Toitware ApS. (Passive scan and connect.)
 * Copyright (C) 2024 Toitware ApS. (Active scan.)
 * SPDX-License-Identifier: 0BSD
 * Adapted from Toit examples/ble/scan.toit, scan-active.toit and connect.toit.
 */
#include <stdio.h>
#include <string.h>
#include "ble_example.h"

#if !defined(TOIT_ble_scan_active)
static const ble_uuid16_t battery = BLE_UUID16_INIT(0x180f);
#endif

typedef struct {
    ble_addr_t address;
#ifdef TOIT_ble_scan_active
    uint8_t data[62];
    size_t length;
#endif
} Device;

static Device devices[12];
static size_t found;

static void print_address(const ble_addr_t *address) {
    printf("%02x:%02x:%02x:%02x:%02x:%02x", address->val[5], address->val[4], address->val[3],
           address->val[2], address->val[1], address->val[0]);
}

static Device *record(const ble_addr_t *address) {
    for (size_t i = 0; i < found; ++i) {
        if (!ble_addr_cmp(&devices[i].address, address)) return &devices[i];
    }
    if (found == sizeof devices / sizeof devices[0]) return NULL;
    Device *device = &devices[found++];
    device->address = *address;
    return device;
}

#ifdef TOIT_ble_scan_active
static void merge(Device *device, const uint8_t *data, size_t length) {
    while (length && data[0] && (size_t)data[0] + 1 <= length) {
        size_t block = data[0] + 1;
        bool duplicate = false;
        for (size_t i = 0; i < device->length; i += (size_t)device->data[i] + 1) {
            if (device->data[i] == data[0] && !memcmp(device->data + i, data, block)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && block <= sizeof device->data - device->length) {
            memcpy(device->data + device->length, data, block);
            device->length += block;
        }
        data += block;
        length -= block;
    }
}
#else
static bool advertises_battery(const struct ble_gap_disc_desc *report) {
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, report->data, report->length_data)) return false;
    for (unsigned i = 0; i < fields.num_uuids16; ++i) {
        if (!ble_uuid_cmp(&fields.uuids16[i].u, &battery.u)) return true;
    }
    return false;
}
#endif

#ifdef TOIT_ble_connect
static const ble_uuid16_t battery_level = BLE_UUID16_INIT(0x2a19);
static uint16_t svc_start, svc_end, value_handle;
static uint8_t own_address;
static bool selected;

static void disconnect(uint16_t connection, const char *reason) {
    if (reason) puts(reason);
    ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
}

static int read_level(uint16_t connection, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attribute, void *arg) {
    (void)arg;
    uint8_t percent;
    if (error->status || !attribute || OS_MBUF_PKTLEN(attribute->om) != 1 ||
        os_mbuf_copydata(attribute->om, 0, 1, &percent) || percent > 100) {
        disconnect(connection, "Cannot read a valid battery level");
        return 0;
    }
    printf("Battery level of ");
    print_address(&devices[0].address);
    printf(": %u%%\n", percent);
    disconnect(connection, NULL);
    return 0;
}

static int found_characteristic(uint16_t connection, const struct ble_gatt_error *error,
                                const struct ble_gatt_chr *characteristic, void *arg) {
    (void)arg;
    if (!error->status) {
        if (!value_handle) value_handle = characteristic->val_handle;
    } else if (error->status != BLE_HS_EDONE || !value_handle ||
               ble_gattc_read(connection, value_handle, read_level, NULL)) {
        disconnect(connection, "Battery Level characteristic discovery failed");
    }
    return 0;
}

static int found_service(uint16_t connection, const struct ble_gatt_error *error,
                         const struct ble_gatt_svc *service, void *arg) {
    (void)arg;
    if (!error->status) {
        if (!svc_start) {
            svc_start = service->start_handle;
            svc_end = service->end_handle;
        }
    } else if (error->status != BLE_HS_EDONE || !svc_start ||
               ble_gattc_disc_chrs_by_uuid(connection, svc_start, svc_end, &battery_level.u,
                                          found_characteristic, NULL)) {
        disconnect(connection, "Battery Service discovery failed");
    }
    return 0;
}
#endif

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC) {
        const struct ble_gap_disc_desc *report = &event->disc;
#ifdef TOIT_ble_scan_active
        Device *device = record(&report->addr);
        if (device) merge(device, report->data, report->length_data);
#else
        if (!advertises_battery(report)) return 0;
        if (!record(&report->addr)) return 0;
#ifdef TOIT_ble_connect
        if (selected) return 0;
        selected = true;
        if (ble_gap_disc_cancel() ||
            ble_gap_connect(own_address, &devices[0].address, 10000, NULL, gap_event, NULL)) {
            puts("Could not connect to the battery peripheral");
        }
#endif
#endif
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
#ifdef TOIT_ble_connect
        if (!selected) puts("No battery peripheral found");
#else
        for (size_t i = 0; i < found; ++i) {
            print_address(&devices[i].address);
#ifdef TOIT_ble_scan_active
            printf(":");
            for (size_t j = 0; j < devices[i].length; ++j) printf(" %02x", devices[i].data[j]);
#endif
            puts("");
        }
        printf("Found %u BLE devices\n", (unsigned)found);
#endif
    }
#ifdef TOIT_ble_connect
    else if (event->type == BLE_GAP_EVENT_CONNECT) {
        if (event->connect.status) {
            printf("Connection failed: %d\n", event->connect.status);
        } else if (ble_gattc_disc_svc_by_uuid(event->connect.conn_handle, &battery.u, found_service, NULL)) {
            disconnect(event->connect.conn_handle, "Could not start Battery Service discovery");
        }
    }
#endif
    return 0;
}

void example_ble_start(void) {
#ifndef TOIT_ble_connect
    uint8_t own_address;
#endif
    example_check(ble_hs_id_infer_auto(0, &own_address), "BLE address");
    struct ble_gap_disc_params params = {.filter_duplicates = 1};
#ifdef TOIT_ble_scan_active
    int duration = 2000;
#else
    params.passive = 1;
    int duration = 3000;
#endif
    example_check(ble_gap_disc(own_address, duration, &params, gap_event, NULL), "BLE scan");
}
