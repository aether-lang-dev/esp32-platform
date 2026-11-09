# esp32: Aether and RexxAE on ESP32 and ESP32-S3

Experimental work towards a "runner" firmware: an ESP32-S3 that takes Rexx
scripts over the air and runs them with RexxAE, the embeddable Rexx engine
written in Aether (`../RexxAE`). The target board is a Guition JC3248W535C
(ESP32-S3-N16R8: 16 MB quad flash, 8 MB octal PSRAM, AXS15231B QSPI display;
the display is not used yet). Until the board arrives, everything runs in
Espressif's QEMU.

This first step proves the toolchain path and measures the engine:

1. Aether's `hello` builds and boots on the S3.
2. RexxAE's engine, compiled to C by `aetherc --emit=lib`, links into
   ESP-IDF firmware with a C host (`project/main/rexx_host.c`) on its step
   API, and runs two scripts: `scripts/demo.rexx` (SAY, a registered host
   FUNCTION round trip, a DO loop, a 4-instruction budget so the step API
   returns RUNNABLE) and `scripts/bench.rexx` (~100 lines: routines,
   SELECT, PARSE, string builtins, nested loops, host calls), timed.
3. The host prints heap, stack and timing figures, then runs 50 more
   create/run/destroy cycles with one script and 50 with a different source
   each time, to find what a long-running runner would accumulate.
4. `runner` loads a separate flash-resident script, yields between engine
   steps and suspends/resumes timer calls, with denial/cancellation checks.

It is modelled on aether's own `make ci-esp32` lane (`tests/esp32/`), which
targets the plain ESP32. This lane adds S3 configurations, C hosts, a flash script store and
measurements.

## Running it

```sh
./lane.sh              # hello + rexxae; or ./lane.sh rexxae
```

It needs podman (or docker), a host C compiler and `make`, and sibling
checkouts `../aether` and `../RexxAE`. The lane reads the exact Aether commit
in `AETHER_REF` (v0.891.0) from the sibling repository with `git archive`
(or fetches a missing commit into a temporary bare repository),
builds its compiler in a temporary directory, and uses that same snapshot's
runtime. It does not use or modify the sibling's built compiler or working
tree. The first housekeeping run found a 0.875.0 compiler beside 0.877.0
sources; rebuilding the pinned snapshot avoids that mismatch.

ESP-IDF 6.1, the Xtensa toolchain and Espressif's QEMU come from
`docker.io/espressif/idf:v6.1`; nothing ESP-specific is installed on the host.
Each program gets a fresh IDF build and configuration inside the container.
Project files, scripts and the container entry script are copied before
launch, so edits during a build do not change that run.

```sh
./lane.sh hello rexxae runner
ESP32_TARGET=esp32 ./lane.sh hello rexxae runner
ESP32_TARGET=esp32s3-no-psram ./lane.sh hello rexxae runner
```

`ESP32_TARGET` selects `project/sdkconfig.<target>`: S3 with 8 MB octal
PSRAM and 16 MB flash (default), S3 with PSRAM disabled and 16 MB flash,
or plain ESP32 with PSRAM disabled and 4 MB flash. The benchmark's heap
measurements are printed on every target.

Set `ESP32_KEEP=/path/to/results` to retain generated C, the staged project
and scripts, compiler/build/size/console logs, revision metadata and ELFs.
Without it, temporary results are deleted at exit. Use a separate results
directory per target. Options: `AETHER_DIR`, `REXXAE_DIR`, `AETHER_REF`
(an explicit development override), `ESP32_ENGINE`, `ESP32_IDF_IMAGE`,
`AETHER_STRICT=1` (reject a version mismatch on a development override),
`ESP32_BUILD_JOBS`, `QEMU_PSRAM` (PSRAM target only), `QEMU_TIMEOUT`, and
`AE_TRACE_ALLOC=1`. The allocation diagnostic logs allocations of 4 KB or
more; symbolize callers with `xtensa-esp32s3-elf-addr2line` (S3) or
`xtensa-esp32-elf-addr2line` (ESP32) against the saved ELF.

### Development runner

`./lane.sh runner` compiles the engine with `runner_host.c`. Its custom
partition table reserves 64 KB each for `script_a` and `script_b`. The lane
provisions `scripts/runner.rexx`, followed by a NUL terminator, at
`script_a`'s offset independently of the application binary. The runner
reads at most 8 KB of source through ESP-IDF's partition API; only slot A
is used in this milestone.

The host mounts `UPTIME_MS()` and `WAIT_MS(integer_ms)` (0..1000), routes
SAY and LOG events to serial, steps at most 32 instructions at a time and
sleeps one FreeRTOS tick between steps. Timer calls remain suspended until
their deadline, including calls inside expressions. A run is stopped after
2048 steps or five seconds, whichever is reached first. This is a
conservative step allowance. Aether checks the elapsed deadline at
cooperative polling points inside exports, including parser and builtin
loops. Native operations without a poll can run until they return.

The QEMU harness checks the flash-loaded script and its timer/log output,
expression suspension, unmounted-function denial, cancellation on unmounted
ADDRESS, invalid timer arguments and runaway cancellation. ADDRESS error
replies populate Rexx's RC/ERRORTEXT and allow execution to continue; the
runner explicitly cancels to enforce its deny policy.

