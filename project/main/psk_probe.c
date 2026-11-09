/* TLS 1.3 external PSK smoke test using ESP-TLS and the IDF HTTP server. */
#include "runner_psk.h"
#include "esp_eth_driver.h"
#include "esp_eth_netif_glue.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include <stdio.h>

static unsigned hits;

static esp_err_t status_get(httpd_req_t *req) {
    char body[32];
    snprintf(body, sizeof body, "{\"hits\":%u}", ++hits);
    printf("PSK authenticated status %u\n", hits);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t result = httpd_resp_sendstr(req, body);
    esp_err_t closed = httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
    return result == ESP_OK ? closed : result;
}

void app_main(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_config_t net_config = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&net_config);
    if (!netif) abort();
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = -1;
    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_config);
    if (!mac || !phy) abort();
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t ethernet = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &ethernet));
    uint8_t address[] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};
    ESP_ERROR_CHECK(esp_eth_ioctl(ethernet, ETH_CMD_S_MAC_ADDR, address));
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_eth_new_netif_glue(ethernet)));
    ESP_ERROR_CHECK(esp_eth_start(ethernet));
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    if (runner_psk_configure(&http_config) != ESP_OK) {
        puts("PSK device is unprovisioned or credential is invalid");
        return;
    }
    http_config.recv_wait_timeout = 2;
    http_config.send_wait_timeout = 2;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &http_config));
    const httpd_uri_t status = {.uri = "/status", .method = HTTP_GET, .handler = status_get};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status));
    puts("PSK status endpoint ready");
}
