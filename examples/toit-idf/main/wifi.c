/* Copyright (C) 2022 Toitware ApS.
 * SPDX-License-Identifier: 0BSD
 * Adapted from Toit examples/wifi/connect.toit and examples/ntp/ntp.toit.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

static void station_event(void *arg, esp_event_base_t base, int32_t event, void *data) {
    (void)data;
    if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) xEventGroupSetBits(arg, 1);
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) xEventGroupSetBits(arg, 2);
}

#ifdef TOIT_ntp
static int64_t wall_us(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (int64_t)now.tv_sec * 1000000 + now.tv_usec;
}

static void synchronize(void) {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_TOIT_NTP_SERVER);
    int64_t before_wall = wall_us();
    int64_t before_mono = esp_timer_get_time();
    ESP_ERROR_CHECK(esp_netif_sntp_init(&config));
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK) {
        int64_t adjustment = wall_us() - before_wall - (esp_timer_get_time() - before_mono);
        time_t now = time(NULL);
        struct tm utc;
        gmtime_r(&now, &utc);
        char iso[32];
        strftime(iso, sizeof iso, "%FT%TZ", &utc);
        printf("synchronized adjustment_us=%lld time=%s\n", (long long)adjustment, iso);
    } else {
        puts("NTP synchronization timed out");
    }
    esp_netif_sntp_deinit();
}
#endif

void app_main(void) {
    wifi_config_t station = {0};
    size_t ssid_length = strlen(CONFIG_TOIT_WIFI_SSID);
    size_t password_length = strlen(CONFIG_TOIT_WIFI_PASSWORD);
    if (!ssid_length || ssid_length > sizeof station.sta.ssid ||
        password_length > sizeof station.sta.password) {
        puts("Configure a valid station SSID and password before building");
        return;
    }
    memcpy(station.sta.ssid, CONFIG_TOIT_WIFI_SSID, ssid_length);
    memcpy(station.sta.password, CONFIG_TOIT_WIFI_PASSWORD, password_length);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *interface = esp_netif_create_default_wifi_sta();
    assert(interface);
    EventGroupHandle_t events = xEventGroupCreate();
    assert(events);
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, station_event, events));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, station_event, events));
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    if (xEventGroupWaitBits(events, 3, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000)) & 1) {
        wifi_ap_record_t ap;
        ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap));
        printf("SSID=%.*s BSSID=%02x:%02x:%02x:%02x:%02x:%02x RSSI=%d channel=%u auth=%u\n",
               32, (const char *)ap.ssid, ap.bssid[0], ap.bssid[1], ap.bssid[2],
               ap.bssid[3], ap.bssid[4], ap.bssid[5], ap.rssi, ap.primary, ap.authmode);
#ifdef TOIT_ntp
        synchronize();
#endif
    } else {
        puts("Station connection failed or timed out");
    }
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, station_event));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, station_event));
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());
    esp_netif_destroy_default_wifi(interface);
    vEventGroupDelete(events);
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
}
