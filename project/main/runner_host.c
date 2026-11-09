/* Shared bounded runner loop, plus the standalone flash-script self-test. */
#include "rexxae.h"
#include "libaether.h"
#include "runner_host.h"
#if defined(RUNNER_CYD_CONSOLE) || defined(RUNNER_WIFI) || defined(RUNNER_USB)
#include "cyd_console.h"
#endif
#ifdef RUNNER_CYD_CONSOLE
#include "cyd_touch.h"
#endif
#ifndef RUNNER_HTTP
#include "runner_peripherals.h"
#endif
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef RUNNER_LED_GPIO
#include "driver/gpio.h"
_Static_assert(GPIO_IS_VALID_OUTPUT_GPIO(RUNNER_LED_GPIO), "LED pin must support output");
#endif

#define STEP_BUDGET 32
#define STEP_LIMIT 2048
#define DEADLINE_US 5000000
#define RUNNER_MEMORY_LIMIT (128u * 1024u)

#ifdef RUNNER_USB
#include "runner_peer.h"
#include "runner_switch.h"
#include "runner_settings.h"


static bool button_label_valid(const char *text) {
    if (!text || strnlen(text, CYD_BUTTON_TEXT + 1) > CYD_BUTTON_TEXT) return false;
    for (; *text; ++text) {
        if ((unsigned char)*text < 32 || (unsigned char)*text > 126) return false;
    }
    return true;
}

/* Protocol receipts occupy their own serial lines. Prefix every line of
 * script text, including newlines in typed values or errors. */
static void usb_output(const char *prefix, const char *text) {
    for (;;) {
        size_t length = strcspn(text, "\r\n");
        printf("%s%.*s\n", prefix, (int)length, text);
        text += length;
        text += strspn(text, "\r\n");
        if (!*text) return;
    }
}
#endif

/* Every Aether export has two results: its ABI value and its runtime outcome.
 * A failed export returns zero, which is also RexxAE's DONE/success value. */
static bool engine_ok(RunnerStats *stats, const char *operation) {
    int outcome = aether_last_outcome();
    if (outcome == AETHER_OK) return true;
    stats->runtime_outcome = outcome;
    if (outcome == AETHER_DEADLINE || outcome == AETHER_CANCELLED) stats->cancelled = true;
    printf("RUNNER runtime failure in %s: %s (outcome %d)\n",
           operation, aether_last_outcome_text(), outcome);
    return false;
}
#define ENGINE_READ(target, call) do { \
    (target) = (call); \
    if (!engine_ok(stats, #call)) goto cleanup; \
} while (0)
#define ENGINE_ACCEPT(call) do { \
    int _runner_rc; \
    ENGINE_READ(_runner_rc, call); \
    if (_runner_rc != 0) goto cleanup; \
} while (0)

#ifndef RUNNER_HTTP
/* This grant is private to the standalone demo. */
enum { RUNNER_GRANT_LED = 4u };
static bool led_write(int level) {
#ifdef RUNNER_LED_GPIO
    return gpio_set_level(RUNNER_LED_GPIO, level) == ESP_OK;
#else
    (void)level;
    return true;
#endif
}
#endif

