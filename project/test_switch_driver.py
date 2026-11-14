"""Compile the real ESP switch adapter against a deterministic timer shim."""
from pathlib import Path
import subprocess
import tempfile

main = Path(__file__).resolve().parent / "main"
with tempfile.TemporaryDirectory() as scratch:
    root = Path(scratch)
    (root / "freertos").mkdir()
    (root / "freertos/FreeRTOS.h").write_text('''
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void test_enter(void);
void test_exit(void);
#define portENTER_CRITICAL(p) ((void)(p), test_enter())
#define portEXIT_CRITICAL(p) ((void)(p), test_exit())
''')
    (root / "esp_timer.h").write_text('''
#include <stdint.h>
#define ESP_OK 0
typedef void *esp_timer_handle_t;
typedef struct { void (*callback)(void *); const char *name; } esp_timer_create_args_t;
int64_t esp_timer_get_time(void);
int esp_timer_create(const esp_timer_create_args_t *, esp_timer_handle_t *);
int esp_timer_start_periodic(esp_timer_handle_t, uint64_t);
int esp_timer_delete(esp_timer_handle_t);
''')
    (root / "test.c").write_text('''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
static int depth;
static int64_t now;
static bool fire_on_unlock;
static void (*tick)(void *);
#include "runner_switch_esp.c"
#include "runner_outlet_esp.c"
void test_enter(void) { assert(depth++ == 0); }
void test_exit(void) {
    assert(--depth == 0);
    if (fire_on_unlock) {
        fire_on_unlock = false;
        now = 1000001;
        tick(NULL);
    }
}
int64_t esp_timer_get_time(void) { return now; }
int esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out) {
    tick = args->callback; *out = &depth; return ESP_OK;
}
int esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t interval) {
    (void)timer; assert(interval == 50000); return ESP_OK;
}
int esp_timer_delete(esp_timer_handle_t timer) { (void)timer; return ESP_OK; }
int main(void) {
    int first = 0, second = 0;
    char initial_reply[RUNNER_OUTLET_TEXT + 1];
    assert(runner_outlet_command("STATUS", RUNNER_GRANT_OUTLET_READ, initial_reply) == 20);
    assert(runner_switch_init());
    assert(!runner_switch_on());
    assert(runner_switch_acquire(&first));
    assert(!runner_switch_acquire(&second));
    assert(runner_switch_session_finish(&first, 1, true));
    assert(runner_switch_on());
    runner_switch_session_close(&second);
    assert(runner_switch_on());
    now = 500000;
    /* Expiry lands at the ownership/write boundary of the old adapter. */
    fire_on_unlock = true;
    assert(runner_switch_session_finish(&first, 60, true));
    assert(runner_switch_on());
    runner_switch_session_close(&first);
    assert(!runner_switch_on());
    assert(runner_switch_acquire(&second));
    assert(runner_switch_session_finish(&second, 1, true));
    now += 1000001; tick(NULL);
    assert(!runner_switch_on());
    assert(runner_switch_acquire(&first));
    assert(runner_switch_session_finish(&first, 60, true));
    assert(runner_switch_session_finish(&first, RUNNER_SWITCH_KEEP, false));
    assert(!runner_switch_on());
    assert(runner_switch_acquire(&second));
    runner_switch_session_close(&second);
    char reply[RUNNER_OUTLET_TEXT + 1];
    unsigned all = RUNNER_GRANT_OUTLET_READ | RUNNER_GRANT_OUTLET_CONTROL | RUNNER_GRANT_OUTLET_CANCEL;
    assert(runner_outlet_command("START 2", all, reply) == 0);
    assert(!runner_switch_acquire(&first));
    runner_switch_session_close(&first);
    assert(runner_switch_on());
    assert(runner_outlet_command("STATUS", all, reply) == 0);
    runner_outlet_button();
    assert(!runner_switch_on());
    runner_outlet_button();
    assert(runner_switch_on());
    now += 10000001; tick(NULL);
    assert(!runner_switch_on());
    assert(runner_switch_acquire(&first));
    assert(runner_switch_session_finish(&first, 60, true));
    assert(runner_outlet_command("START 2", all, reply) == 8);
    assert(runner_outlet_command("CANCEL", all, reply) == 8);
    assert(runner_switch_on());
    runner_switch_session_close(&first);
    assert(runner_outlet_command("CANCEL", all, reply) == 0);
    puts("OUTLET real adapter button start/cancel, timeout and competing owner PASS");
    puts("SWITCH real adapter atomic expiry, ownership, close and failure PASS");
}
''')
    binary = root / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                    "-I", str(main), str(root / "test.c"), str(main / "runner_switch.c"), str(main / "runner_outlet.c"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