This standalone development target has fixed host grants. The separate signed
profile below implements signature verification, timer grants and script-slot
trials/rollback. Firmware OTA, fleet delivery and display services remain open.
The layout leaves space for a second app partition, but does not configure
firmware OTA or firmware rollback yet.

### HTTP delivery over emulated Ethernet

```sh
ESP32_TARGET=esp32 ./lane.sh runner-http
```

This additional target starts an ESP-IDF HTTP server over QEMU's `open_eth`
Ethernet controller. The harness forwards a loopback port **inside the
container**; no host port is published. It sends actual HTTP requests and
stops QEMU after checking delivery. This is a QEMU-only development host,
with unsigned uploads and no authentication; it is not a production OTA path.

- `POST /script` accepts 1..8192 bytes of NUL-free source and returns
  `202 {"id":N}`. Empty/NUL-containing uploads return 400, oversized uploads
  413, and another upload while one is pending/running 409.
- The runner task writes the source to `script_b`, reads it back, then runs
  it through the same bounded loop and timer-only mounted functions. Slot A
  is preserved. This does not switch the boot-active script, validate a
  manifest or implement rollback.
- `GET /status` reports the upload id, `idle`/`pending`/`running`/`done`/`error`,
  the script's exit code and whether its run budget caused cancellation.
  `done` means exit code zero; nonzero exits and engine/storage errors are
  reported as `error`.

The integration test proves changed-source execution and nonzero exit codes,
rejects empty/NUL/oversized and busy uploads, denies an uploaded script's
attempt to call an unmounted HTTP service, polls status while a timer is
suspended, cancels a runaway, and successfully executes a subsequent upload.
The current unsigned HTTP build is 507,264 bytes on plain ESP32.

Wi-Fi/hardware transport, HTTPS/MQTT fleet delivery and a persistent development
server/watch client remain open. Signed manifests and script rollback are
implemented in the separate profile below.
Toit reuse policy and provenance are in [PROVENANCE.md](PROVENANCE.md); no
Toit code has been ported yet.

### Signed updates and persistent rollback

```sh
ESP32_TARGET=esp32 ./lane.sh runner-signed
python3 tools/test_rexx_bundle.py
```

This profile uses P-256/SHA-256 signed bundles and individually granted timer
functions. It verifies before writing flash and again after readback, trials
the inactive script slot, promotes only a zero-exit healthy run, and restores
the prior release after failure or an interrupted trial. NVS preserves the
application binding and highest version admitted to a trial across resets;
old/replayed releases and a different application name are refused.

The test generates disposable current/next signing keys off-device and
compiles only their public keys into firmware. It checks forged/tampered
bundles, grant denial, unchanged flash on rejection, disconnected uploads,
allocation/parse-failure classification, replay refusal, active-slot alternation,
failed-trial recovery, reset during a trial and healthy activation across a
second reset. Firmware is 585,152 bytes on plain ESP32. These are test roots,
not production provisioning, and the host allowance is not a complete memory
or CPU sandbox. The unsigned development target remains separate.

Each runner invocation gets a 128 KiB memory cap and a five-second elapsed
deadline in its Aether execution account. Time spent waiting for a timer
counts against the deadline. Every engine export's runtime outcome is checked
before interpreting its ABI return. `AETHER_MEMORY_CAP` and `AETHER_DEADLINE`
both reject the run.
Trusted teardown detaches the exhausted account before destroying the context
and checks that the account holds zero bytes afterward.

Runner targets require RexxAE commit `ef7a744` or a descendant for
interruption cleanup. The standalone QEMU lane repeatedly trips both limits
during parsing and execution, checks that internal free heap returns to its
warmed baseline, then runs a healthy script. Signed delivery also checks
rollback after memory and deadline failures. The standalone build is
316,848 bytes on ESP32, 351,040 bytes on S3 with PSRAM and 343,104 bytes on
S3 without PSRAM.

[SIGNING.md](SIGNING.md) specifies the wire format, trust model, persistence
policy, health predicate, limits and production work still open. Aether's
OpenSSL-free HTTP/TLS server remains a candidate for production transport;
judge its target cost by emitted code and runtime measurements, rather than
its imports. The current test transport uses ESP-IDF HTTP over OpenETH.

### Watch a script for signed development updates

With a signed runner listening on your development network, use a local P-256
PEM key whose public key the runner trusts:

```sh
tools/rexx watch script.rexx --device http://127.0.0.1:18080 \
  --private-key /path/to/dev-key.pem --key-id fleet-current \
  --name test-script --grant WAIT_MS
```

Use the device's HTTP origin in place of the QEMU address. The watcher submits
the initial file, then signs each stable change with a version one above the
device's persisted floor. Keep `--name` the same for that device. Repeat
`--grant` for each timer function the script needs. The private key is used
locally by OpenSSL. Only the signed bundle is uploaded.

The watcher reports activation, rollback, or an unknown result if it loses
confirmation. Each distinct edit is attempted once. Check the device's
`/status` after an unknown result before submitting again. Use `--once` to
submit the current contents explicitly and exit, with status zero on
confirmed activation. Plain HTTP is for the development network.

