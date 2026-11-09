/* Copyright (C) 2023 Toitware ApS.
 * SPDX-License-Identifier: 0BSD
 * Adapted from Toit examples/ethernet/simple/main.toit.
 */
#include <stdio.h>
#include "esp_event.h"
#include "ethernet_example.h"

void app_main(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    EthernetProvider provider = {0};
    ethernet_provider_install(&provider);
    ethernet_client_use(&provider);
    ethernet_provider_uninstall(&provider);
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
    puts("Ethernet provider closed");
}
