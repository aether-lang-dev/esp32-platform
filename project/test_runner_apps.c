#include "runner_apps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned calls;
static int next_rc;
static int start(unsigned *request) {
    ++calls;
    *request = 7;
    return next_rc;
}

int main(void) {
    RunnerAppsInfo info = {.name = "bench", .active_version = 4, .highest_version = 5,
                          .active_slot = 1, .state = "idle"};
    char result[RUNNER_APPS_RESULT_LIMIT];
    unsigned all = RUNNER_GRANT_APPS_READ | RUNNER_GRANT_APPS_RUN;
    assert(runner_apps_command(&info, "LIST", all, start, result) == 0);
    assert(strstr(result, "\"app\":\"main\"") && strstr(result, "\"name\":\"bench\""));
    assert(runner_apps_command(&info, "  status MAIN  ", all, start, result) == 0);
    assert(strstr(result, "\"activeVersion\":4") && strstr(result, "\"highestVersion\":5"));
    assert(runner_apps_command(&info, "LIST", 0, start, result) == RUNNER_APPS_DENIED);
    assert(runner_apps_command(&info, "STATUS main", RUNNER_GRANT_APPS_RUN, start, result) == RUNNER_APPS_DENIED);
    assert(runner_apps_command(&info, "RUN main", RUNNER_GRANT_APPS_READ, start, result) == RUNNER_APPS_DENIED);
    assert(!calls);
    const char *bad[] = {"", "LIST extra", "RUN", "RUN other", "RUN main extra", "STOP main",
                         "INSTALL main source", "STATUS main/../other"};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        assert(runner_apps_command(&info, bad[i], all, start, result) == RUNNER_APPS_INVALID);
    }
    assert(!calls);
    info.fault = true;
    assert(runner_apps_command(&info, "RUN main", all, start, result) == RUNNER_APPS_FAULT && !calls);
    info.fault = false;
    info.active_version = 0;
    assert(runner_apps_command(&info, "RUN main", all, start, result) == RUNNER_APPS_INVALID && !calls);
    info.active_version = 4;
    next_rc = RUNNER_APPS_BUSY;
    assert(runner_apps_command(&info, "RUN main", all, start, result) == RUNNER_APPS_BUSY && calls == 1);
    next_rc = 0;
    assert(runner_apps_command(&info, "RUN main", all, start, result) == 0 && calls == 2);
    assert(strstr(result, "\"queued\":7"));
    puts("APPS registry, parsing, grants, faults and queued execution PASS");
    return 0;
}
