#include "runner_outlet.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    int action, seconds;
    assert(runner_outlet_request("STATUS", 0, &action, &seconds) == 13);
    assert(runner_outlet_request("status", RUNNER_GRANT_OUTLET_READ, &action, &seconds) == 0 && action == OUTLET_STATUS);
    assert(runner_outlet_request("CANCEL", RUNNER_GRANT_OUTLET_READ, &action, &seconds) == 13);
    assert(runner_outlet_request("cancel", RUNNER_GRANT_OUTLET_CANCEL, &action, &seconds) == 0 && action == OUTLET_CANCEL);
    assert(runner_outlet_request("START 5", RUNNER_GRANT_OUTLET_CANCEL, &action, &seconds) == 13);
    assert(runner_outlet_request("start 60", RUNNER_GRANT_OUTLET_CONTROL, &action, &seconds) == 0 && seconds == 60);
    const char *bad[] = {"", "START", "START 0", "START 61", "START -1", "START 2 extra", "CANCEL other", "STATUS x"};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i)
        assert(runner_outlet_request(bad[i], ~0u, &action, &seconds) == 4);
    puts("OUTLET separate read/start/cancel grants and duration bounds PASS");
}