static int execute(const char *source, unsigned grants, RunnerStats *stats
#ifndef RUNNER_HTTP
                   , unsigned *led_trace, DemoPeripheral peripheral
#elif defined(RUNNER_USB)
                   , const CydTouch *tap, uint32_t version, int *switch_seconds, uint8_t *setting_seconds
#endif
) {
    memset(stats, 0, sizeof *stats);
#ifndef RUNNER_HTTP
    bool led_on = false;
    const char *peripheral_function = demo_function(peripheral);
    if (grants & ~(RUNNER_GRANT_TIMERS | RUNNER_GRANT_LED)) return -1;
#else
    if (grants & ~RUNNER_SIGNED_GRANTS) return -1;
#endif
#ifdef RUNNER_USB
    CydLayout screen = {0};
    bool screen_pending = false;
#endif
    RexxAE ctx = NULL;
    int result = -1;
    AetherExecAccount *previous = NULL;
    if (aether_exec_use(NULL, &previous) != 0) return -1;
    AetherExecLimits limits = {
        .memory_bytes = RUNNER_MEMORY_LIMIT,
        .deadline_ms = DEADLINE_US / 1000,
        .deadline_counts = AETHER_DEADLINE_ELAPSED,
        .parent = previous,
    };
    AetherExecAccount *account = aether_exec_account_new(&limits);
    if (!account || aether_exec_use(account, NULL) != 0) goto cleanup;
    const int64_t deadline = esp_timer_get_time() + DEADLINE_US;
    int64_t wake = 0;
#ifdef RUNNER_CYD_CONSOLE
    int64_t touch_deadline = 0;
#endif
    ENGINE_READ(ctx, aether_rexxae_create(source));
    if (!ctx) goto cleanup;
    ENGINE_ACCEPT(aether_rexxae_set_log_level(ctx, 2));
    if (grants & RUNNER_GRANT_UPTIME) ENGINE_ACCEPT(aether_rexxae_register_function(ctx, "UPTIME_MS", 0, 0));
    if (grants & RUNNER_GRANT_WAIT) ENGINE_ACCEPT(aether_rexxae_register_function(ctx, "WAIT_MS", 1, 1));
#ifdef RUNNER_USB
    if (grants & RUNNER_GRANT_BUTTONS) {
        ENGINE_ACCEPT(aether_rexxae_register_function(ctx, "DISPLAY_BUTTONS", 4, 4));
        ENGINE_ACCEPT(aether_rexxae_register_function(ctx, "DISPLAY_FRAME", 2, 2));
        ENGINE_ACCEPT(aether_rexxae_register_function(ctx, "DISPLAY_BUTTON", 6, 6));
        if (grants & RUNNER_GRANT_TOUCH) {
            ENGINE_ACCEPT(aether_rexxae_register_function(ctx, "BUTTON_EVENT", 0, 0));
        }
    }
    if (grants & RUNNER_GRANT_TOUCH) ENGINE_ACCEPT(aether_rexxae_register_function(ctx, "TOUCH_EVENT", 0, 0));
#endif
#ifdef RUNNER_CYD_CONSOLE
    if (cyd_touch_ready()) ENGINE_ACCEPT(aether_rexxae_register_function(ctx, "WAIT_TOUCH", 1, 1));
#endif
#ifndef RUNNER_HTTP
    if (grants & RUNNER_GRANT_LED) ENGINE_ACCEPT(aether_rexxae_register_function(ctx, "LED_WRITE", 1, 1));
    if (peripheral_function) {
        int arity = demo_arity(peripheral);
        ENGINE_ACCEPT(aether_rexxae_register_function(ctx, peripheral_function, arity, arity));
    }
#endif
    for (;;) {
        vTaskDelay(1);
        if (!stats->cancelled && (stats->steps >= STEP_LIMIT || esp_timer_get_time() >= deadline)) {
            stats->cancelled = true;
            ENGINE_ACCEPT(aether_rexxae_cancel(ctx, "runner budget exhausted"));
            wake = 0;
#ifdef RUNNER_CYD_CONSOLE
            touch_deadline = 0;
#endif
        }
#ifdef RUNNER_CYD_CONSOLE
        if (touch_deadline) {
            CydTouch tap;
            if (cyd_touch_take(&tap)) {
                char reply[64];
                snprintf(reply, sizeof reply, "{\"x\":%u,\"y\":%u,\"pressure\":%u}",
                         tap.x, tap.y, tap.pressure);
                ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, reply, ""));
            } else if (esp_timer_get_time() < touch_deadline) {
                continue;
            } else {
                ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, "null", ""));
            }
            touch_deadline = 0;
        }
