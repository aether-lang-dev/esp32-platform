# TODO

## Toit Mandelbrot runs under its own larger budget

`./lane.sh runner` runs `scripts/toit/mandelbrot.rexx` through
`runner_execute_budget` with a 28 s deadline (`MANDELBROT_DEADLINE_US` in
`firmware/esp-idf/main/runner_host.c`). Every other script, and production
use, keeps the 5 s default. The larger budget is not a performance claim.

Why: RexxAE is too slow for this script on ESP32. It costs about 650k Xtensa
instructions per simple statement, so Mandelbrot is about 5.84 s of work at a
250 MIPS model, against the 5 s default. The shortest-round-trip number
printing added in RexxAE 0ff6219 made it slower: before it the lane printed 2
of 5 rows in 5 s, after it 1.

Measured 2026-11-23 on `cachyos` in QEMU, with the host heavily loaded
(load average 14 to 19 on 4 cores, so wall time is inflated): 47.8 s on
esp32, 47.3 s on esp32s3, 46.4 s on esp32s3-no-psram, 55 steps each. The
120 s budget is about 2.5 times the slowest of those. The runner lane's
QEMU timeout default is now 240 s for the same reason.

Re-measured 2026-11-24 on Aether 0.941.0, RexxAE ad5c7bf, `cachyos` nearly idle
(load 0.5), in QEMU: 17.34 s on esp32, 17.81 s on esp32s3, 18.39 s on esp32c3,
55 steps each. The deadline is the slowest (18.39 s) plus 50% headroom, rounded
up: 28 s. It replaces the 120 s set against the loaded-host numbers above.

The same load made "Toit bubble sort" miss its 5 s default budget on every
target. Quiet-host timings for both scripts are still to be taken.

Dig deeper:

- Profile where RexxAE spends its time per statement on Xtensa: number
  formatting, variable lookup, and the per-step cost of the step API.
- Compare with the host build.
- Either speed RexxAE up, or explain why it cannot be.
- Then put Mandelbrot back on the default budget and delete
  `runner_execute_budget` if nothing else needs it.
