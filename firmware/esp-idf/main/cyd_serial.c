/* USB serial input for the FNK0114B display console. */
#include "cyd_console.h"
#include "cyd_touch.h"
#include "runner_host.h"
#include "driver/uart.h"
#include <stdio.h>

enum { LINE_LIMIT = 160, RX_BUFFER_SIZE = 512 };

static void serial_prompt(void) {
    cyd_console_line("rexx>");
    fputs("rexx> ", stdout);
    fflush(stdout);
}

void cyd_serial_run(void) {
    if (uart_driver_install(UART_NUM_0, RX_BUFFER_SIZE, 0, 0, NULL, 0) != ESP_OK) {
        puts("RUNNER CYD serial input unavailable");
        cyd_console_line("Serial RX error");
        return;
    }
    char line[LINE_LIMIT + 1];
    unsigned length = 0;
    bool discard = false;
    bool skip_lf = false;
    serial_prompt();
    for (;;) {
        unsigned char ch;
        TickType_t wait = cyd_touch_ready() ? pdMS_TO_TICKS(20) : portMAX_DELAY;
        if (uart_read_bytes(UART_NUM_0, &ch, 1, wait) != 1) {
            CydTouch tap;
            if (!length && !discard && cyd_touch_take(&tap)) {
                char message[64];
                snprintf(message, sizeof message, "Touch raw x=%u y=%u pressure=%u", tap.x, tap.y, tap.pressure);
                printf("\n%s\n", message);
                cyd_console_line(message);
                serial_prompt();
            }
            continue;
        }
        if (ch == '\n' && skip_lf) {
            skip_lf = false;
            continue;
        }
        skip_lf = ch == '\r';
        if (ch == '\r' || ch == '\n') {
            puts("");
            if (discard) {
                puts("Input too long; command discarded");
                cyd_console_line("Input too long");
            } else if (length) {
                line[length] = 0;
                cyd_console_line(line);
                RunnerStats stats;
                int result = runner_execute(line, RUNNER_GRANT_TIMERS, &stats);
                if (result) {
                    char message[32];
                    snprintf(message, sizeof message, "RC %d", result);
                    puts(message);
                    cyd_console_line(message);
                }
            }
            length = 0;
            discard = false;
            serial_prompt();
        } else if (ch == 3 || ch == 21) {
            puts("^C");
            length = 0;
            discard = false;
            serial_prompt();
        } else if (!discard && (ch == 8 || ch == 127)) {
            if (length) {
                --length;
                fputs("\b \b", stdout);
                fflush(stdout);
            }
        } else if (!discard && ch >= ' ' && ch <= '~') {
            if (length == LINE_LIMIT) {
                discard = true;
                continue;
            }
            line[length++] = (char)ch;
            putchar(ch);
            fflush(stdout);
        }
    }
}
