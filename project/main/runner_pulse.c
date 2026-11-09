/* Standalone pulse-counter example. */
#include "runner_peripherals.h"
#include "esp_timer.h"

#ifdef RUNNER_TOIT_IO
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"

static pcnt_unit_handle_t counter;
static pcnt_channel_handle_t channel;
static bool enabled;
static bool started;
static bool output;

bool demo_pulse_close(void) {
    bool ok = true;
    if (output) ok &= ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0) == ESP_OK;
    if (started) ok &= pcnt_unit_stop(counter) == ESP_OK;
    if (enabled) ok &= pcnt_unit_disable(counter) == ESP_OK;
    if (channel) ok &= pcnt_del_channel(channel) == ESP_OK;
    if (counter) ok &= pcnt_del_unit(counter) == ESP_OK;
    output = started = enabled = false;
    channel = NULL;
    counter = NULL;
    return ok;
}

bool demo_pulse_open(void) {
    pcnt_unit_config_t unit = {.low_limit = -1, .high_limit = 1000};
    pcnt_chan_config_t input = {.edge_gpio_num = 18, .level_gpio_num = -1};
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_12_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 25, .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t wave = {
        .gpio_num = 5, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0, .duty = 2048,
    };
    if (pcnt_new_unit(&unit, &counter) != ESP_OK ||
        pcnt_new_channel(counter, &input, &channel) != ESP_OK ||
        pcnt_channel_set_edge_action(channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_HOLD) != ESP_OK ||
        pcnt_unit_enable(counter) != ESP_OK) goto fail;
    enabled = true;
    if (pcnt_unit_clear_count(counter) != ESP_OK || pcnt_unit_start(counter) != ESP_OK) goto fail;
    started = true;
    if (ledc_timer_config(&timer) != ESP_OK || ledc_channel_config(&wave) != ESP_OK) goto fail;
    output = true;
    return true;
fail:
    demo_pulse_close();
    return false;
}

bool demo_pulse_count(int *count) {
    return pcnt_unit_get_count(counter, count) == ESP_OK;
}
#else
static int64_t wave_start;

bool demo_pulse_open(void) {
    wave_start = esp_timer_get_time();
    return true;
}

bool demo_pulse_close(void) {
    return true;
}

bool demo_pulse_count(int *count) {
    *count = (int)((esp_timer_get_time() - wave_start) / 40000);
    return true;
}
#endif
