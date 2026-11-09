/* Copyright (C) 2023 Toitware ApS.
 * SPDX-License-Identifier: 0BSD
 * Adapted from Toit examples/ethernet/full/provider.toit.
 */
#include <assert.h>
#include "ethernet_example.h"

void ethernet_provider_install(EthernetProvider *provider) {
    olimex_open(&provider->board);
    esp_netif_config_t config = ESP_NETIF_DEFAULT_ETH();
    provider->netif = esp_netif_new(&config);
    assert(provider->netif);
    provider->glue = esp_eth_new_netif_glue(provider->board.driver);
    assert(provider->glue);
    ESP_ERROR_CHECK(esp_netif_attach(provider->netif, provider->glue));
}

void ethernet_provider_uninstall(EthernetProvider *provider) {
    ESP_ERROR_CHECK(esp_eth_del_netif_glue(provider->glue));
    esp_netif_destroy(provider->netif);
    olimex_close(&provider->board);
}