#endif
        if (wake) {
            if (esp_timer_get_time() < wake) continue;
            wake = 0;
            ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, "0", ""));
        }
        int status;
        ENGINE_READ(status, aether_rexxae_step(ctx, STEP_BUDGET));
        ++stats->steps;
        if (status == REXXAE_DONE) {
            ENGINE_READ(result, aether_rexxae_exit_code(ctx));
            break;
        }
        if (status == REXXAE_ERROR) {
            const char *text;
            ENGINE_READ(text, aether_rexxae_error(ctx));
#ifdef RUNNER_USB
            usb_output("RUNNER error: ", text ? text : "no error text");
#else
            printf("RUNNER error: %s\n", text ? text : "no error text");
#endif
#if defined(RUNNER_CYD_CONSOLE) || defined(RUNNER_WIFI) || defined(RUNNER_USB)
#ifdef RUNNER_USB
            if (!(grants & RUNNER_GRANT_BUTTONS))
#endif
            {
                cyd_console_line("Rexx error");
                if (text) cyd_console_line(text);
            }
#endif
            break;
        }
        if (status == REXXAE_RUNNABLE) {
            ++stats->yields;
        } else if (status == REXXAE_SAY || status == REXXAE_LOG) {
            int level = -1;
            const char *text;
            if (status == REXXAE_LOG) {
                ENGINE_READ(level, aether_rexxae_log_level(ctx));
                ++stats->logs;
            }
            ENGINE_READ(text, aether_rexxae_text(ctx));
            if (!text) goto cleanup;
#ifdef RUNNER_USB
            char prefix[32];
            if (level < 0) usb_output("RUNNER> ", text);
            else {
                snprintf(prefix, sizeof prefix, "RUNNER log %d: ", level);
                usb_output(prefix, text);
            }
#else
            if (level < 0) printf("RUNNER> %s\n", text);
            else printf("RUNNER log %d: %s\n", level, text);
#endif
#if defined(RUNNER_CYD_CONSOLE) || defined(RUNNER_WIFI) || defined(RUNNER_USB)
#ifdef RUNNER_USB
            if (!(grants & RUNNER_GRANT_BUTTONS))
#endif
            cyd_console_line(text);
#endif
        } else if (status == REXXAE_COMMAND) {
#ifdef RUNNER_USB
            const char *target;
            ENGINE_READ(target, aether_rexxae_address(ctx));
            if (target && (grants & RUNNER_GRANT_SETTINGS) && !strcasecmp(target, "SETTINGS")) {
                const char *command;
                ENGINE_READ(command, aether_rexxae_text(ctx));
                if (!command) goto cleanup;
                bool can_write = tap && (grants & RUNNER_GRANT_SETTINGS_WRITE);
                char response[3];
                int rc = runner_settings_command(command, can_write, setting_seconds, response);
                const char *error = rc == RUNNER_SETTINGS_DENIED ? "SETTINGS write is not granted for this invocation" :
                                    rc == RUNNER_SETTINGS_INVALID ? "invalid SETTINGS command" :
                                    rc == RUNNER_SETTINGS_FAULT ? "SETTINGS store unavailable" : "";
                ENGINE_ACCEPT(aether_rexxae_reply(ctx, rc, response, error));
                continue;
            }
            if (target && (grants & RUNNER_GRANT_SWITCH) && !strcasecmp(target, "SWITCH")) {
                const char *command;
                ENGINE_READ(command, aether_rexxae_text(ctx));
                if (!command) goto cleanup;
                bool control = tap && (grants & RUNNER_GRANT_SWITCH_CONTROL) && runner_switch_available();
                const char *response;
                int rc = runner_switch_request(command, control, runner_switch_on(), switch_seconds, &response);
                const char *error = rc == RUNNER_SWITCH_DENIED ? "SWITCH control is not granted for this invocation" :
                                    rc == RUNNER_SWITCH_BAD_COMMAND ? "invalid SWITCH command" : "";
                ENGINE_ACCEPT(aether_rexxae_reply(ctx, rc, response, error));
                continue;
            }
            if (target && (grants & RUNNER_GRANT_LABELS) && !strcasecmp(target, "LABELS")) {
                const char *command;
                ENGINE_READ(command, aether_rexxae_text(ctx));
                if (!command) goto cleanup;
                char response[RUNNER_PEER_TEXT_LIMIT + 1];
                int outcome;
                const char *peer_source = runner_labels_source();
                int rc = peer_source ? runner_peer_call(peer_source, command, response, &outcome) : RUNNER_PEER_ERROR;
                if (!peer_source) {
                    response[0] = 0;
                    outcome = AETHER_OK;
                }
                if (outcome != AETHER_OK) {
                    stats->runtime_outcome = outcome;
                    stats->cancelled = outcome == AETHER_DEADLINE || outcome == AETHER_CANCELLED;
                    goto cleanup;
                }
                ENGINE_ACCEPT(aether_rexxae_reply(ctx, rc, response,
                    rc == 0 ? "" : rc == RUNNER_PEER_ERROR ? "LABELS peer failed" : "LABELS command rejected"));
                continue;
            }
#endif
            ENGINE_ACCEPT(aether_rexxae_cancel(ctx, "ADDRESS is not mounted"));
        } else if (status == REXXAE_FUNCTION) {
            const char *name;
            ENGINE_READ(name, aether_rexxae_function(ctx));
            if (!name) goto cleanup;
            if (!strcmp(name, "uptime_ms") || !strcmp(name, "UPTIME_MS")) {
                char value[32];
                snprintf(value, sizeof value, "%lld", (long long)(esp_timer_get_time() / 1000));
                ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, value, ""));
            } else if (!strcmp(name, "wait_ms") || !strcmp(name, "WAIT_MS")) {
                int type;
                ENGINE_READ(type, aether_rexxae_argument_type(ctx, 0));
                if (type != 2) {
                    ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, "null", "WAIT_MS requires an integer number"));
                    continue;
                }
                const char *text;
                ENGINE_READ(text, aether_rexxae_argument_text(ctx, 0));
                if (!text) goto cleanup;
                char *end;
                errno = 0;
                long delay = strtol(text, &end, 10);
                if (errno || end == text || *end || delay < 0 || delay > 1000) {
                    ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, "null", "WAIT_MS must be an integer from 0 to 1000"));
                    continue;
                }
                wake = esp_timer_get_time() + delay * 1000 + 1;
                ++stats->waits;
