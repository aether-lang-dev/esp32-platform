/* Copyright (C) 2023 Toitware ApS.
 * SPDX-License-Identifier: 0BSD
 * Adapted from Toit examples/ethernet/full/client.toit.
 */
#include <assert.h>
#include <stdio.h>
#include "esp_event.h"
#include "ethernet_example.h"
#include "freertos/event_groups.h"

static void got_ip(void *arg, esp_event_base_t base, int32_t event, void *data) {
    (void)base;
    (void)event;
    const ip_event_got_ip_t *address = data;
    printf("Ethernet address " IPSTR "\n", IP2STR(&address->ip_info.ip));
    xEventGroupSetBits(arg, 1);
}

void ethernet_client_use(EthernetProvider *provider) {
    EventGroupHandle_t event = xEventGroupCreate();
    assert(event);
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip, event));
    ESP_ERROR_CHECK(esp_eth_start(provider->board.driver));
    if (!(xEventGroupWaitBits(event, 1, pdFALSE, pdTRUE, pdMS_TO_TICKS(20000)) & 1)) {
        puts("No Ethernet DHCP address within 20 seconds");
    }
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip));
    ESP_ERROR_CHECK(esp_eth_stop(provider->board.driver));
    vEventGroupDelete(event);
}
