/* The independent ESP timer owns switch expiration. */
#include "runner_switch.h"
#include "esp_timer.h"
#ifdef RUNNER_CYD_SWITCH_LED
#include "driver/gpio.h"
#endif
#include "freertos/FreeRTOS.h"
#include <stdio.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static RunnerSwitch state;
static bool expired;
static bool ready;

#ifdef RUNNER_CYD_SWITCH_LED
enum { BENCH_LED = GPIO_NUM_22 };
static bool drive(bool on) {
    int level = on ? 0 : 1;
    return gpio_set_level(BENCH_LED, level) == ESP_OK && gpio_get_level(BENCH_LED) == level;
}

static bool configure_output(void) {
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BENCH_LED,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_set_level(BENCH_LED, 1) == ESP_OK && gpio_config(&config) == ESP_OK;
}
#else
static bool drive(bool on) { (void)on; return true; }
static bool configure_output(void) { return true; }
#endif

static RunnerSwitchOutput output = {.drive = drive};

static void expire(void *unused) {
    (void)unused;
    portENTER_CRITICAL(&lock);
    if (runner_switch_expire(&state, esp_timer_get_time())) {
        if (!runner_output_write(&output, &state, false)) ready = false;
        expired = true;
    }
    portEXIT_CRITICAL(&lock);
}

bool runner_switch_init(void) {
    const esp_timer_create_args_t args = {.callback = expire, .name = "signed-switch"};
    esp_timer_handle_t timer;
    if (!configure_output() || !runner_output_claim(&output, &state)) return false;
    if (esp_timer_create(&args, &timer) != ESP_OK) {
        (void)runner_output_release(&output, &state);
        return false;
    }
    if (esp_timer_start_periodic(timer, 50000) != ESP_OK) {
        esp_timer_delete(timer);
        (void)runner_output_release(&output, &state);
        return false;
    }
    portENTER_CRITICAL(&lock);
    ready = true;
    portEXIT_CRITICAL(&lock);
#ifdef RUNNER_CYD_SWITCH_LED
    puts("SIGNED USB SWITCH GPIO22 ready (off)");
#endif
    return true;
}

bool runner_switch_available(void) {
    portENTER_CRITICAL(&lock);
    bool available = ready;
    portEXIT_CRITICAL(&lock);
    return available;
}

bool runner_switch_on(void) {
    portENTER_CRITICAL(&lock);
    bool on = state.until_us != 0;
    portEXIT_CRITICAL(&lock);
    return on;
}

bool runner_switch_take_timeout(void) {
    portENTER_CRITICAL(&lock);
    bool value = expired;
    expired = false;
    portEXIT_CRITICAL(&lock);
    return value;
}

bool runner_switch_finish(int seconds, bool success) {
    if (success && seconds == RUNNER_SWITCH_KEEP) return true;
    portENTER_CRITICAL(&lock);
    int duration = success && ready ? seconds : 0;
    bool applied = runner_output_write(&output, &state, duration > 0);
    if (!applied) {
        ready = false;
        (void)runner_output_write(&output, &state, false);
        duration = 0;
    }
    runner_switch_apply(&state, duration, esp_timer_get_time());
    expired = false;
    portEXIT_CRITICAL(&lock);
    if (!applied) puts("SIGNED USB SWITCH output fault");
    else printf("SIGNED USB SWITCH %s\n", duration > 0 ? "ON" : "OFF");
    return applied && (seconds <= 0 || !success || duration > 0);
}
