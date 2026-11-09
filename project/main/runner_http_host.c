/* QEMU HTTP delivery hosts. runner-http accepts unsigned development source;
 * runner-signed verifies bundles against an injected public test trust set. */
#include "runner_host.h"
#ifdef RUNNER_PSK
#include "runner_psk.h"
#endif
#ifdef RUNNER_SIGNED
#include "runner_bundle.h"
#include "runner_store.h"
#ifdef RUNNER_WIFI
#include "runner_wifi.h"
#include "cyd_console.h"
#define trusted_keys wifi_trusted_keys
#else
#include "trusted_test_keys.h"
#define trusted_keys test_trusted_keys
#endif
#define UPLOAD_LIMIT RUNNER_BUNDLE_LIMIT
#else
#define UPLOAD_LIMIT SCRIPT_LIMIT
#endif
#include "esp_eth_driver.h"
#include "esp_eth_netif_glue.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned char *bytes; size_t length; unsigned id; } Upload;
static QueueHandle_t uploads;
static SemaphoreHandle_t available;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static unsigned generation;
static int exit_code;
static int last_runtime_outcome;
static const char *phase = "idle";
static bool was_cancelled;

static esp_err_t response(httpd_req_t *req, const char *status, const char *body) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
#ifdef RUNNER_PSK
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t result = httpd_resp_sendstr(req, body);
    esp_err_t closed = httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
    return result == ESP_OK ? closed : result;
#else
    return httpd_resp_sendstr(req, body);
#endif
}

static esp_err_t status_get(httpd_req_t *req) {
    char body[352];
    portENTER_CRITICAL(&state_lock);
    unsigned id = generation;
    int code = exit_code;
    int runtime_outcome = last_runtime_outcome;
    const char *state = phase;
    bool cancelled = was_cancelled;
    portEXIT_CRITICAL(&state_lock);
#ifdef RUNNER_SIGNED
    RunnerStoreInfo info; runner_store_info(&info);
    snprintf(body, sizeof body, "{\"id\":%u,\"state\":\"%s\",\"exitCode\":%d,\"runtimeOutcome\":%d,\"cancelled\":%s,"
             "\"activeVersion\":%lu,\"highestVersion\":%lu,\"trialVersion\":%lu,\"activeSlot\":%d,\"storeFault\":%s,\"activeFingerprint\":\"%s\"}",
             id, state, code, runtime_outcome, cancelled ? "true" : "false",
             (unsigned long)info.active_version, (unsigned long)info.highest_version,
             (unsigned long)info.trial_version, info.active_slot, info.fault ? "true" : "false", info.active_fingerprint);
#else
    snprintf(body, sizeof body, "{\"id\":%u,\"state\":\"%s\",\"exitCode\":%d,\"runtimeOutcome\":%d,\"cancelled\":%s}",
             id, state, code, runtime_outcome, cancelled ? "true" : "false");
#endif
    return response(req, "200 OK", body);
}

static esp_err_t script_post(httpd_req_t *req) {
    if (req->content_len == 0) return response(req, "400 Bad Request", "{\"error\":\"empty script\"}");
    if (req->content_len > UPLOAD_LIMIT) return response(req, "413 Content Too Large", "{\"error\":\"script too large\"}");
    if (xSemaphoreTake(available, 0) != pdTRUE)
        return response(req, "409 Conflict", "{\"error\":\"runner busy\"}");
    unsigned char *source = malloc(req->content_len + 1);
    if (!source) {
        xSemaphoreGive(available);
        return response(req, "503 Service Unavailable", "{\"error\":\"no memory\"}");
    }
    size_t used = 0;
    while (used < req->content_len) {
        int count = httpd_req_recv(req, (char *)source + used, req->content_len - used);
        if (count <= 0) {
            free(source);
            xSemaphoreGive(available);
            /* Close an incomplete request, without queuing or modifying flash. */
            return ESP_FAIL;
        }
        used += (size_t)count;
    }
#ifdef RUNNER_SIGNED
    RunnerBundle candidate;
    if (runner_bundle_verify(source, used, trusted_keys,
            sizeof trusted_keys / sizeof trusted_keys[0], &candidate) != 0) {
        free(source);
        xSemaphoreGive(available);
        return response(req, "403 Forbidden", "{\"error\":\"bundle rejected\"}");
    }
    int offered = runner_store_offer(&candidate);
    if (offered != 0) {
        free(source);
        xSemaphoreGive(available);
        return response(req, offered < 0 ? "503 Service Unavailable" : "409 Conflict",
                        "{\"error\":\"storage fault or version/application conflict\"}");
    }
#else
    if (memchr(source, 0, used)) {
        free(source);
        xSemaphoreGive(available);
        return response(req, "400 Bad Request", "{\"error\":\"NUL in source\"}");
    }
#endif
    source[used] = 0;
    portENTER_CRITICAL(&state_lock);
    unsigned id = ++generation;
    phase = "pending";
    exit_code = 0;
    last_runtime_outcome = 0;
    was_cancelled = false;
    portEXIT_CRITICAL(&state_lock);
    Upload upload = {.bytes = source, .length = used, .id = id};
    if (xQueueSend(uploads, &upload, 0) != pdTRUE) {
        free(source);
        portENTER_CRITICAL(&state_lock);
        phase = "error";
        exit_code = -1;
        portEXIT_CRITICAL(&state_lock);
        xSemaphoreGive(available);
        return response(req, "503 Service Unavailable", "{\"error\":\"queue full\"}");
    }
    char body[48];
    snprintf(body, sizeof body, "{\"id\":%u}", id);
    return response(req, "202 Accepted", body);
}

