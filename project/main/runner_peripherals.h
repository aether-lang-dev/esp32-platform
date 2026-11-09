#ifndef RUNNER_PERIPHERALS_H
#define RUNNER_PERIPHERALS_H
#include <stdbool.h>

typedef enum {
    DEMO_NONE, DEMO_I2C, DEMO_UART, DEMO_RANDOM, DEMO_PULSE, DEMO_TIMER_WAKE, DEMO_GPIO_WAKE, DEMO_WIFI_SCAN
} DemoPeripheral;

bool demo_open(DemoPeripheral peripheral);
bool demo_close(DemoPeripheral peripheral);
bool demo_pulse_open(void);
bool demo_pulse_close(void);
bool demo_pulse_count(int *count);
bool demo_wifi_scan_open(void);
void demo_wifi_scan_row(unsigned index, char *reply, unsigned reply_size);
bool demo_enter_sleep(DemoPeripheral peripheral);
int demo_arity(DemoPeripheral peripheral);
const char *demo_function(DemoPeripheral peripheral);
const char *demo_invoke(DemoPeripheral peripheral, int type, const char *argument,
                        char *reply, unsigned reply_size);

#endif
