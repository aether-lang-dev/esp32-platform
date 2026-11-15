#ifndef RUNNER_BUNDLE_H
#define RUNNER_BUNDLE_H
#include <stddef.h>
#include <stdint.h>
#include "runner_host.h"
#define RUNNER_BUNDLE_LIMIT (24 + 64 + 64 + SCRIPT_LIMIT + 64)
typedef struct {
    const char *id;
    const unsigned char *public_key; /* SEC1 uncompressed P-256 point, 65 bytes */
} RunnerTrustedKey;
typedef struct {
    uint32_t version;
    unsigned grants;
    char key_id[65];
    char name[65];
    const unsigned char *source; /* Borrowed, not NUL-terminated */
    size_t source_length;
    char fingerprint[65];
} RunnerBundle;
/* Verifies the complete source/manifest binding against a host-owned trust set.
 * No flash writes, engine calls or trust changes on failure. */
/* Bounded length hint from a 24-byte header; not authentication. */
size_t runner_bundle_encoded_size(const unsigned char *header, size_t available);
int runner_bundle_verify(const unsigned char *data, size_t size,
                         const RunnerTrustedKey *keys, size_t key_count,
                         RunnerBundle *result);
#endif
