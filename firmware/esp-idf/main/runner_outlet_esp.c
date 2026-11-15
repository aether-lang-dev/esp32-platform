/* Native timed-outlet app: one host-owned lease shared by all granted clients. */
#include "runner_outlet.h"
#include "runner_switch.h"
#include <stdio.h>
static const char outlet_owner;

int runner_outlet_command(const char *command, unsigned grants, char *result) {
    int action, seconds;
    result[0] = 0;
    int rc = runner_outlet_request(command, grants, &action, &seconds);
    if (rc) return rc;
    if (!runner_switch_available()) return 20;
    if (action != OUTLET_STATUS) {
        rc = runner_switch_owner_set(&outlet_owner, action == OUTLET_START ? seconds : 0, false);
        if (rc) return rc;
    }
    bool busy;
    int remaining = runner_switch_owner_remaining(&outlet_owner, &busy);
    snprintf(result, RUNNER_OUTLET_TEXT + 1,
        "{\"app\":\"outlet\",\"state\":\"%s\",\"remaining\":%d,\"busy\":%s}",
        remaining ? "running" : "idle", remaining, busy ? "true" : "false");
    return 0;
}

void runner_outlet_button(void) {
    /* Toggle under the same driver lock as RPC, ownership and timer expiry. */
    (void)runner_switch_owner_set(&outlet_owner, RUNNER_OUTLET_DEFAULT_SECONDS, true);
}
