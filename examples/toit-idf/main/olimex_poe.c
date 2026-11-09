/* Copyright (C) 2023 Toitware ApS.
 * SPDX-License-Identifier: 0BSD
 * Adapted from Toit examples/ethernet/full/olimex_poe.toit.
 */
#include <assert.h>
#include "driver/gpio.h"
#include "esp_eth_mac_esp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "olimex_poe.h"

#define PHY_POWER GPIO_NUM_12

void olimex_open(OlimexPoe *board) {
    eth_mac_config_t mac = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy = ETH_PHY_DEFAULT_CONFIG();
    phy.reset_gpio_num = -1;
#ifdef TOIT_ethernet_qemu
    board->mac = esp_eth_mac_new_openeth(&mac);
#else
    ESP_ERROR_CHECK(gpio_set_direction(PHY_POWER, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(PHY_POWER, 1));
    vTaskDelay(pdMS_TO_TICKS(100));
    eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac.smi_gpio.mdc_num = 23;
    emac.smi_gpio.mdio_num = 18;
    emac.clock_config.rmii.clock_mode = EMAC_CLK_OUT;
    emac.clock_config.rmii.clock_gpio = 17;
    phy.phy_addr = 0;
    board->mac = esp_eth_mac_new_esp32(&emac, &mac);
#endif
    board->phy = esp_eth_phy_new_generic(&phy);
    assert(board->mac && board->phy);
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(board->mac, board->phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &board->driver));
#ifdef TOIT_ethernet_qemu
    uint8_t address[] = {2, 0, 0, 0x12, 0x34, 0x56};
    ESP_ERROR_CHECK(esp_eth_ioctl(board->driver, ETH_CMD_S_MAC_ADDR, address));
#endif
}

void olimex_close(OlimexPoe *board) {
    ESP_ERROR_CHECK(esp_eth_driver_uninstall(board->driver));
    ESP_ERROR_CHECK(board->phy->del(board->phy));
    ESP_ERROR_CHECK(board->mac->del(board->mac));
#ifndef TOIT_ethernet_qemu
    ESP_ERROR_CHECK(gpio_set_level(PHY_POWER, 0));
    ESP_ERROR_CHECK(gpio_reset_pin(PHY_POWER));
#endif
}