The signed QEMU lane runs the actual watcher, saves a script with a syntax
error, and verifies that a corrected save activates at the next version.

### Pre-shared key TLS smoke test

```sh
ESP32_TARGET=esp32 ./lane.sh psk-probe
```

This standalone OpenETH/QEMU profile uses ESP-TLS with a TLS 1.3 external
pre-shared key and ephemeral key exchange. The lane generates a disposable
32-byte device key and includes it in the same flash operation as the
firmware, in a separate `device_key` data partition at `0x330000`.
The key and merged flash stay inside the container. The smoke firmware is
534,224 bytes.

The host proves that a matching key reaches `GET /status` and that a wrong
key, an unknown device identity and a missing key fail before the HTTP
handler runs. A second authenticated request checks the handler count and
recovery. This profile exercises provisioning and transport for development.
It uses test-only plaintext flash provisioning and OpenSSL's CLI with
disposable keys.

### Signed watch over PSK TLS

The `runner-signed-psk` profile serves the same signed update API over
TLS 1.3 with a flash-provisioned device credential. The signed PSK image
is 721,616 bytes on plain ESP32 in QEMU. Only the native host
reads the device key. RexxAE keeps its timer grants and has no credential
or network access. The bundle signing key is a separate local private key.

```sh
python3 tools/rexx_psk.py device.json device.psk --generate my-device
tools/rexx watch script.rexx --device https://device-address \
  --device-credential device.json --private-key /path/to/signing.pem \
  --key-id fleet-current --name test-script --grant WAIT_MS
```

Provision `device.psk` in the `device_key` partition with the firmware.
Keep both credential files private. The QEMU lane generates its own
disposable copies. The watcher needs Python 3.13 or newer with
`ssl.HAS_PSK` enabled. For the QEMU signed suite, build the test image
with a PSK-capable Python and use it for the lane.

```sh
podman build -t localhost/esp32-idf-psk-test project/psk-test
ESP32_IDF_IMAGE=localhost/esp32-idf-psk-test ESP32_TARGET=esp32 \
  ./lane.sh runner-signed-psk
```

### Sync one script to a fleet

Save a fleet file such as `fleet.json` with each device's HTTPS origin and
its own PSK credential file. Relative credential paths are resolved from
the fleet file.

```json
{
  "alpha": {"device": "https://alpha.local", "credential": "alpha.json"},
  "beta": {"device": "https://beta.local", "credential": "beta.json"}
}
```

```sh
tools/rexx sync script.rexx --fleet fleet.json \
  --private-key /path/to/signing.pem --key-id fleet-current \
  --name test-script --grant WAIT_MS
```

Sync snapshots the source once. It skips devices whose active release
already matches the app name, signing key ID, grants and source. Other
devices get the next version above their own durable version floor.
Each device reports its result independently. Sync exits with status 1
if any device failed, rolled back, or could not be reached. Devices that
activated successfully keep their updates. Run sync again to catch up an
offline device or reconcile an unknown result.

The signed PSK QEMU lane runs two devices with independent keys and
version floors. It checks offline recovery, skipping a matching release
after reboot, grants changes, and rollback on both devices.

To try a script change on the Mac before flashing, the host also builds as
a desktop program against `../RexxAE/build/librexxae.dylib`:

```sh
cd scripts && xxd -i -n demo_rexx demo.rexx > /tmp/demo_rexx.h \
           && xxd -i -n bench_rexx bench.rexx > /tmp/bench_rexx.h
cc -std=c11 -I../../RexxAE/include -I/tmp ../project/main/rexx_host.c \
   -L../../RexxAE/build -lrexxae -Wl,-rpath,$PWD/../../RexxAE/build -o /tmp/host
```

## Results (2026-11-04)

Aether v0.878.0 (`c099a51d9`), RexxAE `184ea83`, ESP-IDF 6.1 and QEMU
9.2.2 (`esp_develop_9.2.2_20260417`). The table below is populated from
fresh QEMU runs; time measures emulation speed, not real chip speed.

| Measurement | S3 + PSRAM | S3, no PSRAM | Plain ESP32 |
|---|---:|---:|---:|
| hello firmware (.bin), B | 188,112 | 180,416 | 152,560 |
| RexxAE benchmark firmware (.bin), B | 339,408 | 331,600 | 305,152 |
| Runner firmware (.bin), B | 339,440 | 331,648 | 305,248 |
| Free internal heap before create, B | 364,815 | 369,692 | 276,344 |
| Demo live heap after create, B | 5,648 | 5,648 | 5,664 |
| Demo live heap after run, B | 7,672 | 7,672 | 7,660 |
| Bench additional live heap after run, B | 40,028 | 39,852 | 39,952 |
| Drop in internal heap minimum through bench, B | 87,743 | 55,652 | 55,564 |
| PSRAM consumed through bench, B | 0 | 0 | 0 |
| Retained after first two contexts, B | 3,076 | 3,076 | 3,076 |
| Further retained growth, same-source cycles 1–50, B | 0 | 0 | 0 |
| Further retained growth, distinct-source cycles 1–50, B | 0 | 0 | 0 |
| Least free main task stack (of 16,384 B) | 13,000 | 12,968 | 13,216 |
| Demo parse time, µs (QEMU) | 43,637 | 43,718 | 48,976 |
| Bench parse time, µs (QEMU) | 370,303 | 376,234 | 404,218 |
| Bench run time, µs (QEMU) | 3,087,136 | 2,984,122 | 3,390,951 |

