/* Station provisioning for the signed CYD firmware. */
#include "runner_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_partition.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>

enum { READY = 1, FAILED = 2, RETRIES = 4 };
static EventGroupHandle_t events;
static unsigned retries;
static wifi_config_t station;
static char key_id[65];
static unsigned char public_key[65];
RunnerTrustedKey wifi_trusted_keys[1] = {{key_id, public_key}};

static bool identifier(const unsigned char *name) {
    if (!*name) return false;
    for (; *name; ++name) {
        unsigned char c = *name;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

bool runner_wifi_load(void) {
    /* Explicit byte fields keep the flash record independent of C struct layout. */
    enum { SSID = 10, PASSWORD = SSID + 32, KEY_ID = PASSWORD + 63, KEY = KEY_ID + 65 };
    unsigned char record[KEY + 65];
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x42, "device_wifi");
    if (!partition || esp_partition_read(partition, 0, record, sizeof record) != ESP_OK ||
        memcmp(record, "RXWIFI01", 8)) return false;
    unsigned ssid_length = record[8], password_length = record[9];
    if (!ssid_length || ssid_length > sizeof station.sta.ssid ||
        password_length < 8 || password_length > 63 ||
        memchr(record + SSID, 0, ssid_length) || record[KEY_ID + 64] ||
        !identifier(record + KEY_ID) || record[KEY] != 4) return false;
    for (unsigned i = 0; i < password_length; ++i) {
        unsigned char c = record[PASSWORD + i];
        if (c < 32 || c > 126) return false;
    }
    memcpy(station.sta.ssid, record + SSID, ssid_length);
    memcpy(station.sta.password, record + PASSWORD, password_length);
    memcpy(key_id, record + KEY_ID, sizeof key_id);
    memcpy(public_key, record + KEY, sizeof public_key);
    station.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    return true;
}

static void station_event(void *arg, esp_event_base_t base, int32_t event, void *data) {
    (void)arg;
    if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *address = data;
        retries = 0;
        printf("SIGNED CYD Wi-Fi address " IPSTR "\n", IP2STR(&address->ip_info.ip));
        xEventGroupSetBits(events, READY);
    } else if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        if (retries++ >= RETRIES || esp_wifi_connect() != ESP_OK) xEventGroupSetBits(events, FAILED);
    }
}

bool runner_wifi_start(void) {
    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK ||
        !esp_netif_create_default_wifi_sta()) return false;
    events = xEventGroupCreate();
    if (!events) return false;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    config.nvs_enable = false;
    if (esp_wifi_init(&config) != ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, station_event, NULL) != ESP_OK ||
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, station_event, NULL) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA, &station) != ESP_OK ||
        esp_wifi_start() != ESP_OK) return false;
    if (esp_wifi_connect() == ESP_OK &&
        (xEventGroupWaitBits(events, READY | FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000)) & READY)) {
        return true;
    }
    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, station_event);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, station_event);
    esp_wifi_stop();
    return false;
}
