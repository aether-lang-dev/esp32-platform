#ifndef CYD_TOUCH_H
#define CYD_TOUCH_H
#include <stdbool.h>

typedef struct {
    unsigned x, y, pressure;
} CydTouch;

bool cyd_touch_init(void);
bool cyd_touch_ready(void);
bool cyd_touch_take(CydTouch *touch);

#endif
