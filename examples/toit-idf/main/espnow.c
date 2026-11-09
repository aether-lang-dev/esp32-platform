/* Copyright (C) 2022 Toitware ApS.
 * SPDX-License-Identifier: 0BSD
 * Adapted from Toit examples/espnow.toit.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const uint8_t broadcast[] = {255, 255, 255, 255, 255, 255};
static QueueHandle_t inbox;

typedef struct {
    uint8_t sender[6];
    char text[48];
} Message;

static void receive(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
    if (length <= 0 || (size_t)length >= sizeof ((Message *)0)->text) return;
    Message message = {0};
    memcpy(message.sender, info->src_addr, sizeof message.sender);
    for (int i = 0; i < length; ++i) {
        message.text[i] = data[i] >= ' ' && data[i] <= '~' ? (char)data[i] : '?';
    }
    xQueueSend(inbox, &message, 0);
}

void app_main(void) {
    inbox = xQueueCreate(8, sizeof(Message));
    assert(inbox);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    config.nvs_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(receive));
    esp_now_peer_info_t peer = {.channel = 1, .ifidx = WIFI_IF_STA};
    memcpy(peer.peer_addr, broadcast, sizeof broadcast);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    for (unsigned count = 0;; ++count) {
        char line[32];
        int length = snprintf(line, sizeof line, "hello %u", count);
        ESP_ERROR_CHECK(esp_now_send(broadcast, (const uint8_t *)line, length));
        printf("Send datagram: %s\n", line);
        TickType_t remaining = pdMS_TO_TICKS(1000);
        TimeOut_t timeout;
        vTaskSetTimeOutState(&timeout);
        Message message;
        while (xQueueReceive(inbox, &message, remaining)) {
            printf("Receive datagram from %02x:%02x:%02x:%02x:%02x:%02x: %s\n",
                   message.sender[0], message.sender[1], message.sender[2],
                   message.sender[3], message.sender[4], message.sender[5], message.text);
            if (xTaskCheckForTimeOut(&timeout, &remaining)) break;
        }
    }
}
