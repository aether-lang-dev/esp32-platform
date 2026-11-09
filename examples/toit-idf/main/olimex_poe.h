#ifndef OLIMEX_POE_H
#define OLIMEX_POE_H
#include "esp_eth.h"

typedef struct {
    esp_eth_mac_t *mac;
    esp_eth_phy_t *phy;
    esp_eth_handle_t driver;
} OlimexPoe;

void olimex_open(OlimexPoe *board);
void olimex_close(OlimexPoe *board);
#endif
