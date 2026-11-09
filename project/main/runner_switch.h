#ifndef RUNNER_SWITCH_H
#define RUNNER_SWITCH_H
#include <stdbool.h>
#include <stdint.h>

enum { RUNNER_SWITCH_KEEP = -1, RUNNER_SWITCH_DENIED = 13, RUNNER_SWITCH_BAD_COMMAND = 4 };
typedef struct { int64_t until_us; } RunnerSwitch;

int runner_switch_request(const char *command, bool can_control, bool on,
                          int *seconds, const char **result);
void runner_switch_apply(RunnerSwitch *state, int seconds, int64_t now_us);
bool runner_switch_expire(RunnerSwitch *state, int64_t now_us);

typedef struct {
    const void *owner;
    bool (*drive)(bool on);
} RunnerSwitchOutput;

bool runner_output_claim(RunnerSwitchOutput *output, const void *owner);
bool runner_output_write(const RunnerSwitchOutput *output, const void *owner, bool on);
bool runner_output_release(RunnerSwitchOutput *output, const void *owner);

bool runner_switch_init(void);
bool runner_switch_available(void);
bool runner_switch_on(void);
bool runner_switch_take_timeout(void);
bool runner_switch_finish(int seconds, bool success);

#endif
