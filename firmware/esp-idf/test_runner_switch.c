#include "runner_switch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    RunnerSwitch state = {0};
    int pending = RUNNER_SWITCH_KEEP;
    const char *reply;
    assert(runner_switch_request("status", false, false, &pending, &reply) == 0);
    assert(!strcmp(reply, "OFF"));
    assert(runner_switch_request("on 5", false, false, &pending, &reply) == RUNNER_SWITCH_DENIED);
    assert(runner_switch_request("off", false, false, &pending, &reply) == RUNNER_SWITCH_DENIED);
    assert(pending == RUNNER_SWITCH_KEEP && state.until_us == 0);

    const char *invalid[] = {"on", "on 0", "on 61", "on -1", "on 1x", "on 2 ", "on 999", "toggle"};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        assert(runner_switch_request(invalid[i], true, false, &pending, &reply) == RUNNER_SWITCH_BAD_COMMAND);
        assert(pending == RUNNER_SWITCH_KEEP);
    }

    assert(runner_switch_request("ON 5", true, false, &pending, &reply) == 0);
    assert(pending == 5 && state.until_us == 0 && !strcmp(reply, "ON"));
    assert(runner_switch_request("status", false, false, &pending, &reply) == 0);
    assert(!strcmp(reply, "ON")); /* The caller sees its staged request. */
    runner_switch_apply(&state, pending, 100);
    assert(!runner_switch_expire(&state, 5000099));
    assert(runner_switch_expire(&state, 5000100));
    assert(!runner_switch_expire(&state, 5000101));
    assert(state.until_us == 0);

    runner_switch_apply(&state, 1, 100);
    runner_switch_apply(&state, 2, 200);
    assert(!runner_switch_expire(&state, 1000100)); /* Old deadline cannot turn it off. */
    assert(runner_switch_request("OFF", true, true, &pending, &reply) == 0);
    assert(!strcmp(reply, "OFF") && pending == 0);
    runner_switch_apply(&state, pending, 300);
    assert(!state.until_us);
    puts("SWITCH permissions, staging, deadline replacement and expiration PASS");
}
