/* Keep the installed Tasmota partition table and Safeboot intact. */
#include "runner_s60.h"
#include "runner_outlet.h"
#include "driver/gpio.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>
#if __has_include("/gen/s60_credentials.h")
#include "/gen/s60_credentials.h"
#else
static const unsigned char s60_wifi[235] = {0};
static const unsigned char s60_psk[105] = {0};
#endif

static esp_partition_t slots[2], state;
static const esp_partition_t *recovery;

static bool region(const char *label, esp_partition_type_t type, int subtype,
                   uint32_t address, uint32_t size) {
    const esp_partition_t *partition = esp_partition_find_first(type, subtype, label);
    return partition && partition->address == address && partition->size == size &&
           !partition->encrypted && !partition->readonly;
}

void runner_s60_recover(void) {
    if (recovery && esp_ota_set_boot_partition(recovery) == ESP_OK) esp_restart();
}

static void recovery_button(void *unused) {
    (void)unused;
    unsigned held = 0;
    for (;;) {
        if (gpio_get_level(GPIO_NUM_9)) {
            if (held == 50) {
                /* Release the boot-strapping button before restarting. */
                ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_4, 0));
                runner_s60_recover();
            } else if (held >= 2) {
                runner_outlet_button();
            }
            held = 0;
        } else if (held < 50) {
            ++held;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool runner_s60_init(void) {
    if (!region("nvs", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, 0x9000, 0x5000) ||
        !region("otadata", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, 0xe000, 0x2000) ||
        !region("safeboot", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, 0x10000, 0xd0000) ||
        !region("app0", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, 0xe0000, 0x2d0000) ||
        !region("spiffs", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 0x3b0000, 0x50000)) {
        puts("S60 partition layout refused");
        return false;
    }
    recovery = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "safeboot");
    gpio_config_t button = {.pin_bit_mask = 1ULL << GPIO_NUM_9, .mode = GPIO_MODE_INPUT,
                            .pull_up_en = GPIO_PULLUP_ENABLE};
    if (gpio_config(&button) != ESP_OK ||
        xTaskCreate(recovery_button, "s60-recovery", 3072, NULL, 5, NULL) != pdPASS) return false;

    const esp_partition_t *data = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    /* Bounded views use ESP-IDF's partition read/write size checks. */
    for (unsigned i = 0; i < 2; ++i) {
        slots[i] = *data;
        slots[i].address += i * 0x10000;
        slots[i].size = 0x10000;
    }
    state = *data;
    state.address += 0x20000;
    state.size = 0x6000;
    strcpy(state.label, RUNNER_S60_NVS);
    state.subtype = ESP_PARTITION_SUBTYPE_DATA_NVS;
    /* Never erase a failed NVS initialization or reset its replay floor. */
    return nvs_flash_init_partition_ptr(&state) == ESP_OK;
}

const esp_partition_t *runner_s60_slot(unsigned index) {
    return index < 2 && slots[index].size ? &slots[index] : NULL;
}

bool runner_s60_record(bool wifi, void *record, size_t length) {
    const unsigned char *source = wifi ? s60_wifi : s60_psk;
    size_t size = wifi ? sizeof s60_wifi : sizeof s60_psk;
    if (length != size) return false;
    memcpy(record, source, size);
    return true;
}
