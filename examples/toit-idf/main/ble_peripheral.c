/* Copyright (C) 2023 Toitware ApS. (Advertise.)
 * Copyright (C) 2021 Toitware ApS. (Heart rate.)
 * SPDX-License-Identifier: 0BSD
 * Adapted from Toit examples/ble/advertise.toit and heart_rate.toit.
 */
#include <stdio.h>
#include <string.h>
#include "ble_example.h"
#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static uint8_t own_address;

#ifdef TOIT_ble_heart_rate
static uint16_t subscribed = BLE_HS_CONN_HANDLE_NONE;
static uint16_t notify_handle;
static uint8_t rate = 60;
static struct ble_npl_callout heartbeat;
static const ble_uuid16_t service = BLE_UUID16_INIT(0x1825);
static const ble_uuid128_t notify_uuid = BLE_UUID128_INIT(
    0x0d, 0xe5, 0xa1, 0x7f, 0x68, 0xdd, 0x7c, 0xa9,
    0x85, 0x40, 0x41, 0xac, 0x6e, 0x3c, 0x4b, 0x63);
static const ble_uuid128_t write_uuid = BLE_UUID128_INIT(
    0x0d, 0xe5, 0xa1, 0x7f, 0x68, 0xdd, 0x7c, 0xa9,
    0x85, 0x40, 0x41, 0x1c, 0x6e, 0x3c, 0x4b, 0x63);

static int write_data(uint16_t connection, uint16_t attribute,
                      struct ble_gatt_access_ctxt *context, void *arg) {
    (void)connection;
    (void)attribute;
    (void)arg;
    uint16_t length = OS_MBUF_PKTLEN(context->om);
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
    uint8_t bytes[32];
    if (length > sizeof bytes || os_mbuf_copydata(context->om, 0, length, bytes)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    printf("Heart rate app received data:");
    for (uint16_t i = 0; i < length; ++i) printf(" %02x", bytes[i]);
    puts("");
    return 0;
}

static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service.u,
     .characteristics = (struct ble_gatt_chr_def[]) {
        {.uuid = &notify_uuid.u, .access_cb = write_data,
         .val_handle = &notify_handle, .flags = BLE_GATT_CHR_F_NOTIFY},
        {.uuid = &write_uuid.u, .access_cb = write_data, .flags = BLE_GATT_CHR_F_WRITE},
        {0},
     }},
    {0},
};

static void beat(struct ble_npl_event *event) {
    (void)event;
    if (subscribed != BLE_HS_CONN_HANDLE_NONE) {
        uint8_t payload[] = {0x06, rate};
        struct os_mbuf *packet = ble_hs_mbuf_from_flat(payload, sizeof payload);
        if (packet) ble_gatts_notify_custom(subscribed, notify_handle, packet);
        rate = rate == 129 ? 60 : rate + 1;
    }
    ble_npl_callout_reset(&heartbeat, ble_npl_time_ms_to_ticks32(500));
}
#else
static const ble_uuid16_t service = BLE_UUID16_INIT(0x180f);
static const uint8_t manufacturer[] = {255, 255, 't', 'o', 'i', 't'};
#endif

static void advertise(void);

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
#ifdef TOIT_ble_heart_rate
    if (event->type == BLE_GAP_EVENT_SUBSCRIBE && event->subscribe.attr_handle == notify_handle) {
        subscribed = event->subscribe.cur_notify ? event->subscribe.conn_handle : BLE_HS_CONN_HANDLE_NONE;
    }
    if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        subscribed = BLE_HS_CONN_HANDLE_NONE;
        advertise();
    }
    if (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status) advertise();
#else
    if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) puts("Advertising complete");
#endif
    return 0;
}

static void advertise(void) {
#ifdef TOIT_ble_heart_rate
    const char *name = "Toit heart rate demo";
    int duration = BLE_HS_FOREVER;
#else
    const char *name = "Toit device";
    int duration = 1000000;
#endif
    struct ble_gap_adv_params params = {
#ifdef TOIT_ble_heart_rate
        .conn_mode = BLE_GAP_CONN_MODE_UND,
#else
        .conn_mode = BLE_GAP_CONN_MODE_NON,
#endif
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .name = (const uint8_t *)name, .name_len = strlen(name), .name_is_complete = 1,
        .uuids16 = &service, .num_uuids16 = 1, .uuids16_is_complete = 1,
#ifndef TOIT_ble_heart_rate
        .mfg_data = manufacturer, .mfg_data_len = sizeof manufacturer,
#endif
    };
    example_check(ble_gap_adv_set_fields(&fields), "BLE advertisement fields");
    example_check(ble_gap_adv_start(own_address, NULL, duration, &params, gap_event, NULL), "BLE advertise");
}

void example_ble_start(void) {
    example_check(ble_hs_id_infer_auto(0, &own_address), "BLE address");
    advertise();
#ifdef TOIT_ble_heart_rate
    ble_npl_callout_init(&heartbeat, nimble_port_get_dflt_eventq(), beat, NULL);
    ble_npl_callout_reset(&heartbeat, ble_npl_time_ms_to_ticks32(500));
#endif
}

#ifdef TOIT_ble_heart_rate
void example_ble_prepare(void) {
    ble_svc_gap_init();
    ble_svc_gatt_init();
    example_check(ble_gatts_count_cfg(services), "GATT count");
    example_check(ble_gatts_add_svcs(services), "GATT install");
    example_check(ble_svc_gap_device_name_set("Toit heart rate demo"), "GAP device name");
}
#endif
