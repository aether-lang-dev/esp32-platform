#include "libaether.h"
#include "rexxae.h"
#include "runner_peer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *source, const char *command, int rc, const char *expected) {
    char reply[RUNNER_PEER_TEXT_LIMIT + 1];
    int outcome;
    assert(runner_peer_call(source, command, reply, &outcome) == rc);
    assert(outcome == AETHER_OK);
    assert(strcmp(reply, expected) == 0);
}

int main(void) {
    FILE *file = fopen("scripts/services/labels.rexx", "rb");
    assert(file);
    char source[1024];
    size_t length = fread(source, 1, sizeof source, file);
    assert(feof(file) && !ferror(file));
    fclose(file);
    source[length] = 0;
    assert(runner_peer_check(source));
    assert(!runner_peer_check("EXIT 0"));
    assert(!runner_peer_check("SAY 'fake status'\nEXIT 0"));
    assert(!runner_peer_check("DO FOREVER\nLET n = 1\nEND"));

    RexxAE caller = aether_rexxae_create("ADDRESS LABELS 'status'\nSAY RC RESULT\nEXIT RC");
    assert(caller);
    assert(aether_rexxae_step(caller, 32) == REXXAE_COMMAND);
    assert(strcmp(aether_rexxae_address(caller), "labels") == 0);
    char reply[RUNNER_PEER_TEXT_LIMIT + 1];
    int outcome;
    assert(runner_peer_call(source, aether_rexxae_text(caller), reply, &outcome) == 0);
    assert(outcome == AETHER_OK && strcmp(reply, "PEER READY") == 0);
    assert(aether_rexxae_reply(caller, 0, reply, "") == 0);
    assert(aether_rexxae_step(caller, 32) == REXXAE_SAY);
    assert(strcmp(aether_rexxae_text(caller), "0 PEER READY") == 0);
    assert(aether_rexxae_step(caller, 32) == REXXAE_DONE);
    aether_rexxae_destroy(caller);

    check(source, "upper a\"b\\c", 0, "A\"B\\C");
    check(source, "unknown", 4, "");
    check(source, "upper abc\ndef", 4, "");
    char too_long[RUNNER_PEER_TEXT_LIMIT + 2];
    memset(too_long, 'a', sizeof too_long - 1);
    too_long[sizeof too_long - 1] = 0;
    check(source, too_long, 4, "");
    check("ADDRESS LABELS 'status'\nEXIT 0", "status", RUNNER_PEER_ERROR, "");
    check("DO FOREVER\nLET n = 1\nEND", "", RUNNER_PEER_ERROR, "");
    check("SAY 'a'\nSAY 'b'\nEXIT 0", "", RUNNER_PEER_ERROR, "");
    check("SAY COPIES('x', 129)\nEXIT 0", "", RUNNER_PEER_ERROR, "");
    AetherExecLimits limits = {.memory_bytes = 128 * 1024};
    AetherExecAccount *account = aether_exec_account_new(&limits);
    assert(account && aether_exec_use(account, NULL) == 0);
    check(source, "status", 0, "PEER READY");
    assert(aether_exec_account_used(account) == 0);
    assert(aether_exec_use(NULL, NULL) == 0);
    aether_exec_account_free(account);

    limits.memory_bytes = 1;
    account = aether_exec_account_new(&limits);
    assert(account && aether_exec_use(account, NULL) == 0);
    assert(runner_peer_call(source, "status", reply, &outcome) == RUNNER_PEER_ERROR);
    assert(outcome != AETHER_OK && !reply[0]);
    assert(aether_exec_account_used(account) == 0);
    assert(aether_exec_use(NULL, NULL) == 0);
    aether_exec_account_free(account);
    check(source, "status", 0, "PEER READY");
    puts("ADDRESS peer reply, rejection, recursion denial, budget and recovery PASS");
}
