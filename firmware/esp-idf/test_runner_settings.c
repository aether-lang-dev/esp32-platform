#include "runner_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned writes;
static uint8_t persisted = RUNNER_SETTINGS_DEFAULT;
static bool writable = true;

static bool save(uint8_t seconds) {
    ++writes;
    if (!writable) return false;
    persisted = seconds;
    return true;
}

int main(void) {
    RunnerSettings settings = {.auto_off_seconds = RUNNER_SETTINGS_DEFAULT};
    uint8_t pending = RUNNER_SETTINGS_KEEP;
    char reply[3];

    assert(runner_settings_request(&settings, "get auto_off_seconds", false, &pending, reply) == 0);
    assert(!strcmp(reply, "5"));
    assert(runner_settings_request(&settings, "set auto_off_seconds 10", false, &pending, reply) ==
           RUNNER_SETTINGS_DENIED);
    assert(pending == RUNNER_SETTINGS_KEEP);
    const char *invalid[] = {"get arbitrary", "set arbitrary 10", "set auto_off_seconds ",
                             "set auto_off_seconds 0", "set auto_off_seconds 61", "set auto_off_seconds -1",
                             "set auto_off_seconds 10x", "set auto_off_seconds 100"};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        assert(runner_settings_request(&settings, invalid[i], true, &pending, reply) ==
               RUNNER_SETTINGS_INVALID);
        assert(pending == RUNNER_SETTINGS_KEEP);
    }

    assert(runner_settings_request(&settings, "SET AUTO_OFF_SECONDS 60", true, &pending, reply) == 0);
    assert(runner_settings_request(&settings, "get auto_off_seconds", false, &pending, reply) == 0);
    assert(!strcmp(reply, "60") && settings.auto_off_seconds == 5);
    assert(runner_settings_finish(&settings, pending, false, save));
    assert(writes == 0 && persisted == 5);
    assert(runner_settings_finish(&settings, pending, true, save));
    assert(writes == 1 && persisted == 60 && settings.auto_off_seconds == 60);
    assert(runner_settings_finish(&settings, pending, true, save) && writes == 1);

    RunnerSettings rebooted = {.auto_off_seconds = persisted};
    pending = RUNNER_SETTINGS_KEEP;
    assert(runner_settings_request(&rebooted, "get auto_off_seconds", false, &pending, reply) == 0);
    assert(!strcmp(reply, "60"));
    writable = false;
    assert(!runner_settings_finish(&rebooted, 10, true, save));
    assert(rebooted.fault && rebooted.auto_off_seconds == 60 && persisted == 60);
    assert(runner_settings_request(&rebooted, "get auto_off_seconds", false, &pending, reply) ==
           RUNNER_SETTINGS_FAULT);
    puts("SETTINGS key and bounds, staging, failed invocation, commit and storage fault PASS");
}
