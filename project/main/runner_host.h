#ifndef RUNNER_HOST_H
#define RUNNER_HOST_H
#include <stdbool.h>
#include <stdint.h>
#define SCRIPT_LIMIT 8192
enum { RUNNER_GRANT_UPTIME = 1u, RUNNER_GRANT_WAIT = 2u,
       RUNNER_GRANT_TOUCH = 8u, RUNNER_GRANT_BUTTONS = 16u, RUNNER_GRANT_LABELS = 32u,
       RUNNER_GRANT_SWITCH_READ = 64u, RUNNER_GRANT_SWITCH_CONTROL = 128u,
       RUNNER_GRANT_SWITCH = RUNNER_GRANT_SWITCH_READ | RUNNER_GRANT_SWITCH_CONTROL,
       RUNNER_GRANT_SETTINGS_READ = 256u, RUNNER_GRANT_SETTINGS_WRITE = 512u,
       RUNNER_GRANT_SETTINGS = RUNNER_GRANT_SETTINGS_READ | RUNNER_GRANT_SETTINGS_WRITE,
       RUNNER_GRANT_TIMERS = RUNNER_GRANT_UPTIME | RUNNER_GRANT_WAIT };
enum { RUNNER_GRANT_APPS_READ = 1024u, RUNNER_GRANT_APPS_RUN = 2048u };
#ifdef RUNNER_PSK
#define RUNNER_REPL_GRANTS (RUNNER_GRANT_TIMERS | RUNNER_GRANT_APPS_READ | RUNNER_GRANT_APPS_RUN)
#endif
#ifdef RUNNER_USB
#include "cyd_touch.h"
#define RUNNER_SIGNED_GRANTS (RUNNER_GRANT_TIMERS | RUNNER_GRANT_TOUCH | RUNNER_GRANT_BUTTONS | RUNNER_GRANT_LABELS | RUNNER_GRANT_SWITCH | RUNNER_GRANT_SETTINGS)
#else
#define RUNNER_SIGNED_GRANTS RUNNER_GRANT_TIMERS
#endif
typedef struct {
    int runtime_outcome;
    uint64_t retained_bytes;
    unsigned steps;
    unsigned yields;
    unsigned waits;
    unsigned logs;
    bool cancelled;
} RunnerStats;
#ifdef RUNNER_PSK
typedef int (*RunnerAppsCommand)(const char *command, unsigned grants, char *result);
typedef struct {
    RunnerAppsCommand apps;
    void *context, *account, *memory;
    void (*output)(void *argument, const char *kind, const char *text);
    bool (*cancelled)(void *argument);
    void *argument;
} RunnerSession;
int runner_session_execute(RunnerSession *session, const char *source, unsigned grants, RunnerStats *stats);
void runner_session_close(RunnerSession *session);
#endif

/* Called from the runner task only; source is copied by the engine. */
int runner_execute(const char *source, unsigned grants, RunnerStats *stats);
#ifdef RUNNER_USB
const char *runner_labels_source(void);
int runner_execute_touch(const char *source, unsigned grants, RunnerStats *stats, const CydTouch *tap, uint32_t version);
#endif
#endif
