#include "runner_apps.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int rejected(char *result, int rc, const char *reason) {
    snprintf(result, RUNNER_APPS_RESULT_LIMIT, "{\"error\":\"%s\"}", reason);
    return rc;
}

int runner_apps_command(const RunnerAppsInfo *info, const char *command, unsigned grants,
                        int (*start)(unsigned *request), char result[RUNNER_APPS_RESULT_LIMIT]) {
    char verb[16], app[65], extra[2];
    int words = sscanf(command, " %15s %64s %1s", verb, app, extra);
    if (words == 1 && !strcasecmp(verb, "LIST")) {
        if (!(grants & RUNNER_GRANT_APPS_READ)) return rejected(result, RUNNER_APPS_DENIED, "APPS read not granted");
        snprintf(result, RUNNER_APPS_RESULT_LIMIT, "[{\"app\":\"main\",\"installed\":%s,\"name\":\"%s\"}]",
                 info->active_version ? "true" : "false", info->name);
        return 0;
    }
    if (words != 2 || strcasecmp(app, "main")) return rejected(result, RUNNER_APPS_INVALID, "unknown APPS command or application");
    if (!strcasecmp(verb, "STATUS")) {
        if (!(grants & RUNNER_GRANT_APPS_READ)) return rejected(result, RUNNER_APPS_DENIED, "APPS read not granted");
        snprintf(result, RUNNER_APPS_RESULT_LIMIT,
                 "{\"app\":\"main\",\"name\":\"%s\",\"activeVersion\":%lu,\"highestVersion\":%lu,"
                 "\"trialVersion\":%lu,\"activeSlot\":%d,\"state\":\"%s\",\"request\":%u,\"storeFault\":%s}",
                 info->name, (unsigned long)info->active_version, (unsigned long)info->highest_version,
                 (unsigned long)info->trial_version, info->active_slot, info->state, info->request,
                 info->fault ? "true" : "false");
        return 0;
    }
    if (!strcasecmp(verb, "RUN")) {
        if (!(grants & RUNNER_GRANT_APPS_RUN)) return rejected(result, RUNNER_APPS_DENIED, "APPS run not granted");
        if (info->fault) return rejected(result, RUNNER_APPS_FAULT, "application store unavailable");
        if (!info->active_version) return rejected(result, RUNNER_APPS_INVALID, "no installed application");
        unsigned request = 0;
        int rc = start(&request);
        if (rc) return rejected(result, rc, rc == RUNNER_APPS_BUSY ? "application runner busy" : "could not queue application");
        snprintf(result, RUNNER_APPS_RESULT_LIMIT, "{\"app\":\"main\",\"queued\":%u}", request);
        return 0;
    }
    return rejected(result, RUNNER_APPS_INVALID, "unknown APPS command");
}

int runner_apps_call(const RunnerAppsInfo *info, const char *command, unsigned grants,
                     int (*call)(const char *, char *), char result[RUNNER_APPS_RESULT_LIMIT]) {
    char verb[16], app[65];
    int offset = 0;
    if (sscanf(command, " %15s %64s %n", verb, app, &offset) != 2 ||
        strcasecmp(verb, "CALL") || strcasecmp(app, "main") || !offset || !command[offset])
        return rejected(result, RUNNER_APPS_INVALID, "expected CALL main command");
    if (!(grants & RUNNER_GRANT_APPS_CALL))
        return rejected(result, RUNNER_APPS_DENIED, "APPS call not granted");
    if (info->fault) return rejected(result, RUNNER_APPS_FAULT, "application store unavailable");
    if (!info->active_version) return rejected(result, RUNNER_APPS_INVALID, "no installed application");
    if (strlen(command + offset) > 128)
        return rejected(result, RUNNER_APPS_INVALID, "service command too long");
    return call(command + offset, result);
}