#ifdef RUNNER_USB
            } else if ((grants & RUNNER_GRANT_BUTTONS) && !strcasecmp(name, "DISPLAY_BUTTONS")) {
                screen = (CydLayout){
                    .buttons = {{.col = 1, .row = 4, .width = 13, .height = 4},
                                {.col = 1, .row = 10, .width = 13, .height = 4}},
                    .count = 2, .legacy = true,
                };
                char *labels[] = {screen.title, screen.buttons[0].label,
                                  screen.buttons[1].label, screen.status};
                bool valid = true;
                for (int i = 0; i < 4; ++i) {
                    int type;
                    const char *label;
                    ENGINE_READ(type, aether_rexxae_argument_type(ctx, i));
                    ENGINE_READ(label, aether_rexxae_argument_text(ctx, i));
                    if (type != 3 || !button_label_valid(label)) {
                        valid = false;
                        break;
                    }
                    strcpy(labels[i], label);
                }
                ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, valid ? "true" : "null",
                    valid ? "" : "DISPLAY_BUTTONS requires four ASCII strings of at most 13 characters"));
                screen_pending = valid;
            } else if ((grants & RUNNER_GRANT_BUTTONS) && !strcasecmp(name, "DISPLAY_FRAME")) {
                if (screen.legacy) screen = (CydLayout){0};
                char *labels[] = {screen.title, screen.status};
                bool valid = true;
                for (int i = 0; i < 2; ++i) {
                    int type;
                    const char *label;
                    ENGINE_READ(type, aether_rexxae_argument_type(ctx, i));
                    ENGINE_READ(label, aether_rexxae_argument_text(ctx, i));
                    if (type != 3 || !button_label_valid(label)) {
                        valid = false;
                        break;
                    }
                    strcpy(labels[i], label);
                }
                ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, valid ? "true" : "null",
                    valid ? "" : "DISPLAY_FRAME requires two ASCII strings of at most 13 characters"));
                screen_pending = valid;
            } else if ((grants & RUNNER_GRANT_BUTTONS) && !strcasecmp(name, "DISPLAY_BUTTON")) {
                CydButton button = {0};
                unsigned *fields[] = {&button.id, &button.col, &button.row, &button.width, &button.height};
                bool valid = true;
                for (int i = 0; i < 5; ++i) {
                    int type;
                    const char *number;
                    int index = i ? i + 1 : 0;
                    ENGINE_READ(type, aether_rexxae_argument_type(ctx, index));
                    ENGINE_READ(number, aether_rexxae_argument_text(ctx, index));
                    size_t length = number ? strspn(number, "0123456789") : 0;
                    if (type != 2 || !length || length > 3 || number[length]) {
                        valid = false;
                        break;
                    }
                    *fields[i] = (unsigned)strtoul(number, NULL, 10);
                }
                if (valid) {
                    int type;
                    const char *label;
                    ENGINE_READ(type, aether_rexxae_argument_type(ctx, 1));
                    ENGINE_READ(label, aether_rexxae_argument_text(ctx, 1));
                    valid = type == 3 && button_label_valid(label);
                    if (valid) {
                        strcpy(button.label, label);
                        valid = cyd_layout_add(&screen, &button);
                    }
                }
                ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, valid ? "true" : "null",
                    valid ? "" : "DISPLAY_BUTTON requires a unique ID, fitting label and free on-screen rectangle"));
            } else if ((grants & RUNNER_GRANT_BUTTONS) && (grants & RUNNER_GRANT_TOUCH) &&
                       !strcasecmp(name, "BUTTON_EVENT")) {
                unsigned id = cyd_console_button_at(tap, version);
                char value[16] = "null";
                if (id) snprintf(value, sizeof value, "%u", id);
                ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, value, ""));
            } else if ((grants & RUNNER_GRANT_TOUCH) && !strcasecmp(name, "TOUCH_EVENT")) {
                char value[80] = "null";
                if (tap) {
                    snprintf(value, sizeof value, "{\"x\":%u,\"y\":%u,\"pressure\":%u}",
                             tap->x, tap->y, tap->pressure);
                }
                ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, value, ""));
#endif
#ifdef RUNNER_CYD_CONSOLE
            } else if (!strcasecmp(name, "WAIT_TOUCH")) {
                int type;
                const char *text;
                ENGINE_READ(type, aether_rexxae_argument_type(ctx, 0));
                ENGINE_READ(text, aether_rexxae_argument_text(ctx, 0));
                char *end = NULL;
                errno = 0;
                long delay = type == 2 && text ? strtol(text, &end, 10) : -1;
                if (errno || !end || end == text || *end || delay < 0 || delay > 2000) {
                    ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, "null", "WAIT_TOUCH requires an integer from 0 to 2000"));
                    continue;
                }
                touch_deadline = esp_timer_get_time() + delay * 1000 + 1;
