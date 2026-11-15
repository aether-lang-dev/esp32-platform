/* Standalone Toit Wi-Fi scan adapter. */
#include "runner_peripherals.h"
#include <stdio.h>

#define AP_LIMIT 8

#ifdef RUNNER_TOIT_WIFI
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static wifi_ap_record_t records[AP_LIMIT];
static uint16_t record_count;

bool demo_wifi_scan_open(void) {
    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK) return false;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    config.nvs_enable = false;
    bool initialized = esp_wifi_init(&config) == ESP_OK;
    bool started = initialized && esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK &&
                   esp_wifi_start() == ESP_OK;
    bool success = false;
    if (started) {
        wifi_scan_config_t scan = {
            .scan_type = WIFI_SCAN_TYPE_PASSIVE,
            .scan_time.passive = 120,
            .channel_bitmap.ghz_2_channels = 0xfe,
        };
        record_count = AP_LIMIT;
        success = esp_wifi_scan_start(&scan, true) == ESP_OK &&
                  esp_wifi_scan_get_ap_records(&record_count, records) == ESP_OK;
        if (!success) esp_wifi_clear_ap_list();
        success &= esp_wifi_stop() == ESP_OK;
    }
    if (initialized) success &= esp_wifi_deinit() == ESP_OK;
    success &= esp_event_loop_delete_default() == ESP_OK;
    return success;
}

void demo_wifi_scan_row(unsigned index, char *reply, unsigned reply_size) {
    if (index >= record_count) {
        snprintf(reply, reply_size, "\"\"");
        return;
    }
    const wifi_ap_record_t *ap = &records[index];
    char ssid[33];
    unsigned i = 0;
    for (; i < sizeof ap->ssid - 1 && ap->ssid[i]; ++i) {
        unsigned char ch = ap->ssid[i];
        ssid[i] = ch >= ' ' && ch <= '~' && ch != '"' && ch != '\\' ? (char)ch : '?';
    }
    ssid[i] = 0;
    snprintf(reply, reply_size,
             "{\"ssid\":\"%s\",\"bssid\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
             "\"rssi\":%d,\"channel\":%u,\"auth\":%u}",
             ssid, (unsigned)ap->bssid[0], (unsigned)ap->bssid[1], (unsigned)ap->bssid[2],
             (unsigned)ap->bssid[3], (unsigned)ap->bssid[4], (unsigned)ap->bssid[5],
             ap->rssi, (unsigned)ap->primary, (unsigned)ap->authmode);
}
#else
bool demo_wifi_scan_open(void) {
    return true;
}

void demo_wifi_scan_row(unsigned index, char *reply, unsigned reply_size) {
    static const char *const rows[] = {
        "{\"ssid\":\"test-ap\",\"bssid\":\"02:00:00:00:00:01\",\"rssi\":-44,\"channel\":1,\"auth\":3}",
        "{\"ssid\":\"lab-guest\",\"bssid\":\"02:00:00:00:00:02\",\"rssi\":-69,\"channel\":6,\"auth\":0}",
    };
    snprintf(reply, reply_size, "%s", index < sizeof rows / sizeof rows[0] ? rows[index] : "\"\"");
}
#endif
