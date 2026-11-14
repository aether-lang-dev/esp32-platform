#ifndef RUNNER_APPS_H
#define RUNNER_APPS_H
#include <stdbool.h>
#include <stdint.h>
#include "runner_host.h"

enum { RUNNER_APPS_RESULT_LIMIT = 512 };
enum { RUNNER_APPS_INVALID = 4, RUNNER_APPS_BUSY = 8,
       RUNNER_APPS_DENIED = 13, RUNNER_APPS_FAULT = 20 };
typedef struct {
    char name[65];
    uint32_t active_version, highest_version, trial_version;
    int active_slot;
    bool fault;
    const char *state;
    unsigned request;
} RunnerAppsInfo;

/* CALL is opt-in; each request gets a fresh bounded Rexx context. */
#define RUNNER_APPS_SERVICE_MARKER "/* ADDRESS SERVICE main v1 */\n"
int runner_apps_call(const RunnerAppsInfo *info, const char *command, unsigned grants,
                     int (*call)(const char *, char *), char result[RUNNER_APPS_RESULT_LIMIT]);

/* The host owns the registry and queues execution on the normal app task. */
int runner_apps_command(const RunnerAppsInfo *info, const char *command, unsigned grants,
                        int (*start)(unsigned *request), char result[RUNNER_APPS_RESULT_LIMIT]);
#endif
