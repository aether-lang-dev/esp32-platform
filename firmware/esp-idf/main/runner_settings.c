#include "runner_settings.h"
#include <stdio.h>
#include <strings.h>

bool runner_settings_valid(uint8_t seconds) { return seconds >= 1 && seconds <= 60; }

int runner_settings_request(const RunnerSettings *settings, const char *command, bool can_write,
                            uint8_t *pending, char result[3]) {
    *result = 0;
    if (settings->fault) return RUNNER_SETTINGS_FAULT;
    if (!strcasecmp(command, "get auto_off_seconds")) {
        unsigned seconds = *pending == RUNNER_SETTINGS_KEEP ? settings->auto_off_seconds : *pending;
        snprintf(result, 3, "%u", seconds <= 60 ? seconds : 0);
        return 0;
    }
    static const char prefix[] = "set auto_off_seconds ";
    if (strncasecmp(command, prefix, sizeof prefix - 1)) return RUNNER_SETTINGS_INVALID;
    const char *digits = command + sizeof prefix - 1;
    if (*digits < '0' || *digits > '9') return RUNNER_SETTINGS_INVALID;
    unsigned seconds = (unsigned)(*digits++ - '0');
    if (*digits >= '0' && *digits <= '9') seconds = seconds * 10 + (unsigned)(*digits++ - '0');
    if (*digits || !runner_settings_valid((uint8_t)seconds)) return RUNNER_SETTINGS_INVALID;
    if (!can_write) return RUNNER_SETTINGS_DENIED;
    *pending = (uint8_t)seconds;
    return 0;
}

bool runner_settings_finish(RunnerSettings *settings, uint8_t pending, bool success,
                            RunnerSettingsSave save) {
    if (!success || pending == RUNNER_SETTINGS_KEEP || pending == settings->auto_off_seconds) return true;
    if (settings->fault || !runner_settings_valid(pending) || !save(pending)) {
        settings->fault = true;
        return false;
    }
    settings->auto_off_seconds = pending;
    return true;
}