#endif
#ifndef RUNNER_HTTP
            } else if (!strcmp(name, "led_write") || !strcmp(name, "LED_WRITE")) {
                int type;
                const char *text;
                ENGINE_READ(type, aether_rexxae_argument_type(ctx, 0));
                ENGINE_READ(text, aether_rexxae_argument_text(ctx, 0));
                if (type != 2 || !text || (strcmp(text, "0") && strcmp(text, "1"))) {
                    ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, "null", "LED_WRITE requires 0 or 1"));
                    continue;
                }
                int level = text[0] - '0';
                if (!led_write(level)) {
                    ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, "null", "LED write failed"));
                    continue;
                }
                led_on = level == 1;
                *led_trace = (*led_trace << 2) | (unsigned)(level + 1);
                ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, "0", ""));
            } else if (peripheral_function && !strcasecmp(name, peripheral_function)) {
                int type;
                const char *argument;
                char reply[160];
                type = -1;
                argument = NULL;
                if (demo_arity(peripheral)) {
                    ENGINE_READ(type, aether_rexxae_argument_type(ctx, 0));
                    ENGINE_READ(argument, aether_rexxae_argument_text(ctx, 0));
                }
                const char *error = demo_invoke(peripheral, type, argument, reply, sizeof reply);
                ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, error ? "null" : reply, error ? error : ""));
#endif
            } else ENGINE_ACCEPT(aether_rexxae_reply_value(ctx, "null", "function is not mounted"));
        } else goto cleanup;
    }
cleanup:
#ifndef RUNNER_HTTP
    if (led_on) {
        if (led_write(0)) *led_trace = (*led_trace << 2) | 1u;
        else result = -1;
    }
#endif
    /* The trusted destructor must run outside the exhausted account. */
    aether_exec_use(NULL, NULL);
    if (ctx) {
        aether_rexxae_destroy(ctx);
        if (!engine_ok(stats, "destroy")) result = -1;
    }
    if (account) {
        stats->retained_bytes = aether_exec_account_used(account);
        aether_exec_account_free(account);
        if (stats->retained_bytes) printf("RUNNER cleanup retained %llu bytes\n", (unsigned long long)stats->retained_bytes);
    }
    if (aether_exec_use(previous, NULL) != 0) result = -1;
    if (stats->runtime_outcome != AETHER_OK || stats->retained_bytes) result = -1;
#ifdef RUNNER_USB
    if (result == 0 && screen_pending && !cyd_console_layout(&screen, version)) {
        puts("SIGNED USB display unavailable");
        result = -1;
    }
#endif
    return result;
}
#ifdef RUNNER_USB
int runner_execute_touch(const char *source, unsigned grants, RunnerStats *stats, const CydTouch *tap, uint32_t version) {
    int switch_seconds = RUNNER_SWITCH_KEEP;
    uint8_t setting_seconds = RUNNER_SETTINGS_KEEP;
    int result = execute(source, grants, stats, tap, version, &switch_seconds, &setting_seconds);
    if (result == 0 && !runner_settings_commit(setting_seconds)) {
        puts("SIGNED USB SETTINGS write failed");
        result = -1;
    }
    if (tap && (grants & RUNNER_GRANT_SWITCH_CONTROL) &&
        !runner_switch_finish(switch_seconds, result == 0)) result = -1;
    return result;
}
#endif

int runner_execute(const char *source, unsigned grants, RunnerStats *stats) {
    if (grants & ~RUNNER_GRANT_TIMERS) return -1;
#ifdef RUNNER_USB
    return runner_execute_touch(source, grants, stats, NULL, 0);
#elif defined(RUNNER_HTTP)
    return execute(source, grants, stats);
#else
    return execute(source, grants, stats, NULL, DEMO_NONE);
#endif
}
#undef ENGINE_READ
#undef ENGINE_ACCEPT

#ifndef RUNNER_HTTP
static char *load_script(void) {
    const esp_partition_t *slot = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x40, "script_a");
    if (!slot || slot->size < SCRIPT_LIMIT + 1) return NULL;
    char *source = malloc(SCRIPT_LIMIT + 1);
    if (!source) return NULL;
    if (esp_partition_read(slot, 0, source, SCRIPT_LIMIT + 1) != ESP_OK ||
        !memchr(source, '\0', SCRIPT_LIMIT + 1) || !*source) {
        free(source);
        return NULL;
    }
    printf("RUNNER loaded %u bytes from script_a\n", (unsigned)strlen(source));
    return source;
}

