#include "cyd_console.h"
#include <assert.h>
#include <stdio.h>

static unsigned hit(const CydLayout *layout, unsigned x, unsigned y) {
    CydTouch tap = {x, y, 500};
    return cyd_layout_hit(layout, &tap);
}

int main(void) {
    CydLayout layout = {0};
    CydButton hello = {1, 1, 4, 6, 3, "HELLO"};
    CydButton about = {2, 4, 9, 7, 3, "ABOUT"};
    CydButton reset = {3, 9, 13, 5, 3, "RESET"};
    assert(cyd_layout_add(&layout, &hello));
    assert(cyd_layout_add(&layout, &about));
    assert(cyd_layout_add(&layout, &reset));
    assert(hit(&layout, 3000, 1100) == 1);
    assert(hit(&layout, 2000, 2100) == 2);
    assert(hit(&layout, 1000, 2800) == 3);
    assert(hit(&layout, 1000, 1100) == 0);
    assert(hit(&layout, 3000, 1550) == 0);
    assert(hit(&layout, 4095, 0) == 0);
    assert(hit(&layout, 2197, 1100) == 0);
    assert(hit(&layout, 2198, 1100) == 1);

    CydButton invalid = {1, 0, 8, 1, 1, "X"};
    assert(!cyd_layout_add(&layout, &invalid)); /* duplicate ID */
    invalid = (CydButton){4, 6, 6, 2, 2, "X"};
    assert(!cyd_layout_add(&layout, &invalid)); /* overlap */
    invalid = (CydButton){4, 14, 16, 2, 1, "X"};
    assert(!cyd_layout_add(&layout, &invalid)); /* right edge */
    invalid = (CydButton){4, 14, 16, 1, 2, "X"};
    assert(!cyd_layout_add(&layout, &invalid)); /* bottom edge */
    invalid = (CydButton){4, 14, 16, 1, 1, "XX"};
    assert(!cyd_layout_add(&layout, &invalid)); /* label does not fit */
    invalid = (CydButton){4, 14, 16, 1, 1, ""};
    assert(!cyd_layout_add(&layout, &invalid));
    assert(layout.count == 3);
    for (unsigned col = 0; col < 3; ++col) {
        CydButton extra = {4 + col, col, 8, 1, 1, "X"};
        assert(cyd_layout_add(&layout, &extra));
    }
    CydButton extra = {7, 3, 8, 1, 1, "X"};
    assert(!cyd_layout_add(&layout, &extra));

    layout = (CydLayout){0};
    hello.col = 8;
    assert(cyd_layout_add(&layout, &hello));
    assert(hit(&layout, 3000, 1100) == 0);
    assert(hit(&layout, 1000, 1100) == 1);
    puts("CYD layout limits, calibrated hit regions and moved button PASS");
}
