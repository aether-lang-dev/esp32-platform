#ifndef RUNNER_PEER_H
#define RUNNER_PEER_H
#include <stdbool.h>

enum { RUNNER_PEER_TEXT_LIMIT = 128, RUNNER_PEER_ERROR = 20 };
/* A fresh context borrows the caller's execution account. Returns the peer's
 * exit code or a host RC. The first Aether failure is returned in outcome. */
int runner_peer_call(const char *source, const char *command,
                     char result[RUNNER_PEER_TEXT_LIMIT + 1], int *outcome);

/* Validate the firmware-owned LABELS v1 contract under a bounded account. */
bool runner_peer_check(const char *source);

#endif