static bool network_start(void) {
#ifndef RUNNER_WIFI
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
#endif
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
#ifdef RUNNER_PSK
    if (runner_psk_configure(&http) != ESP_OK) {
        puts("SIGNED PSK device is unprovisioned or credential is invalid");
        return false;
    }
#endif
#ifdef RUNNER_WIFI
    if (!runner_wifi_start()) {
        puts("SIGNED CYD Wi-Fi could not obtain an address");
        cyd_console_line("Wi-Fi failed");
        return false;
    }
#endif
    http.recv_wait_timeout = 2;
#ifdef RUNNER_SIGNED
    http.stack_size = 8192;
#endif
    http.send_wait_timeout = 2;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &http));
    const httpd_uri_t post = {.uri="/script", .method=HTTP_POST, .handler=script_post};
    const httpd_uri_t get = {.uri="/status", .method=HTTP_GET, .handler=status_get};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &get));
#ifdef RUNNER_WIFI
    puts("SIGNED CYD Wi-Fi endpoint started");
    cyd_console_line("Wi-Fi signed runner ready");
#elif defined(RUNNER_SIGNED)
    printf("SIGNED endpoint started (public test trust set)\n");
#else
    printf("HTTP development endpoint started (unsigned uploads)\n");
#endif
    return true;
}

#ifdef RUNNER_SIGNED
static int run_bundle(RunnerBundle *bundle, RunnerStats *stats) {
    ((unsigned char *)bundle->source)[bundle->source_length] = 0;
    return runner_execute((const char *)bundle->source, bundle->grants, stats);
}
static int restore_active(void) {
    RunnerStoreInfo info; runner_store_info(&info);
    if (!info.active_version) return 0;
    RunnerBundle active;
    unsigned char *bytes = runner_store_load_active(trusted_keys,
        sizeof trusted_keys / sizeof trusted_keys[0], &active);
    if (!bytes) { puts("SIGNED active release could not be verified/read"); return -1; }
    RunnerStats stats;
    int result = run_bundle(&active, &stats);
    printf("SIGNED restored active v%lu exit=%d\n", (unsigned long)active.version, result);
    free(bytes);
    return result;
}
#endif

void app_main(void) {
#ifdef RUNNER_WIFI
    if (!runner_wifi_load()) {
        puts("SIGNED CYD Wi-Fi provisioning missing or invalid");
        return;
    }
    (void)cyd_console_init();
    cyd_console_line("RexxAE signed Wi-Fi");
#endif
    uploads = xQueueCreate(1, sizeof(Upload));
    available = xSemaphoreCreateBinary();
    if (!uploads || !available) abort();
    xSemaphoreGive(available);
#ifdef RUNNER_SIGNED
    int boot = runner_store_init();
    if (!boot) boot = restore_active();
    if (boot) { phase = "error"; exit_code = -1; }
#endif
    if (!network_start()) return;
    for (;;) {
        Upload upload;
        if (xQueueReceive(uploads, &upload, portMAX_DELAY) != pdTRUE) continue;
        portENTER_CRITICAL(&state_lock);
        phase = "running";
        portEXIT_CRITICAL(&state_lock);
        RunnerStats stats = {0};
        int result = -1;
#ifdef RUNNER_SIGNED
        RunnerBundle candidate;
        if (runner_store_stage(upload.bytes, upload.length, trusted_keys,
                sizeof trusted_keys / sizeof trusted_keys[0], &candidate) == 0) {
            printf("SIGNED trial %s v%lu key=%s grants=%u\n", candidate.name,
                   (unsigned long)candidate.version, candidate.key_id, candidate.grants);
            result = run_bundle(&candidate, &stats);
            if (runner_store_finish(result == 0) != 0) result = -1;
            if (result != 0) (void)restore_active();
        }
#else
        const esp_partition_t *slot = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "script_b");
        size_t length = upload.length + 1;
        if (slot && slot->size >= length && esp_partition_erase_range(slot, 0, slot->size) == ESP_OK &&
            esp_partition_write(slot, 0, upload.bytes, length) == ESP_OK) {
            memset(upload.bytes, 0, length);
            if (esp_partition_read(slot, 0, upload.bytes, length) == ESP_OK && upload.bytes[length-1] == 0)
                result = runner_execute((const char *)upload.bytes, RUNNER_GRANT_TIMERS, &stats);
        }
#endif
        free(upload.bytes);
        printf("HTTP upload %u finished: exit=%d cancelled=%d\n", upload.id, result, stats.cancelled);
        portENTER_CRITICAL(&state_lock);
        exit_code = result;
        last_runtime_outcome = stats.runtime_outcome;
        was_cancelled = stats.cancelled;
        phase = result == 0 ? "done" : "error";
        portEXIT_CRITICAL(&state_lock);
        xSemaphoreGive(available);
    }
}
