/*
 * Ownership scenario adapted from Toit's tests/hw/esp32/gpio-num-reservation-test.toit.
 * Copyright (C) 2026 Toit contributors.
 *
 * Zero-Clause BSD License
 * Copyright (C) 2022 Toit contributors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
#include "runner_switch.h"
#include <assert.h>
#include <stdio.h>

static bool level;
static bool healthy = true;
static unsigned writes;

static bool drive(bool on) {
    ++writes;
    if (!healthy) return false;
    level = on;
    return true;
}

int main(void) {
    int first = 0, second = 0;
    RunnerSwitchOutput output = {.drive = drive};
    assert(runner_output_claim(&output, &first));
    assert(!level && writes == 1);
    assert(runner_output_write(&output, &first, true));
    assert(level && writes == 2);
    assert(!runner_output_claim(&output, &second));
    assert(!runner_output_write(&output, &second, false));
    assert(!runner_output_release(&output, &second));
    assert(level && writes == 2);

    healthy = false;
    assert(!runner_output_release(&output, &first));
    assert(!runner_output_claim(&output, &second));
    healthy = true;
    assert(runner_output_release(&output, &first));
    assert(!level);
    assert(runner_output_claim(&output, &second));
    assert(!runner_output_write(&output, &first, true));
    assert(runner_output_write(&output, &second, true));
    assert(runner_output_release(&output, &second));
    assert(!level);
    puts("SWITCH output exclusive ownership, failed release, and safe reclaim PASS");
}
