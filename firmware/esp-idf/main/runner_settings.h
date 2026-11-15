#ifndef RUNNER_SETTINGS_H
#define RUNNER_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RUNNER_SETTINGS_DEFAULT = 5, RUNNER_SETTINGS_KEEP = 0,
    RUNNER_SETTINGS_INVALID = 4, RUNNER_SETTINGS_DENIED = 13, RUNNER_SETTINGS_FAULT = 20,
};
typedef struct {
    uint8_t auto_off_seconds;
    bool fault;
} RunnerSettings;

typedef bool (*RunnerSettingsSave)(uint8_t seconds);
bool runner_settings_valid(uint8_t seconds);
int runner_settings_request(const RunnerSettings *settings, const char *command, bool can_write,
                            uint8_t *pending, char result[3]);
bool runner_settings_finish(RunnerSettings *settings, uint8_t pending, bool success,
                            RunnerSettingsSave save);

bool runner_settings_init(void);
int runner_settings_command(const char *command, bool can_write, uint8_t *pending, char result[3]);
bool runner_settings_commit(uint8_t pending);

#endif
