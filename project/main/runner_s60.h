#ifndef RUNNER_S60_H
#define RUNNER_S60_H
#include "esp_partition.h"
#include <stdbool.h>
#define RUNNER_S60_NVS "rexx-state"
bool runner_s60_init(void);
void runner_s60_recover(void);
const esp_partition_t *runner_s60_slot(unsigned index);
bool runner_s60_record(bool wifi, void *record, size_t length);
#endif