Live bench heap is relative to the post-demo-destroy baseline. The internal
minimum is ESP-IDF’s sum of per-region low-water marks, so its drop is a
conservative high-water indication, not an exact simultaneous peak. Timing
varies with emulator/host load. All six runner checks pass on every target.


The two former main/branch columns are retired: the three changes below
are now part of the pinned release. Real parse/run timings and PSRAM cost
still require a board.

## What is and isn't emulated

- **Emulated:** the ESP32-S3 (Xtensa LX7) CPU, boot ROM and second-stage
  bootloader, the partition table, 16 MB SPI flash (QEMU accepts 2, 4, 8 or
  16 MB images), and 8 MB of octal PSRAM. The machine's `ssi_psram` device is
  quad unless `-global ssi_psram.is_octal=true` is set; without that, the
  octal-PSRAM `sdkconfig` the N16R8 needs aborts at boot ("PSRAM chip is not
  connected, or wrong PSRAM line mode"). With it the boot log identifies
  an AP Memory 64 Mbit generation-3 octal PSRAM, ESP-IDF adds it to the heap
  (`psram_free` 8,386,156 B), The engine no longer needs the old fixed 16 KB JSON arena blocks. QEMU's `-m` sets the PSRAM size, up to 32 MB.
- **Not emulated, or not exercised:** real timing (above); flash and PSRAM
  speed, cache misses and PSRAM's cost relative to internal RAM; the second
  core (the firmware uses one task); Wi-Fi/BLE (there is no OTA path yet);
  the display; power. The boot also prints `rtcinit: o_code calibration
  fail`, a harmless QEMU artefact.

## Upstream changes found and made

Made on aether's branch `esp32/rexxae` and merged as
aether-lang-dev/aether#3550, with a regression test each; in releases
from v0.876.0 (the old main/branch comparison has been replaced by the release measurements above):

1. **`ae78f610b` std.json: size an arena's first chunk to what it will
   hold.** Every built `json.obj`/`json.arr` roots an arena whose first
   chunk was the fixed 16 KB meant for parsed documents. RexxAE builds its
   program, values and frames from such containers, so a run made ~23,000
   16 KB allocations, every one past ESP-IDF's 16 KB
   `SPIRAM_MALLOC_ALWAYSINTERNAL` threshold and into PSRAM (found with
   `AE_TRACE_ALLOC=1`: one caller, `arena_new_chunk`, 16,396 bytes). Built
   containers now start at 256 B and parsed documents at ~4 bytes per input
   byte (256 B to 16 KB). JSONTestSuite, the std.json `.ae` tests and an
   ASan+UBSan desktop twin of this firmware pass.
2. **`50e14635d` coop_runtime_sources: add std.bytes, std.mem,
   std.strbuilder, std.sync.** The list the wasm and ESP32 builds link is
   meant to be closed under what a program reaches; without these RexxAE
   does not link (`aether_strbuilder_new`, `aether_bytes_data`,
   `aether_sync_atomic_new`, ... undefined). This lane supplied them
   in older revisions; the lane now uses the released list directly.
3. **`4d1f41969` resource caps: `-DAETHER_ACCT_FILTER_LOG2`.** The
   execution-account address filter is 32 KiB of `.bss` in every program,
   used or not: the largest static block in this firmware. The lane builds
   with `AETHER_ACCT_FILTER_LOG2=14` (2 KiB); the pinned runtime honours the define.

Each has a `new_changelogs/` fragment.

## Open problems

- **Timing on real hardware.** Nothing here says how fast the engine is on
  an S3. If 107 lines really take 0.27 s to parse on the chip, the parser
  (and std.json's deep copy on every `json.set`/`push`, which makes
  building a tree bottom-up copy each subtree again at every level) is
  the first thing to look at.
- **Peak vs live heap.** A 107-line script adds about 40 KB of live heap after running,
  with a larger drop in the reported heap minimum while parsing/running: transient copies, the same deep-copy
  pattern. Fine on an S3 with PSRAM, and it bounds how large a script a
  PSRAM-less device can take.
- **3 KB retained after the first contexts.** It does not grow afterwards
  (100 further cycles, same and distinct sources), so it looks like
  one-time lazy initialisation in the runtime or engine. Not identified.
- **Dialect surprises** (RexxJS behaviour, which RexxAE follows; both
  checked against `../RexxJS`): `%` is the remainder operator and `//`
  is not an operator (`i // 4` inside a function call is a parse error);
  `POS('fox', s)` returns 0 where classic Rexx gives 17; a template that
  starts with a literal pattern (`PARSE VAR line 'temp=' temp ...`) leaves
  the pattern in the first variable. Worth knowing before porting classic
  Rexx scripts to a runner.
- **Warnings.** As in aether's lane, the generated C and the runtime build
  with `-w` (they are not warning-clean under xtensa gcc). The host builds
  with `-Wall -Wextra -Werror`, which caught an `int32_t` (`long` on
  Xtensa) passed to `%d`.
- **aether Makefile targets** `test-json-conformance` and `test-json-asan`
  do not link on main (`string_empty` and friends undefined: they compile
  `aether_json.c` without the string runtime). Not fixed here.
- **Next steps** toward the runner: see [PLAN.md](PLAN.md).

### Toit examples

The standalone `runner` lane runs the [Rexx LED demo](scripts/toit/led.rexx).
It turns one logical LED on for two seconds, then turns it off. In QEMU the
runner simulates the LED, checks the on/off sequence and rejects ungranted
calls and invalid levels. To build the standalone image with a physical
active-high LED, set `RUNNER_LED_GPIO` to the output pin wired for your
board when running `./lane.sh runner`. Set `ESP32_KEEP` to retain the
merged image at `out/runner.flash.bin` under that directory. The script
takes no pin number.

The same lane runs [bubble sort](scripts/toit/bubble_sort.rexx) on a fixed
list with negative and repeated values, and renders a small
[Mandelbrot](scripts/toit/mandelbrot.rexx) grid on serial. Both scripts
check their results and run with zero host function grants.

[Hello](scripts/toit/hello.rexx) prints a greeting with zero host grants.
[Logging](scripts/toit/service_log.rexx) uses the uptime grant and emits two
timestamped log events, which the runner prints on serial.

The [I²C scanner](scripts/toit/i2c_scan.rexx),
[UART loopback](scripts/toit/uart_loopback.rexx), and
[random die](scripts/toit/random_die.rexx) run in the standalone lane too.
QEMU supplies two I²C fixture addresses and a UART echo fixture. The
random demo uses the ESP-IDF random API and checks the bounds of each draw.

To build the wired version, run
`ESP32_TARGET=esp32 RUNNER_TOIT_IO=1 ESP32_KEEP=/absolute/output/path ./lane.sh runner`.
The build saves the merged image at `out/runner.flash.bin` under that path.
The demo uses GPIO 21 for SDA and GPIO 22 for SCL, with bus pull-ups, and
UART1 at 115200 baud. Connect TX GPIO 26 to RX GPIO 25 for loopback.
Use a classic ESP32 board that exposes those pins.

The [HTTP body counter](scripts/toit/http.ae) runs on desktop Aether.
Set `TOIT_HTTP_URL` and run `ae run scripts/toit/http.ae`. To test against a
local server, build with `ae build scripts/toit/http.ae -o /tmp/toit-http`
and run `python3 project/test_toit_http.py /tmp/toit-http`.

The [pulse-counter port](scripts/toit/pulse_counter.rexx) reads five samples
at 500 ms intervals. In the wired build, connect output GPIO 5 to input
GPIO 18 through a 330 Ω resistor on a classic ESP32 development board.
LEDC generates the 25 Hz square wave and PCNT counts its rising edges.
QEMU uses an elapsed-time fixture to exercise the same script and grant.

[Timer wake](scripts/toit/deep_sleep.rexx) and
[GPIO wake](scripts/toit/gpio_wake.rexx) report the wake reason. QEMU injects
the reasons. Build a dedicated hardware image with
`ESP32_TARGET=esp32 RUNNER_TOIT_WAKE=timer ESP32_KEEP=/absolute/output/path ./lane.sh runner`
or set `RUNNER_TOIT_WAKE=gpio`. The timer image sleeps for 10 seconds per
cycle. The GPIO image wakes on a high level at GPIO 32 or after 30 seconds.
Use an external pull-down on GPIO 32 for the GPIO demo.

The [Wi-Fi scan](scripts/toit/wifi_scan.rexx) reports SSID, BSSID, RSSI,
channel and the ESP-IDF authentication-mode number. The wired image
passively scans channels 1–7 and reports up to eight access points.
QEMU uses fixture access points.

The [standalone ESP-IDF ports](examples/toit-idf/README.md) cover the Toit
BLE, Wi-Fi connect, Ethernet, ESP-NOW, NTP and touch-wake examples.
Run the desktop RSA port with `python3 scripts/toit/rsa/rsa_gen.py`.

### Original RexxAE examples

- [Timed shutoff](scripts/examples/auto_off.rexx) uses the standalone
  runner's simulated or configured LED as a load indicator. It turns the
  output off after a monotonic deadline, splitting waits at the host's
  1,000 ms limit. The standalone lane also checks that an errored script
  leaves the output off.
- [Timer probe](scripts/examples/timer_probe.rexx) records five 100 ms
  requests against the host's monotonic clock and reports the min, mean,
  and max observed delay. Its self-check catches an early wake.
- [Maintenance window](scripts/examples/maintenance_window.rexx) computes
  the next weekday window from a fixed UTC reference instant and logs it.
  The signed lane installs this script with zero grants and checks the
  resulting INFO event and suppression of DEBUG. To try it with the
  existing signed workflow, pass the script to `tools/rexx watch --once`
  with the device and signing options described above.

### Classic ESP32 board smoke image

The short smoke profile loads a RexxAE script from flash, checks timer
suspension and logging, and runs the timer probe. Use the full standalone
lane in QEMU for quota and deadline stress checks. On the Freenove FNK0114B,
the large parse in the quota test runs long enough to trigger the hardware
watchdog.

```sh
ESP32_TARGET=esp32 RUNNER_BOARD_SMOKE=1 ESP32_KEEP=/absolute/output/path ./lane.sh runner
```

The lane checks the image in QEMU and saves it at `out/runner.flash.bin`
under the output path. On a dedicated classic ESP32 board confirmed to have
4 MB of flash, write the merged image at address `0x0`. It replaces the
existing contents of that flash. UART output is at 115200 baud. This
profile leaves the display and touch pins unused.

### Freenove FNK0114B display console

```sh
ESP32_TARGET=esp32 RUNNER_CYD_CONSOLE=1 ESP32_KEEP=/absolute/output/path ./lane.sh runner
```

This hardware build saves a merged image at `out/runner.flash.bin` under
the chosen path. The FNK0114B smoke script mirrors Rexx `SAY`, log events,
and script errors to the 240×320 ST7789, with a scrolling 15×20 text grid.
USB serial still carries the full 115200-baud diagnostic output.
The LCD console currently displays ASCII output.

This build uses SPI2 with SCLK 14, MOSI 13, CS 15, DC 2, and an active-high
backlight on GPIO 21. It is specific to the FNK0114B ST7789 variant.

After the smoke checks, the CYD shows `rexx>`. Open USB serial at 115200
baud with local echo disabled. Enter one Rexx command or a short script
with semicolon-separated clauses. The panel shows the submitted command,
its output and the next prompt. Lines are limited to 160 ASCII characters.
Backspace edits a line, and Ctrl-C or Ctrl-U clears input before submission.

Each line runs in a fresh RexxAE context with the runner's existing memory
and time limits, uptime and bounded wait capabilities. Variables are scoped
to the submitted line. The prompt is for the local USB connection.
Run `python3 tools/test_cyd_serial.py /dev/cu.usbserial-110` with pyserial
installed to reset the attached board and exercise the prompt.

The FNK0114B's XPT2046 uses a separate SPI bus on GPIO 25 (clock),
32 (MOSI), 39 (MISO), and 33 (chip select). At the idle prompt a press
prints raw X/Y readings and a contact-strength reading on the display
and over USB. Release before taking another sample.

A local command can use `WAIT_TOUCH(ms)` with a whole number from 0 to
2000. It returns an object with `x`, `y`, and `pressure` on a new press,
or typed null when the wait expires. Coordinates are raw 12-bit readings.
Leave the panel untouched while running the serial regression test.

### Signed Wi-Fi runner for the FNK0114B

Build the image with:

```sh
ESP32_TARGET=esp32 ESP32_KEEP=/absolute/output/path ./lane.sh runner-cyd-wifi
```

The lane boots the unprovisioned image in QEMU and checks that it stops
before opening an endpoint. The merged image is saved at
`out/runner-cyd-wifi.flash.bin`. It is for the 4 MB FNK0114B.

Use a private directory for the following files. Put the 2.4 GHz WPA2
test-network credentials in `wifi.json` with `ssid` and `password` string
fields. The following commands are for initial provisioning and replace
the device image and its script slots. Run them from the repository root,
with paths adjusted for your test setup.

```sh
umask 077
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out fleet.pem
openssl pkey -in fleet.pem -pubout -out fleet-public.pem
python3 tools/rexx_psk.py cyd-credential.json cyd-psk.bin --generate cyd-dev
python3 tools/rexx_wifi.py wifi.json fleet-public.pem fleet-dev cyd-wifi.bin
esptool --chip esp32 --port /dev/cu.usbserial-110 --after no_reset write_flash \
  0x0 /absolute/output/path/out/runner-cyd-wifi.flash.bin
esptool --chip esp32 --port /dev/cu.usbserial-110 write_flash \
  0x330000 cyd-psk.bin 0x331000 cyd-wifi.bin
```

The fleet private signing key stays on the Mac. The Wi-Fi record contains
the station credentials and one public signing key. The TLS PSK is in
its own record. The board prints its DHCP address on USB serial at
115200 baud. The screen shows deployment output. This profile serves
only signed releases and keeps USB serial for diagnostics.

With a PSK-capable Python, send a first release:

```sh
python3.13 tools/rexx watch scripts/examples/maintenance_window.rexx --once \
  --device https://CYD_IP --device-credential cyd-credential.json \
  --private-key fleet.pem --key-id fleet-dev --name cyd-demo
```

### Signed USB runner for the FNK0114B

```sh
ESP32_TARGET=esp32 ESP32_KEEP=/absolute/output/path ./lane.sh runner-cyd-usb
```

The lane checks the unprovisioned image in QEMU and saves
`out/runner-cyd-usb.flash.bin`. Install pyserial in the Python environment
used for USB deployment. With a P-256 fleet public PEM key, provision the
4 MB board as follows. The full image write resets the board's script slots.

```sh
python3 tools/rexx_usb.py provision fleet-public.pem fleet-dev usb-trust.bin
esptool --chip esp32 --port /dev/cu.usbserial-110 write_flash \
  0x0 /absolute/output/path/out/runner-cyd-usb.flash.bin
esptool --chip esp32 --port /dev/cu.usbserial-110 write_flash 0x332000 usb-trust.bin
python3 tools/rexx_usb.py status --port /dev/cu.usbserial-110
python3 tools/rexx_usb.py deploy scripts/examples/maintenance_window.rexx \
  --port /dev/cu.usbserial-110 --private-key fleet.pem --key-id fleet-dev --name cyd-demo
```

The Mac signs each release. The CYD runs the trial, displays script output,
and returns an activation or rollback receipt over USB. Use one serial
client at a time. The signed USB profile uses USB for updates and status.

Deploy the tap example with an explicit touch grant.

```sh
python3 tools/rexx_usb.py deploy scripts/examples/signed_tap.rexx \
  --port /dev/cu.usbserial-110 --private-key fleet.pem --key-id fleet-dev \
  --name cyd-demo --grant TOUCH_EVENT
```

The example displays `Tap app ready` during the deployment trial and when
restored at boot or after a rollback. Tap anywhere to run the active
release and see `Tap received` with raw controller coordinates. Change
that message and deploy again to change the tap response over USB.

The USB runner mounts `TOUCH_EVENT()` only for releases with the signed
grant. It returns a typed object with `x`, `y` and `pressure` on a tap, and
JSON null during a trial or restore. Each tap starts a fresh execution.
The same execution limits apply, and tap results leave the active release
and version floor intact.

The two-button example adds a separate display grant.

```sh
python3 tools/rexx_usb.py deploy scripts/examples/signed_buttons.rexx \
  --port /dev/cu.usbserial-110 --private-key fleet.pem --key-id fleet-dev \
  --name cyd-demo --grant TOUCH_EVENT --grant DISPLAY_BUTTONS
```

`DISPLAY_BUTTONS(title, top, bottom, status)` stages a portrait screen
using four printable ASCII strings of at most 13 characters each. The
runner draws the last requested screen when the script exits successfully.
Script output for releases with the display grant goes to serial. On the
240×320 panel the buttons cover x in [16, 224) and y in [64, 128) for the
upper button or [160, 224) for the lower button.

The example starts from Freenove's FNK0114B reference calibration. Its Rexx
code maps raw touch samples to screen coordinates, selects the button, and
sets the status line. Edit the calibration or change `Hello, CYD!` in the
script and deploy again over USB. The app's hit testing can be exercised
against a native RexxAE library.

```sh
python3 project/test_cyd_buttons.py ../RexxAE/build/librexxae.dylib
```

The [three-button Rexx example](scripts/examples/signed_layout.rexx) uses
the same two grants and declares its own layout.

- `DISPLAY_FRAME(title, status)` requests a screen update at successful exit.
- `DISPLAY_BUTTON(id, label, col, row, width, height)` adds a button to that
  invocation's layout. Use unique integer IDs from 1 to 255 and up to six
  buttons. The 16×16-pixel cells use columns 0 through 14 and rows 3 through
  16. Rectangles may share edges. Overlap, labels that do not fit, and
  rectangles outside the button area fail the script.
- `BUTTON_EVENT()` requires both grants. It returns the ID at the tap
  position in the current release's displayed layout, or JSON null
  on boot, during a deployment trial, or outside a button. The host
  uses the FNK0114B reference calibration for hit testing.

Deploy the script with the command above using `scripts/examples/signed_layout.rexx`.
To move HELLO to the right, change its column from 1 to 8 and deploy again.
Labels, positions, and actions are defined in Rexx. The fixed two-button
function remains available for existing scripts.

Run the host geometry test from the repository root.

```sh
cc -std=c11 -Wall -Wextra -Werror -DRUNNER_USB -Iproject/main \
  project/test_cyd_layout.c project/main/cyd_layout.c -o /tmp/test-cyd-layout
/tmp/test-cyd-layout
```

### Signed Rexx-to-Rexx ADDRESS on the CYD

```sh
python3 tools/rexx_usb.py deploy scripts/examples/signed_address.rexx \
  --port /dev/cu.usbserial-110 --private-key fleet.pem --key-id fleet-dev \
  --name cyd-demo --grant ADDRESS_LABELS
```

The firmware exposes the named `LABELS` route to releases with the
`ADDRESS_LABELS` grant. `ADDRESS LABELS 'status'` calls a separate Rexx
program and sets `RC`, `RESULT` and `ERRORTEXT` in the caller. An unmounted
target or a missing grant aborts the invocation under the existing
runner policy. The network profiles reject the USB-only grant.

The [LABELS handler](scripts/services/labels.rexx) is selected and bundled
by the firmware. For each call the host starts a fresh Rexx context, sets
`ADDRESS_COMMAND`, and returns one optional `SAY` as the text result and
the handler's exit code as `RC`. It can use Rexx builtins. It has no mounted
functions or command routes and shares the caller's execution account.
Commands are limited to 128 printable ASCII bytes, replies to 128 bytes,
and the peer receives at most four bursts of 32 instructions. A rejected
command returns RC 4. A peer execution failure returns RC 20. A failure
of the shared Aether execution account aborts the caller's invocation.

The firmware owns the route policy and provides a built-in version-zero
handler. On the signed USB profile, install a separate signed peer with:

```sh
python3 tools/rexx_usb.py peer-status --port /dev/cu.usbserial-110
python3 tools/rexx_usb.py peer-deploy scripts/services/labels.rexx \
  --port /dev/cu.usbserial-110 --private-key fleet.pem --key-id fleet-dev
```

Peer releases have a separate version floor, A/B slots, and the fixed signed
name `peer.labels.v1`. They carry zero grants. The app upload endpoint
rejects that name, and the peer endpoint rejects other names. To activate,
the candidate must answer the firmware's `status` and `upper usb Probe`
requests and return RC 4 for an unknown command. A failed trial keeps the
previous peer. A successful update takes effect on the next call, including
from an already installed app. Desktop RexxAE uses the same existing
ADDRESS ABI and leaves routing to its embedding application.

An existing CYD needs the updated app and partition table. For this exact
layout the partition table is at 0x8000, and the new peer slots use previously
unallocated space at 0x333000 and 0x336000. The existing app slots, NVS and
trust partition retain their offsets. Use a full image write for fresh boards.

Run the native peer test from the repository root with a built RexxAE
library and the matching Aether headers. On macOS:

```sh
cc -std=gnu11 -Wall -Wextra -Werror -Iproject/main \
  -I../aether/include -I../RexxAE/include \
  project/test_runner_peer.c project/main/runner_peer.c \
  -L../RexxAE/build -lrexxae -Wl,-rpath,"$PWD/../RexxAE/build" \
  -o /tmp/test-runner-peer
/tmp/test-runner-peer
```

### Signed timed switch on the CYD

```sh
python3 tools/rexx_usb.py deploy scripts/examples/signed_switch.rexx \
  --port /dev/cu.usbserial-110 --private-key fleet.pem --key-id fleet-dev \
  --name cyd-demo --grant TOUCH_EVENT --grant DISPLAY_BUTTONS --grant SWITCH_CONTROL \
  --grant SETTINGS_READ
```

The CYD shows a simulated load with **ON** and **OFF** buttons. The ON
label displays the configured timeout, which defaults to five seconds.
`ADDRESS SWITCH 'status'` returns `ON` or `OFF` with either switch grant.
`SWITCH_CONTROL` also allows `on N` for 1–60 seconds and `off` during an
active touch invocation. To use the same app as a read-only panel, sign it
with `SWITCH_READ` in place of `SWITCH_CONTROL`. Unavailable control returns
RC 13. Invalid commands return RC 4.

The host stages a command until the active invocation exits successfully.
A failed control invocation puts the load off. Boot, restore, deployment
trials and timer notifications have read access only. An admitted update
starts from off. A host ESP timer expires the load even while Rexx is idle
or USB is busy, and the runner reruns the active switch app read-only to
refresh the display. The load is firmware state and resets to off on reboot.

Run the switch policy test from the repository root.

```sh
cc -std=c11 -Wall -Wextra -Werror -Iproject/main \
  project/test_runner_switch.c project/main/runner_switch.c -o /tmp/test-runner-switch
/tmp/test-runner-switch
```

### Persistent switch settings

The switch panel reads `ADDRESS SETTINGS 'get auto_off_seconds'` and
uses the reply in its next switch command. A different signed maintenance
app can change the setting:

```sh
python3 tools/rexx_usb.py deploy scripts/examples/signed_settings.rexx \
  --port /dev/cu.usbserial-110 --private-key fleet.pem --key-id fleet-dev \
  --name cyd-demo --grant TOUCH_EVENT --grant DISPLAY_BUTTONS --grant SETTINGS_WRITE
```

Press **SET 10 SEC** or **RESET TO 5**, then deploy the switch panel again.
`SETTINGS_READ` permits reads. `SETTINGS_WRITE` also permits
`set auto_off_seconds N` for an integer from 1 to 60 during an active
touch invocation. Other contexts can read. Unknown keys or malformed
commands return RC 4, denied writes return RC 13, and a storage fault
returns RC 20.

A write is staged until the invocation and any requested screen update
succeed. The caller can read its pending value during that invocation.
The host commits changed values to a separate NVS namespace. A failed
invocation discards the pending value, and a rejected deployment cannot
change the stored setting. The committed value survives reboot and app
replacement. Only the one named setting is exposed.

```sh
cc -std=c11 -Wall -Wextra -Werror -Iproject/main \
  project/test_runner_settings.c project/main/runner_settings.c -o /tmp/test-runner-settings
/tmp/test-runner-settings
```

### Bench switch output on the FNK0114B

The normal signed USB image uses a simulated load. To bind the switch to
the FNK0114B's onboard red LED, build the board-specific image:

```sh
ESP32_TARGET=esp32 RUNNER_CYD_SWITCH_LED=1 \
  ESP32_KEEP=/absolute/output/path ./lane.sh runner-cyd-usb
```

Freenove's board example identifies the red LED as active-low GPIO22.
The firmware claims that output, sets it off on startup, and checks
the pad level when changing it. The same signed `SWITCH_CONTROL` grant,
touch-only write policy, and host timer apply. An expired timer drives
the pad off before notifying the app. The Rexx code receives no pin
number or GPIO configuration API.

The ownership regression adapts Toit's 0BSD GPIO reservation test.
It checks that a second owner cannot claim, change or release the output,
and that release drives it off before another owner can claim it.

```sh
cc -std=c11 -Wall -Wextra -Werror -Iproject/main \
  project/test_runner_switch_output.c project/main/runner_switch.c -o /tmp/test-runner-switch-output
/tmp/test-runner-switch-output
```
