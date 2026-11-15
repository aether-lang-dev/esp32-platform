/* Volatile PSK-authorized sessions, executed by the existing runner task. */
#include "runner_repl.h"
#include "runner_switch.h"
#include "runner_host.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { EVENT_COUNT = 16, EVENT_TEXT = 256, RESPONSE_SIZE = 10000 };
typedef struct {
    unsigned sequence;
    char kind[8], text[EVENT_TEXT];
    bool end;
} Event;
static SemaphoreHandle_t lock;
static RunnerSession engine;
static char session[33];
static char *pending;
static unsigned grants, command, emitted;
static int exit_code, outcome;
static const char *phase = "idle";
static bool closing, was_cancelled;
static atomic_bool cancelled;
static int64_t touched;
static Event events[EVENT_COUNT];

static esp_err_t reply(httpd_req_t *req, const char *status, const char *body) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t result = httpd_resp_sendstr(req, body);
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
    return result;
}

static bool number(const char *text, unsigned *result) {
    if (!text[0] || strspn(text, "0123456789") != strlen(text)) return false;
    errno = 0;
    char *end;
    unsigned long n = strtoul(text, &end, 10);
    if (errno || *end || n > UINT32_MAX) return false;
    *result = (unsigned)n;
    return true;
}

static bool authorized(httpd_req_t *req) {
    char id[33];
    if (httpd_req_get_hdr_value_str(req, "X-Repl-Session", id, sizeof id) != ESP_OK ||
        !session[0] || strcmp(id, session)) {
        reply(req, "409 Conflict", "{\"error\":\"session expired or replaced\"}");
        return false;
    }
    touched = esp_timer_get_time();
    return true;
}

static char *body(httpd_req_t *req) {
    if (!req->content_len || req->content_len > SCRIPT_LIMIT) return NULL;
    char *text = malloc(req->content_len + 1);
    if (!text) return NULL;
    size_t n = 0;
    while (n < req->content_len) {
        int got = httpd_req_recv(req, text + n, req->content_len - n);
        if (got <= 0) { free(text); return NULL; }
        n += got;
    }
    if (memchr(text, 0, n)) { free(text); return NULL; }
    text[n] = 0;
    return text;
}

static void output(void *argument, const char *kind, const char *text) {
    (void)argument;
    do {
        size_t length = strnlen(text, EVENT_TEXT - 1);
        while (length && ((unsigned char)text[length] & 0xc0) == 0x80) --length;
        if (!length && *text) length = 1;
        xSemaphoreTake(lock, portMAX_DELAY);
        Event *event = &events[emitted % EVENT_COUNT];
        event->sequence = ++emitted;
        snprintf(event->kind, sizeof event->kind, "%s", kind);
        memcpy(event->text, text, length);
        event->text[length] = 0;
        event->end = text[length] == 0;
        xSemaphoreGive(lock);
        text += length;
    } while (*text);
}

static bool is_cancelled(void *argument) {
    (void)argument;
    return atomic_load(&cancelled);
}

static esp_err_t open_session(httpd_req_t *req) {
    char *text = body(req);
    unsigned requested;
    if (!text || !number(text, &requested) || requested & ~RUNNER_REPL_GRANTS) {
        free(text);
        return reply(req, "400 Bad Request", "{\"error\":\"invalid session grants\"}");
    }
    free(text);
    xSemaphoreTake(lock, portMAX_DELAY);
    if (session[0]) {
        xSemaphoreGive(lock);
        return reply(req, "409 Conflict", "{\"error\":\"a session is already open\"}");
    }
    uint8_t random[16];
    esp_fill_random(random, sizeof random);
    for (unsigned i = 0; i < sizeof random; ++i) sprintf(session + i * 2, "%02x", random[i]);
    grants = requested;
    command = emitted = 0;
    exit_code = outcome = 0;
    closing = was_cancelled = false;
    phase = "idle";
    touched = esp_timer_get_time();
    char result[80];
    snprintf(result, sizeof result, "{\"session\":\"%s\",\"sequence\":0}", session);
    xSemaphoreGive(lock);
    return reply(req, "201 Created", result);
}

static esp_err_t submit(httpd_req_t *req) {
    char sequence[16];
    unsigned next;
    if (httpd_req_get_hdr_value_str(req, "X-Repl-Sequence", sequence, sizeof sequence) != ESP_OK ||
        !number(sequence, &next)) return reply(req, "400 Bad Request", "{\"error\":\"missing command sequence\"}");
    char *text = body(req);
    if (!text) return reply(req, "400 Bad Request", "{\"error\":\"empty, oversized or incomplete command\"}");
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!authorized(req)) {
        xSemaphoreGive(lock);
        free(text);
        return ESP_OK;
    }
    if (closing || pending || !strcmp(phase, "running") || command == UINT32_MAX || next != command + 1) {
        xSemaphoreGive(lock);
        free(text);
        return reply(req, "409 Conflict", "{\"error\":\"busy or repeated command sequence\"}");
    }
    pending = text;
    command = next;
    emitted = 0;
    phase = "pending";
    atomic_store(&cancelled, false);
    char result[48];
    snprintf(result, sizeof result, "{\"sequence\":%u}", command);
    xSemaphoreGive(lock);
    return reply(req, "202 Accepted", result);
}