static bool quota_self_test(void) {
    char *parse = malloc(7208);
    if (!parse) return false;
    for (unsigned i = 0; i < 1800; ++i) memcpy(parse + 4 * i, "x=1\n", 4);
    memcpy(parse + 7200, "EXIT 0\n", 8);
    const char *copies = "SAY LENGTH(COPIES(\"0123456789\",30000))\nEXIT 0\n";
    RunnerStats stats;
    size_t baseline = 0;
    bool ok = true;
    for (unsigned i = 0; i < 9; ++i) {
        if (runner_execute(parse, 0, &stats) != -1 ||
            stats.runtime_outcome != AETHER_MEMORY_CAP || stats.retained_bytes || stats.steps) ok = false;
        if (runner_execute(copies, 0, &stats) != -1 ||
            stats.runtime_outcome != AETHER_MEMORY_CAP || stats.retained_bytes) ok = false;
        if (runner_execute("EXIT 0\n", 0, &stats) != 0) ok = false;
        size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (i == 0) baseline = free_bytes;
        printf("RUNNER quota cycle %u: free=%zu baseline=%zu\n", i, free_bytes, baseline);
        if (free_bytes != baseline) ok = false;
    }
    free(parse);
    return ok;
}

/* Time one export so the step test has already parsed its script. */
static bool deadline_case(const char *source, bool parsing) {
    RexxAE ctx = NULL;
    if (!parsing) {
        ctx = aether_rexxae_create(source);
        if (aether_last_outcome() != AETHER_OK || !ctx) {
            if (ctx) aether_rexxae_destroy(ctx);
            return false;
        }
    }
    AetherExecLimits limits = {
        .memory_bytes = RUNNER_MEMORY_LIMIT,
        .deadline_ms = 1,
        .deadline_counts = AETHER_DEADLINE_ACTIVE,
    };
    AetherExecAccount *account = aether_exec_account_new(&limits);
    bool armed = account && aether_exec_use(account, NULL) == 0;
    int outcome = AETHER_OK;
    if (armed) {
        if (parsing) ctx = aether_rexxae_create(source);
        else aether_rexxae_step(ctx, STEP_BUDGET);
        outcome = aether_last_outcome();
        aether_exec_use(NULL, NULL);
    }
    if (ctx) aether_rexxae_destroy(ctx);
    uint64_t retained = account ? aether_exec_account_used(account) : 0;
    if (account) aether_exec_account_free(account);
    printf("RUNNER deadline %s: outcome=%d retained=%llu\n",
           parsing ? "parse" : "builtin", outcome, (unsigned long long)retained);
    return armed && outcome == AETHER_DEADLINE && retained == 0;
}

static bool deadline_self_test(void) {
    char *source = malloc(SCRIPT_LIMIT + 1);
    if (!source) return false;
    static const char ending[] = "*/\nEXIT 0\n";
    size_t end = SCRIPT_LIMIT + 1 - sizeof ending;
    memcpy(source, "/*", 2);
    memset(source + 2, 'x', end - 2);
    memcpy(source + end, ending, sizeof ending);
    bool ok = true;
    size_t baseline = 0;
    for (unsigned i = 0; i < 5; ++i) {
        ok &= deadline_case(source, true);
        ok &= deadline_case("SAY LENGTH(COPIES('x',50000))\nEXIT 0\n", false);
        RunnerStats stats;
        ok &= runner_execute("EXIT 0\n", 0, &stats) == 0;
        size_t available = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (i == 0) baseline = available;
        ok &= available == baseline;
        printf("RUNNER deadline cycle %u: free=%zu baseline=%zu\n", i, available, baseline);
    }
    free(source);
    return ok;
}

extern const char toit_led_script[] asm("_binary_led_rexx_start");
extern const char toit_bubble_sort_script[] asm("_binary_bubble_sort_rexx_start");
extern const char toit_mandelbrot_script[] asm("_binary_mandelbrot_rexx_start");
extern const char toit_hello_script[] asm("_binary_hello_rexx_start");
extern const char toit_service_log_script[] asm("_binary_service_log_rexx_start");

extern const char auto_off_script[] asm("_binary_auto_off_rexx_start");
extern const char timer_probe_script[] asm("_binary_timer_probe_rexx_start");

static bool led_self_test(void) {
#ifdef RUNNER_LED_GPIO
    if (!led_write(0) || gpio_set_direction(RUNNER_LED_GPIO, GPIO_MODE_OUTPUT) != ESP_OK) return false;
#endif
    RunnerStats stats;
    unsigned trace = 0;
    if (execute("CALL LED_WRITE 1\nEXIT 0\n", 0, &stats, &trace, DEMO_NONE) != -1 || trace) return false;
    if (execute("CALL LED_WRITE 2\nEXIT 0\n", RUNNER_GRANT_LED, &stats, &trace, DEMO_NONE) != -1 || trace) return false;
    return execute(toit_led_script, RUNNER_GRANT_LED | RUNNER_GRANT_WAIT, &stats, &trace, DEMO_NONE) == 0 &&
           trace == ((2u << 2) | 1u) && stats.waits == 2;
}

