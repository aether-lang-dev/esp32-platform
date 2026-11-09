#include "runner_switch.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int runner_switch_request(const char *command, bool can_control, bool on,
                          int *seconds, const char **result) {
    *result = "";
    if (!strcasecmp(command, "status")) {
        bool requested_on = *seconds == RUNNER_SWITCH_KEEP ? on : *seconds > 0;
        *result = requested_on ? "ON" : "OFF";
        return 0;
    }

    int duration = 0;
    if (strncasecmp(command, "on ", 3) == 0) {
        const char *digits = command + 3;
        size_t length = strspn(digits, "0123456789");
        if (!length || length > 2 || digits[length]) return RUNNER_SWITCH_BAD_COMMAND;
        duration = (int)strtol(digits, NULL, 10);
        if (duration < 1 || duration > 60) return RUNNER_SWITCH_BAD_COMMAND;
    } else if (strcasecmp(command, "off")) {
        return RUNNER_SWITCH_BAD_COMMAND;
    }
    if (!can_control) return RUNNER_SWITCH_DENIED;

    *seconds = duration;
    *result = duration ? "ON" : "OFF";
    return 0;
}

void runner_switch_apply(RunnerSwitch *state, int seconds, int64_t now_us) {
    state->until_us = seconds > 0 ? now_us + (int64_t)seconds * 1000000 : 0;
}

bool runner_switch_expire(RunnerSwitch *state, int64_t now_us) {
    if (!state->until_us || now_us < state->until_us) return false;
    state->until_us = 0;
    return true;
}

bool runner_output_claim(RunnerSwitchOutput *output, const void *owner) {
    if (!owner || output->owner || !output->drive(false)) return false;
    output->owner = owner;
    return true;
}

bool runner_output_write(const RunnerSwitchOutput *output, const void *owner, bool on) {
    return owner && output->owner == owner && output->drive(on);
}

bool runner_output_release(RunnerSwitchOutput *output, const void *owner) {
    if (!runner_output_write(output, owner, false)) return false;
    output->owner = NULL;
    return true;
}
