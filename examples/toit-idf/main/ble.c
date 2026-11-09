/* Native NimBLE lifecycle shared by the Toit example ports. */
#include <stdio.h>
#include <stdlib.h>
#include "ble_example.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

void example_check(int rc, const char *operation) {
    if (rc) {
        printf("%s failed: %d\n", operation, rc);
        abort();
    }
}

static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

#ifdef TOIT_ble_heart_rate
void example_ble_prepare(void);
#endif

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nimble_port_init());
#ifdef TOIT_ble_heart_rate
    example_ble_prepare();
#endif
    ble_hs_cfg.sync_cb = example_ble_start;
    nimble_port_freertos_init(host_task);
}