static bool auto_off_self_test(void) {
    RunnerStats stats;
    unsigned trace = 0;
    if (execute(auto_off_script, RUNNER_GRANT_LED | RUNNER_GRANT_TIMERS, &stats, &trace, DEMO_NONE) != 0 ||
        trace != ((2u << 2) | 1u) || stats.waits < 2) return false;

    /* A script that errors while the output is on must leave it off. */
    trace = 0;
    const char *bad = "CALL LED_WRITE 1\nCALL WAIT_MS -1\nEXIT 0\n";
    return execute(bad, RUNNER_GRANT_LED | RUNNER_GRANT_WAIT, &stats, &trace, DEMO_NONE) == -1 &&
           trace == ((2u << 2) | 1u);
}

extern const char toit_i2c_scan_script[] asm("_binary_i2c_scan_rexx_start");
extern const char toit_uart_loopback_script[] asm("_binary_uart_loopback_rexx_start");
extern const char toit_random_die_script[] asm("_binary_random_die_rexx_start");

extern const char toit_pulse_counter_script[] asm("_binary_pulse_counter_rexx_start");
extern const char toit_deep_sleep_script[] asm("_binary_deep_sleep_rexx_start");
extern const char toit_gpio_wake_script[] asm("_binary_gpio_wake_rexx_start");

extern const char toit_wifi_scan_script[] asm("_binary_wifi_scan_rexx_start");

static bool peripheral_self_test(void) {
    static const struct {
        DemoPeripheral peripheral;
        const char *script;
        const char *invalid_call;
        unsigned grants;
    } examples[] = {
        {DEMO_I2C, toit_i2c_scan_script, "CALL I2C_PROBE 0", 0},
        {DEMO_UART, toit_uart_loopback_script, "CALL UART_ECHO 1", RUNNER_GRANT_WAIT},
        {DEMO_RANDOM, toit_random_die_script, "CALL RANDOM_BELOW 0", 0},
        {DEMO_PULSE, toit_pulse_counter_script, "CALL PULSE_COUNT 1", RUNNER_GRANT_WAIT},
        {DEMO_TIMER_WAKE, toit_deep_sleep_script, "CALL WAKE_CAUSE 1", RUNNER_GRANT_UPTIME},
        {DEMO_GPIO_WAKE, toit_gpio_wake_script, "CALL WAKE_CAUSE 1", RUNNER_GRANT_WAIT},
        {DEMO_WIFI_SCAN, toit_wifi_scan_script, "CALL WIFI_SCAN_ROW 8", 0},
    };
    RunnerStats stats;
    bool ok = true;
    for (unsigned i = 0; i < sizeof examples / sizeof examples[0]; ++i) {
        DemoPeripheral peripheral = examples[i].peripheral;
        bool denied = runner_execute(examples[i].invalid_call, 0, &stats) == -1;
        bool opened = demo_open(peripheral);
        bool pass = false;
        if (opened) {
            bool invalid = execute(examples[i].invalid_call, 0, &stats, NULL, peripheral) == -1;
            bool ran = execute(examples[i].script, examples[i].grants, &stats, NULL, peripheral) == 0;
            bool closed = demo_close(peripheral);
            pass = denied && invalid && ran && closed;
        }
        const char *label = demo_function(peripheral);
        if (peripheral == DEMO_TIMER_WAKE) label = "timer wake";
        if (peripheral == DEMO_GPIO_WAKE) label = "GPIO wake";
        printf("RUNNER Toit %s %s\n", label, pass ? "PASS" : "FAIL");
        ok &= pass;
    }
    return ok;
}

