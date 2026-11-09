#ifndef RUNNER_STORE_H
#define RUNNER_STORE_H
#include <stdbool.h>
#include "runner_bundle.h"
typedef struct {
    uint32_t active_version, highest_version, trial_version;
    int active_slot;
    bool fault;
    char active_fingerprint[65];
} RunnerStoreInfo;
typedef enum { RUNNER_STORE_APP, RUNNER_STORE_LABELS } RunnerStoreId;
#define RUNNER_LABELS_BUNDLE "peer.labels.v1"

int runner_store_init_at(RunnerStoreId id);
void runner_store_info_at(RunnerStoreId id, RunnerStoreInfo *info);
int runner_store_offer_at(RunnerStoreId id, const RunnerBundle *candidate);
int runner_store_stage_at(RunnerStoreId id, unsigned char *bytes, size_t length,
                          const RunnerTrustedKey *keys, size_t count, RunnerBundle *parsed);
int runner_store_finish_at(RunnerStoreId id, bool healthy);
unsigned char *runner_store_load_active_at(RunnerStoreId id, const RunnerTrustedKey *keys,
                                          size_t count, RunnerBundle *parsed);

static inline int runner_store_init(void) { return runner_store_init_at(RUNNER_STORE_APP); }
static inline void runner_store_info(RunnerStoreInfo *info) { runner_store_info_at(RUNNER_STORE_APP, info); }
static inline int runner_store_offer(const RunnerBundle *bundle) {
    return runner_store_offer_at(RUNNER_STORE_APP, bundle);
}
static inline int runner_store_stage(unsigned char *bytes, size_t length,
                                     const RunnerTrustedKey *keys, size_t count, RunnerBundle *parsed) {
    return runner_store_stage_at(RUNNER_STORE_APP, bytes, length, keys, count, parsed);
}
static inline int runner_store_finish(bool healthy) { return runner_store_finish_at(RUNNER_STORE_APP, healthy); }
static inline unsigned char *runner_store_load_active(const RunnerTrustedKey *keys, size_t count, RunnerBundle *parsed) {
    return runner_store_load_active_at(RUNNER_STORE_APP, keys, count, parsed);
}
#endif
