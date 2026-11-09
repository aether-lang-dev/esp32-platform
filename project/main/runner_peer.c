#include "runner_peer.h"
#include "libaether.h"
#include "rexxae.h"
#include <stdbool.h>
#include <string.h>

enum { PEER_STEP_BUDGET = 32, PEER_STEPS = 4 };

static bool encode_command(const char *command, char *encoded) {
    if (strnlen(command, RUNNER_PEER_TEXT_LIMIT + 1) > RUNNER_PEER_TEXT_LIMIT) return false;
    *encoded++ = '"';
    for (const unsigned char *p = (const unsigned char *)command; *p; ++p) {
        if (*p < 32 || *p > 126) return false;
        if (*p == '"' || *p == '\\') *encoded++ = '\\';
        *encoded++ = (char)*p;
    }
    *encoded++ = '"';
    *encoded = 0;
    return true;
}

int runner_peer_call(const char *source, const char *command,
                     char result[RUNNER_PEER_TEXT_LIMIT + 1], int *outcome) {
    char encoded[2 * RUNNER_PEER_TEXT_LIMIT + 3];
    *outcome = AETHER_OK;
    *result = 0;
    if (!encode_command(command, encoded)) return 4;

    int rc = RUNNER_PEER_ERROR;
    bool replied = false;
    RexxAE peer = aether_rexxae_create(source);
    *outcome = aether_last_outcome();
    if (*outcome != AETHER_OK || !peer) goto cleanup;
    int seeded = aether_rexxae_set_variable_json(peer, "ADDRESS_COMMAND", encoded);
    *outcome = aether_last_outcome();
    if (*outcome != AETHER_OK || seeded != 0) goto cleanup;

    for (unsigned i = 0; i < PEER_STEPS; ++i) {
        int step = aether_rexxae_step(peer, PEER_STEP_BUDGET);
        *outcome = aether_last_outcome();
        if (*outcome != AETHER_OK) break;
        if (step == REXXAE_DONE) {
            rc = aether_rexxae_exit_code(peer);
            *outcome = aether_last_outcome();
            break;
        }
        if (step == REXXAE_RUNNABLE) continue;
        /* No functions or ADDRESS environments are mounted in the peer. */
        if (step != REXXAE_SAY || replied) break;
        const char *text = aether_rexxae_text(peer);
        *outcome = aether_last_outcome();
        if (*outcome != AETHER_OK || !text ||
            strnlen(text, RUNNER_PEER_TEXT_LIMIT + 1) > RUNNER_PEER_TEXT_LIMIT) break;
        strcpy(result, text);
        replied = true;
    }

cleanup:
    if (peer) {
        /* Like the runner destructor, cleanup is trusted and quota-exempt. */
        AetherExecAccount *account = NULL;
        if (aether_exec_use(NULL, &account) != 0) rc = RUNNER_PEER_ERROR;
        aether_rexxae_destroy(peer);
        if (*outcome == AETHER_OK) *outcome = aether_last_outcome();
        if (aether_exec_use(account, NULL) != 0) rc = RUNNER_PEER_ERROR;
    }
    if (*outcome != AETHER_OK) rc = RUNNER_PEER_ERROR;
    if (rc != 0) *result = 0;
    return rc;
}

bool runner_peer_check(const char *source) {
    char encoded[2 * RUNNER_PEER_TEXT_LIMIT + 3];
    char response[RUNNER_PEER_TEXT_LIMIT + 1];
    int outcome;
    AetherExecAccount *previous = NULL;
    if (aether_exec_use(NULL, &previous) != 0) return false;
    const AetherExecLimits limits = {
        .memory_bytes = 64 * 1024, .deadline_ms = 1500,
        .deadline_counts = AETHER_DEADLINE_ELAPSED, .parent = previous,
    };
    AetherExecAccount *account = aether_exec_account_new(&limits);
    bool healthy = account && aether_exec_use(account, NULL) == 0 &&
        runner_peer_call(source, "status", response, &outcome) == 0 && *response &&
        encode_command(response, encoded) &&
        runner_peer_call(source, "upper usb Probe", response, &outcome) == 0 &&
        strcmp(response, "USB PROBE") == 0 &&
        runner_peer_call(source, "unknown", response, &outcome) == 4 &&
        outcome == AETHER_OK;
    if (aether_exec_use(NULL, NULL) != 0) healthy = false;
    if (account) {
        healthy = healthy && aether_exec_account_used(account) == 0;
        aether_exec_account_free(account);
    }
    return aether_exec_use(previous, NULL) == 0 && healthy;
}