void app_main(void) {
#ifdef RUNNER_CYD_CONSOLE
    bool display_ready = cyd_console_init();
    printf("RUNNER CYD console %s\n", display_ready ? "ready" : "unavailable");
    cyd_console_line("RexxAE on ESP32");
    printf("RUNNER CYD touch %s\n", cyd_touch_init() ? "ready" : "unavailable");
#endif
    RunnerStats stats;
#ifdef RUNNER_TOIT_WAKE
    DemoPeripheral wake = RUNNER_TOIT_WAKE == 1 ? DEMO_TIMER_WAKE : DEMO_GPIO_WAKE;
    const char *script = wake == DEMO_TIMER_WAKE ? toit_deep_sleep_script : toit_gpio_wake_script;
    if (execute(script, RUNNER_GRANT_TIMERS, &stats, NULL, wake) != 0 || !demo_enter_sleep(wake)) {
        printf("RUNNER could not enter deep sleep\n");
    }
    return;
#endif
    char *source = load_script();
#ifdef RUNNER_BOARD_SMOKE
    bool smoke_ok = false;
    if (source) {
        smoke_ok = runner_execute(source, RUNNER_GRANT_TIMERS, &stats) == 0 &&
                   stats.waits == 1 && stats.logs == 1;
        free(source);
    }
    printf("RUNNER board flash/timer/log %s\n", smoke_ok ? "PASS" : "FAIL");
    bool probe_ok = runner_execute(timer_probe_script, RUNNER_GRANT_TIMERS, &stats) == 0 && stats.waits == 5;
    printf("RUNNER board timer probe %s\n", probe_ok ? "PASS" : "FAIL");
#ifdef RUNNER_CYD_CONSOLE
    cyd_console_line(smoke_ok && probe_ok ? "RUNNER LANE OK" : "RUNNER FAILED");
#endif
    printf(smoke_ok && probe_ok ? "RUNNER LANE OK\n" : "RUNNER LANE FAILED\n");
#ifdef RUNNER_CYD_CONSOLE
    cyd_serial_run();
#endif
    return;
#endif
    bool quota_ok = quota_self_test();
    printf("RUNNER quota recovery %s\n", quota_ok ? "PASS" : "FAIL");
    bool deadline_ok = deadline_self_test();
    printf("RUNNER deadline recovery %s\n", deadline_ok ? "PASS" : "FAIL");
    bool ok = source != NULL && quota_ok && deadline_ok;
    if (source) {
        bool flash_ok = runner_execute(source, RUNNER_GRANT_TIMERS, &stats) == 0 && stats.waits == 1 && stats.logs == 1;
        ok &= flash_ok;
        free(source);
        printf("RUNNER flash/timer/log %s\n", flash_ok ? "PASS" : "FAIL");
    }
    /* Exercise observable host boundaries, including a timer suspended inside
     * an expression. No services carry across independently owned contexts. */
    bool pass = runner_execute("x = WAIT_MS(1) + WAIT_MS(1)\nEXIT x\n", RUNNER_GRANT_TIMERS, &stats) == 0 && stats.waits == 2;
    printf("RUNNER expression suspension %s\n", pass ? "PASS" : "FAIL"); ok &= pass;
    pass = runner_execute("CALL WAIT_MS 1\nEXIT 0\n", 0, &stats) == -1 && stats.waits == 0;
    printf("RUNNER unmounted function %s\n", pass ? "PASS" : "FAIL"); ok &= pass;
    pass = runner_execute("ADDRESS GPIO 'write 2 1'\nEXIT 0\n", RUNNER_GRANT_TIMERS, &stats) == -1;
    printf("RUNNER unmounted ADDRESS %s\n", pass ? "PASS" : "FAIL"); ok &= pass;
    pass = runner_execute("CALL WAIT_MS -1\nEXIT 0\n", RUNNER_GRANT_TIMERS, &stats) == -1 && stats.waits == 0;
    printf("RUNNER invalid timer %s\n", pass ? "PASS" : "FAIL"); ok &= pass;
    pass = runner_execute("DO FOREVER\nLET n = 1\nEND\n", RUNNER_GRANT_TIMERS, &stats) == -1 && stats.cancelled && stats.yields > 0;
    printf("RUNNER runaway cancellation %s (%u steps)\n", pass ? "PASS" : "FAIL", stats.steps); ok &= pass;
    pass = led_self_test();
    printf("RUNNER Toit LED port and capability checks %s\n", pass ? "PASS" : "FAIL"); ok &= pass;
    pass = pass && auto_off_self_test();
    printf("RUNNER auto-off and output cleanup %s\n", pass ? "PASS" : "FAIL"); ok &= pass;
    pass = runner_execute(timer_probe_script, RUNNER_GRANT_TIMERS, &stats) == 0 && stats.waits == 5;
    printf("RUNNER timer probe %s\n", pass ? "PASS" : "FAIL"); ok &= pass;
    pass = runner_execute(toit_bubble_sort_script, 0, &stats) == 0;
    printf("RUNNER Toit bubble sort %s\n", pass ? "PASS" : "FAIL"); ok &= pass;
    pass = runner_execute(toit_mandelbrot_script, 0, &stats) == 0;
    printf("RUNNER Toit Mandelbrot %s\n", pass ? "PASS" : "FAIL"); ok &= pass;
    pass = runner_execute(toit_hello_script, 0, &stats) == 0;
    printf("RUNNER Toit hello %s\n", pass ? "PASS" : "FAIL"); ok &= pass;
    pass = runner_execute(toit_service_log_script, RUNNER_GRANT_UPTIME, &stats) == 0 && stats.logs == 2;
    printf("RUNNER Toit logging %s\n", pass ? "PASS" : "FAIL"); ok &= pass;
    ok &= peripheral_self_test();
    printf(ok ? "RUNNER LANE OK\n" : "RUNNER LANE FAILED\n");
}

#endif /* RUNNER_HTTP */