static esp_err_t cancel_or_close(httpd_req_t *req) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!authorized(req)) { xSemaphoreGive(lock); return ESP_OK; }
    atomic_store(&cancelled, true);
    if (!strcmp(req->uri, "/repl/close")) closing = true;
    xSemaphoreGive(lock);
    return reply(req, "202 Accepted", "{\"accepted\":true}");
}

static esp_err_t read_events(httpd_req_t *req) {
    char cursor[16];
    unsigned after;
    if (httpd_req_get_hdr_value_str(req, "X-Repl-After", cursor, sizeof cursor) != ESP_OK ||
        !number(cursor, &after)) return reply(req, "400 Bad Request", "{\"error\":\"missing event cursor\"}");
    char *result = malloc(RESPONSE_SIZE);
    if (!result) return reply(req, "503 Service Unavailable", "{\"error\":\"no memory\"}");
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!authorized(req)) { xSemaphoreGive(lock); free(result); return ESP_OK; }
    if (after > emitted) {
        xSemaphoreGive(lock);
        free(result);
        return reply(req, "400 Bad Request", "{\"error\":\"invalid event cursor\"}");
    }
    unsigned oldest = emitted > EVENT_COUNT ? emitted - EVENT_COUNT : 0;
    bool lost = after < oldest;
    if (lost) after = oldest;
    size_t used = (size_t)snprintf(result, RESPONSE_SIZE,
        "{\"sequence\":%u,\"state\":\"%s\",\"exitCode\":%d,\"runtimeOutcome\":%d,"
        "\"cancelled\":%s,\"lost\":%s,\"cursor\":%u,\"events\":[",
        command, phase, exit_code, outcome, was_cancelled ? "true" : "false",
        lost ? "true" : "false", emitted);
    bool first = true;
    for (unsigned i = after; i < emitted; ++i) {
        const Event *event = &events[i % EVENT_COUNT];
        used += (size_t)snprintf(result + used, RESPONSE_SIZE - used,
            "%s{\"kind\":\"%s\",\"end\":%s,\"hex\":\"", first ? "" : ",",
            event->kind, event->end ? "true" : "false");
        first = false;
        for (const unsigned char *p = (const unsigned char *)event->text; *p; ++p) {
            used += (size_t)snprintf(result + used, RESPONSE_SIZE - used, "%02x", *p);
        }
        used += (size_t)snprintf(result + used, RESPONSE_SIZE - used, "\"}");
    }
    snprintf(result + used, RESPONSE_SIZE - used, "]}");
    xSemaphoreGive(lock);
    esp_err_t sent = reply(req, "200 OK", result);
    free(result);
    return sent;
}

esp_err_t runner_repl_start(httpd_handle_t server, RunnerAppsCommand apps) {
    lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;
    engine = (RunnerSession){.apps = apps, .output = output, .cancelled = is_cancelled};
    const httpd_uri_t routes[] = {
        {.uri = "/repl/open", .method = HTTP_POST, .handler = open_session},
        {.uri = "/repl/command", .method = HTTP_POST, .handler = submit},
        {.uri = "/repl/events", .method = HTTP_GET, .handler = read_events},
        {.uri = "/repl/cancel", .method = HTTP_POST, .handler = cancel_or_close},
        {.uri = "/repl/close", .method = HTTP_POST, .handler = cancel_or_close},
    };
    for (unsigned i = 0; i < sizeof routes / sizeof routes[0]; ++i) {
        esp_err_t result = httpd_register_uri_handler(server, &routes[i]);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}

void runner_repl_poll(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (session[0] && (closing || esp_timer_get_time() - touched > 120000000)) {
        free(pending);
        pending = NULL;
        runner_session_close(&engine);
        session[0] = 0;
        phase = "idle";
    }
    char *source = pending;
    pending = NULL;
    if (source) phase = "running";
    xSemaphoreGive(lock);
    if (!source) {
        if (atomic_load(&cancelled)) runner_switch_session_close(&engine);
        return;
    }
    RunnerStats stats;
    int result = runner_session_execute(&engine, source, grants, &stats);
    free(source);
    xSemaphoreTake(lock, portMAX_DELAY);
    exit_code = result;
    outcome = stats.runtime_outcome;
    was_cancelled = stats.cancelled;
    phase = "done";
    xSemaphoreGive(lock);
}
