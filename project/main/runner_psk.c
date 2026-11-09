#include "runner_psk.h"
#include "bootloader_random.h"
#include "esp_partition.h"
#include "esp_tls.h"
#include <string.h>

static uint8_t key[32];
static char identity[65];
static const psk_hint_key_t psk = {.key = key, .key_size = sizeof key, .hint = identity};
static esp_tls_cfg_server_t tls_config = {
    .psk_hint_key = &psk,
    .tls_version = ESP_TLS_VER_TLS_1_3,
    .tls_handshake_timeout_ms = 3000,
};

static void free_session(void *session) {
    esp_tls_server_session_delete(session);
}

static int secure_send(httpd_handle_t server, int fd, const char *buf, size_t len, int flags) {
    (void)flags;
    return esp_tls_conn_write(httpd_sess_get_transport_ctx(server, fd), buf, len);
}

static int secure_recv(httpd_handle_t server, int fd, char *buf, size_t len, int flags) {
    (void)flags;
    int rc = esp_tls_conn_read(httpd_sess_get_transport_ctx(server, fd), buf, len);
    return rc == ESP_TLS_ERR_SSL_WANT_READ ? HTTPD_SOCK_ERR_TIMEOUT : rc;
}

static int secure_pending(httpd_handle_t server, int fd) {
    return esp_tls_get_bytes_avail(httpd_sess_get_transport_ctx(server, fd));
}

static esp_err_t secure_open(httpd_handle_t server, int fd) {
    esp_tls_t *session = esp_tls_init();
    if (!session) return ESP_ERR_NO_MEM;
    if (esp_tls_server_session_create(&tls_config, fd, session) != 0) {
        esp_tls_server_session_delete(session);
        return ESP_FAIL;
    }
    httpd_sess_set_transport_ctx(server, fd, session, free_session);
    ESP_ERROR_CHECK(httpd_sess_set_send_override(server, fd, secure_send));
    ESP_ERROR_CHECK(httpd_sess_set_recv_override(server, fd, secure_recv));
    ESP_ERROR_CHECK(httpd_sess_set_pending_override(server, fd, secure_pending));
    return ESP_OK;
}

esp_err_t runner_psk_configure(httpd_config_t *config) {
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x41, "device_key");
    uint8_t record[8 + sizeof identity + sizeof key];
    if (!partition || esp_partition_read(partition, 0, record, sizeof record) != ESP_OK ||
        memcmp(record, "RXPSK001", 8) != 0) return ESP_ERR_INVALID_STATE;
    const uint8_t *id = record + 8;
    size_t n = 0;
    while (n < sizeof identity && id[n]) {
        unsigned c = id[n++];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return ESP_ERR_INVALID_ARG;
    }
    if (!n || n == sizeof identity) return ESP_ERR_INVALID_ARG;
    for (size_t i = n; i < sizeof identity; ++i)
        if (id[i]) return ESP_ERR_INVALID_ARG;
    const uint8_t *secret = id + sizeof identity;
    bool all_zero = true, all_erased = true;
    for (size_t i = 0; i < sizeof key; ++i) {
        all_zero &= secret[i] == 0;
        all_erased &= secret[i] == 0xff;
    }
    if (all_zero || all_erased) return ESP_ERR_INVALID_ARG;
    memcpy(identity, id, sizeof identity);
    memcpy(key, secret, sizeof key);
    /* The OpenETH test profile has no RF, ADC or I2S users. */
    bootloader_random_enable();
    config->server_port = 443;
    config->open_fn = secure_open;
    config->stack_size = 12288;
    return ESP_OK;
}
