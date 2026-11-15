#ifndef RUNNER_WIFI_H
#define RUNNER_WIFI_H
#include "runner_bundle.h"

extern RunnerTrustedKey wifi_trusted_keys[1];
bool runner_wifi_load(void);
bool runner_wifi_start(void);

#endif
