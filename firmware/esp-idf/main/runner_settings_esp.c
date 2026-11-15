#include "runner_settings.h"
#include "nvs.h"
#include <stdio.h>

/* ESP-IDF limits NVS key names to 15 characters. */
static const char key[] = "auto_off_s";
static nvs_handle_t store;
static RunnerSettings settings = {.auto_off_seconds = RUNNER_SETTINGS_DEFAULT, .fault = true};

bool runner_settings_init(void) {
    if (nvs_open("rexx-settings", NVS_READWRITE, &store) != ESP_OK) return false;
    uint8_t seconds = RUNNER_SETTINGS_DEFAULT;
    esp_err_t status = nvs_get_u8(store, key, &seconds);
    if ((status != ESP_OK && status != ESP_ERR_NVS_NOT_FOUND) || !runner_settings_valid(seconds)) {
        nvs_close(store);
        return false;
    }
    settings = (RunnerSettings){.auto_off_seconds = seconds};
    return true;
}

int runner_settings_command(const char *command, bool can_write, uint8_t *pending, char result[3]) {
    return runner_settings_request(&settings, command, can_write, pending, result);
}

static bool save(uint8_t seconds) {
    if (nvs_set_u8(store, key, seconds) != ESP_OK || nvs_commit(store) != ESP_OK) return false;
    printf("SIGNED USB SETTINGS auto_off_seconds=%u\n", (unsigned)seconds);
    return true;
}

bool runner_settings_commit(uint8_t pending) {
    return runner_settings_finish(&settings, pending, true, save);
}
