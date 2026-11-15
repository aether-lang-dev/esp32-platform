#ifndef CYD_CONSOLE_H
#define CYD_CONSOLE_H
#include <stdbool.h>

bool cyd_console_init(void);
void cyd_console_line(const char *text);
void cyd_serial_run(void);

#ifdef RUNNER_USB
#include "cyd_touch.h"
#include <stdint.h>

enum {
    CYD_COLS = 15, CYD_ROWS = 20, CYD_BUTTON_TEXT = 13,
    CYD_BUTTON_LIMIT = 6, CYD_BUTTON_ID_MAX = 255,
    CYD_BUTTON_ROW_MIN = 3, CYD_BUTTON_ROW_END = 17,
};
typedef struct {
    unsigned id, col, row, width, height;
    char label[CYD_BUTTON_TEXT + 1];
} CydButton;

typedef struct {
    char title[CYD_BUTTON_TEXT + 1], status[CYD_BUTTON_TEXT + 1];
    CydButton buttons[CYD_BUTTON_LIMIT];
    unsigned count;
    bool legacy;
} CydLayout;

bool cyd_layout_add(CydLayout *layout, const CydButton *button);
unsigned cyd_layout_hit(const CydLayout *layout, const CydTouch *tap);
bool cyd_console_layout(const CydLayout *layout, uint32_t version);
unsigned cyd_console_button_at(const CydTouch *tap, uint32_t version);
#endif

#endif
