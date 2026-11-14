#include "runner_outlet.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

int runner_outlet_request(const char *command, unsigned grants, int *action, int *seconds) {
    unsigned needed;
    *seconds = 0;
    if (!strcasecmp(command, "STATUS")) {
        *action = OUTLET_STATUS;
        needed = RUNNER_GRANT_OUTLET_READ;
    } else if (!strcasecmp(command, "CANCEL")) {
        *action = OUTLET_CANCEL;
        needed = RUNNER_GRANT_OUTLET_CANCEL | RUNNER_GRANT_OUTLET_CONTROL;
    } else if (!strncasecmp(command, "START ", 6)) {
        const char *digits = command + 6;
        size_t count = strspn(digits, "0123456789");
        if (!count || count > 2 || digits[count]) return 4;
        *seconds = (int)strtol(digits, NULL, 10);
        if (*seconds < 1 || *seconds > 60) return 4;
        *action = OUTLET_START;
        needed = RUNNER_GRANT_OUTLET_CONTROL;
    } else return 4;
    return grants & needed ? 0 : 13;
}
