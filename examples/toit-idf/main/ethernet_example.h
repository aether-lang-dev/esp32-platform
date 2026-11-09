#ifndef ETHERNET_EXAMPLE_H
#define ETHERNET_EXAMPLE_H
#include "esp_eth_netif_glue.h"
#include "esp_netif.h"
#include "olimex_poe.h"

typedef struct {
    OlimexPoe board;
    esp_netif_t *netif;
    esp_eth_netif_glue_handle_t glue;
} EthernetProvider;

void ethernet_provider_install(EthernetProvider *provider);
void ethernet_provider_uninstall(EthernetProvider *provider);
void ethernet_client_use(EthernetProvider *provider);
#endif
