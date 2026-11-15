#ifndef RUNNER_OUTLET_H
#define RUNNER_OUTLET_H
#include <stdbool.h>
#include "runner_host.h"
enum { RUNNER_OUTLET_TEXT = 128, RUNNER_OUTLET_DEFAULT_SECONDS = 10 };
enum { OUTLET_STATUS, OUTLET_START, OUTLET_CANCEL };
int runner_outlet_request(const char *command, unsigned grants, int *action, int *seconds);
int runner_outlet_command(const char *command, unsigned grants, char *result);
void runner_outlet_button(void);
#endif
