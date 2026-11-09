/* Copyright (C) 2022 Toitware ApS.
 * SPDX-License-Identifier: 0BSD
 * Adapted from Toit examples/triggers/touch.toit.
 */
#include <inttypes.h>
#include <stdio.h>
#include "driver/touch_sens.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void) {
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TOUCHPAD) {
        puts("Woken up from touchpad");
        vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
        puts("Woken up for another reason");
    }

    touch_sensor_sample_config_t sample = TOUCH_SENSOR_V1_DEFAULT_SAMPLE_CONFIG(
        5.0, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_1V7);
    touch_sensor_config_t config = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, &sample);
    touch_sensor_handle_t sensor;
    ESP_ERROR_CHECK(touch_sensor_new_controller(&config, &sensor));

    touch_channel_config_t settings = {
        .abs_active_thresh = {1000}, .charge_speed = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT, .group = TOUCH_CHAN_TRIG_GROUP_BOTH,
    };
    touch_channel_handle_t pad;
    ESP_ERROR_CHECK(touch_sensor_new_channel(sensor, 9, &settings, &pad)); /* GPIO32 */
    touch_sensor_filter_config_t filter = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_filter(sensor, &filter));
    ESP_ERROR_CHECK(touch_sensor_enable(sensor));
    uint64_t sum = 0;
    uint32_t raw = 0;
    for (int sample_index = 0; sample_index < 16; ++sample_index) {
        ESP_ERROR_CHECK(touch_sensor_trigger_oneshot_scanning(sensor, 2000));
        ESP_ERROR_CHECK(touch_channel_read_data(pad, TOUCH_CHAN_DATA_TYPE_SMOOTH, &raw));
        sum += raw;
    }
    ESP_ERROR_CHECK(touch_sensor_disable(sensor));
    settings.abs_active_thresh[0] = (uint32_t)(sum * 2 / (3 * 16));
    if (!settings.abs_active_thresh[0]) {
        puts("Touch calibration produced no usable samples");
        return;
    }
    ESP_ERROR_CHECK(touch_sensor_reconfig_channel(pad, &settings));
    printf("GPIO32 threshold=%" PRIu32 " raw=%" PRIu32 "\n", settings.abs_active_thresh[0], raw);

    touch_sleep_config_t sleep = TOUCH_SENSOR_DEFAULT_DSLP_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_sleep_wakeup(sensor, &sleep));
    ESP_ERROR_CHECK(touch_sensor_enable(sensor));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(sensor));
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(30000000));
    puts("Sleeping for up to 30 seconds");
    fflush(stdout);
    esp_deep_sleep_start();
}
