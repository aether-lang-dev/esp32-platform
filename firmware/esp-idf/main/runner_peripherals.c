/* Standalone example adapters. Pins, bus parameters and timeouts belong to
 * the firmware. The signed runner does not link these adapters. */
#include "runner_peripherals.h"
#include "esp_random.h"
#ifdef RUNNER_TOIT_WAKE
#include "esp_sleep.h"
#endif
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RUNNER_TOIT_IO
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static i2c_master_bus_handle_t bus;
#define DEMO_UART_PORT UART_NUM_1
#endif

bool demo_open(DemoPeripheral peripheral) {
    if (peripheral == DEMO_PULSE) return demo_pulse_open();
    if (peripheral == DEMO_WIFI_SCAN) return demo_wifi_scan_open();
#ifdef RUNNER_TOIT_IO
    if (peripheral == DEMO_I2C) {
        i2c_master_bus_config_t config = {
            .i2c_port = I2C_NUM_0, .sda_io_num = 21, .scl_io_num = 22,
            .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        return i2c_new_master_bus(&config, &bus) == ESP_OK;
    }
    if (peripheral == DEMO_UART) {
        uart_config_t config = {
            .baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
        };
        return uart_param_config(DEMO_UART_PORT, &config) == ESP_OK &&
               uart_set_pin(DEMO_UART_PORT, 26, 25, UART_PIN_NO_CHANGE,
                            UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) == ESP_OK &&
               uart_driver_install(DEMO_UART_PORT, 256, 0, 0, NULL, 0) == ESP_OK;
    }
#endif
    return peripheral != DEMO_NONE;
}

bool demo_close(DemoPeripheral peripheral) {
    if (peripheral == DEMO_PULSE) return demo_pulse_close();
#ifdef RUNNER_TOIT_IO
    if (peripheral == DEMO_I2C) return i2c_del_master_bus(bus) == ESP_OK;
    if (peripheral == DEMO_UART) return uart_driver_delete(DEMO_UART_PORT) == ESP_OK;
#endif
    return peripheral != DEMO_NONE;
}

int demo_arity(DemoPeripheral peripheral) {
    return peripheral == DEMO_PULSE || peripheral == DEMO_TIMER_WAKE ||
           peripheral == DEMO_GPIO_WAKE ? 0 : 1;
}

#ifdef RUNNER_TOIT_WAKE
bool demo_enter_sleep(DemoPeripheral peripheral) {
    const bool gpio_wake = peripheral == DEMO_GPIO_WAKE;
    if (esp_sleep_enable_timer_wakeup(gpio_wake ? 30000000 : 10000000) != ESP_OK) return false;
    if (gpio_wake && esp_sleep_enable_ext1_wakeup_io(1ULL << 32, ESP_EXT1_WAKEUP_ANY_HIGH) != ESP_OK) return false;
    fflush(stdout);
    esp_deep_sleep_start();
}
#endif

static int wake_cause(DemoPeripheral peripheral) {
#ifdef RUNNER_TOIT_WAKE
    (void)peripheral;
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_TIMER) return 1;
    if (cause == ESP_SLEEP_WAKEUP_EXT1) return 2;
    return 0;
#else
    return peripheral == DEMO_TIMER_WAKE ? 1 : 2;
#endif
}

const char *demo_function(DemoPeripheral peripheral) {
    switch (peripheral) {
        case DEMO_I2C: return "I2C_PROBE";
        case DEMO_UART: return "UART_ECHO";
        case DEMO_RANDOM: return "RANDOM_BELOW";
        case DEMO_WIFI_SCAN: return "WIFI_SCAN_ROW";
        case DEMO_PULSE: return "PULSE_COUNT";
        case DEMO_TIMER_WAKE:
        case DEMO_GPIO_WAKE: return "WAKE_CAUSE";
        default: return NULL;
    }
}

static bool read_number(int type, const char *argument, long *number) {
    if (type != 2 || !argument || !isdigit((unsigned char)argument[0])) return false;
    char *end;
    *number = strtol(argument, &end, 10);
    return !*end;
}

static const char *uart_echo(int type, const char *argument, char *reply, unsigned reply_size) {
    if (type != 3 || !argument) return "UART_ECHO requires a string";
    size_t length = strlen(argument);
    if (!length || length > 24) return "UART_ECHO requires 1 to 24 characters";
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)argument[i];
        if (ch < ' ' || ch > '~' || ch == '"' || ch == '\\') return "UART_ECHO requires plain ASCII text";
    }
    char line[25];
    memcpy(line, argument, length);
    line[length] = '\n';
#ifdef RUNNER_TOIT_IO
    char received[sizeof line];
    if (uart_flush_input(DEMO_UART_PORT) != ESP_OK ||
        uart_write_bytes(DEMO_UART_PORT, line, length + 1) != (int)(length + 1) ||
        uart_read_bytes(DEMO_UART_PORT, received, length + 1, pdMS_TO_TICKS(100)) != (int)(length + 1) ||
        memcmp(received, line, length + 1)) return "UART loopback mismatch or timeout";
#endif
    snprintf(reply, reply_size, "\"%.*s\"", (int)length, line);
    return NULL;
}

const char *demo_invoke(DemoPeripheral peripheral, int type, const char *argument,
                        char *reply, unsigned reply_size) {
    if (peripheral == DEMO_PULSE) {
        int count;
        if (!demo_pulse_count(&count)) return "pulse counter read failed";
        snprintf(reply, reply_size, "%d", count);
        return NULL;
    }
    if (peripheral == DEMO_TIMER_WAKE || peripheral == DEMO_GPIO_WAKE) {
        snprintf(reply, reply_size, "%d", wake_cause(peripheral));
        return NULL;
    }
    if (peripheral == DEMO_UART) return uart_echo(type, argument, reply, reply_size);
    long number;
    if (!read_number(type, argument, &number)) return "expected an unsigned integer";
    if (peripheral == DEMO_WIFI_SCAN) {
        if (number < 0 || number > 7) return "Wi-Fi scan index must be from 0 to 7";
        demo_wifi_scan_row((unsigned)number, reply, reply_size);
        return NULL;
    }
    if (peripheral == DEMO_I2C) {
        if (number < 8 || number > 119) return "I2C address must be from 8 to 119";
#ifdef RUNNER_TOIT_IO
        esp_err_t status = i2c_master_probe(bus, (uint16_t)number, 10);
        if (status != ESP_OK && status != ESP_ERR_NOT_FOUND) return "I2C probe failed";
        bool found = status == ESP_OK;
#else
        bool found = number == 0x3c || number == 0x68;
#endif
        snprintf(reply, reply_size, "%u", found ? 1u : 0u);
        return NULL;
    }
    if (peripheral != DEMO_RANDOM || number < 1 || number > 100) return "random limit must be from 1 to 100";
    uint32_t limit = (uint32_t)number;
    uint32_t threshold = (0u - limit) % limit;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        uint32_t value = esp_random();
        if (value >= threshold) {
            snprintf(reply, reply_size, "%u", (unsigned)(value % limit));
            return NULL;
        }
    }
    return "random sample unavailable";
}
