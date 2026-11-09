# Plan

Where this is going: a **runner** firmware. Native firmware (C and Aether,
reflashed rarely) owns the hardware and holds the RexxAE engine. Rexx
scripts are what goes over the air: small, signed, granted only the
functions their manifest names, held to an instruction budget, and
stoppable without resetting the chip.

Written 2026-11-04 from the 2026-11-03 session, so the strands don't get
lost. The README has the results so far. Each strand below says what is
done, what is next, and what it waits on.

## Status at a glance

| Strand | State | Waits on |
|---|---|---|
| 1. Lane housekeeping | done, 2026-11-04 | — |
| 2. More chips in QEMU (plain ESP32, S3 without PSRAM) | done, 2026-11-04 | — |
| 3. Engine memory and speed | partly understood | real timings (5) |
| 4. The runner firmware | signed manifests, timer grants and durable script rollback tested | production provisioning/transport, broader services |
| 5. The Guition board | ordered | the board arriving |
| 6. The CYD (plain ESP32, no PSRAM) | serial, display, touch and signed USB updates verified on FNK0114B | Wi-Fi handshake |
| 7. Time, scheduling and logging | cron and logging done | DATE/TIME in RexxAE |
| 8. Dialect and policy decisions | open | Paul |
| 9. Upstream loose ends | found, not fixed | nothing |

## 1. Lane housekeeping

Completed 2026-11-04. The lane no longer carries `AE_EXTRA_STD` and uses
Aether's released cooperative-runtime source list. `AETHER_REF` pins
v0.891.0's exact commit. The compiler and runtime are built/staged from one
isolated release snapshot; sibling working trees are untouched. The old
main/branch measurement columns are replaced by current release results.
See README for commands and measurements.

## 2. More chips in QEMU, before any board arrives

Completed 2026-11-04: `ESP32_TARGET=esp32` (4 MB flash, no PSRAM) and
`ESP32_TARGET=esp32s3-no-psram`, alongside the original S3 with PSRAM.
Each runs hello, the RexxAE benchmark/retention harness and the first runner
milestone. Size and heap measurements are in README. These are emulator
results, not measurements of CYD peripherals or hardware timing.

## 3. Engine memory and speed

- **Parse time.** QEMU says 0.27 s to parse 107 lines, which is not a real
  figure (QEMU isn't cycle-accurate). If the board shows anything like
  it, the parser comes first.
- **std.json deep copies.** `json.set` and `json.push` copy the subtree,
  so building a tree bottom up copies each subtree again at every level.
  This is the likely cause of the peak heap (76 to 87 KB) being about
  twice the live heap (41 KB). A fix is an aether change: moving instead
  of copying when the value is a temporary. aether-lang-dev/aether#3544
  (Nic's hardening PR, not merged yet) already moves JSON children "when
  their arena is well used": check what's left after it merges.
- **3 KB kept after the first two contexts**, never growing afterwards.
  Probably one-time lazy setup in the runtime or engine. Not identified.
  This matters only if it turns out to be per-script.
- **Memory quota and interruption cleanup, 2026-11-05.** RexxAE
  `c8b517d` releases the parser and evaluator allocations exercised by the
  quota sweep. The runner uses a 128 KiB execution account and treats the
  runtime outcome as authoritative. QEMU exercises repeated allocation
  refusal during both parsing and execution and checks heap recovery.
  The same account now sets a five-second elapsed deadline. RexxAE
  `ef7a744` frees builtin buffers on interruption. QEMU tests repeated
  deadline failures inside parsing and a long builtin. See the earlier
  [findings](asks/rexxae-resource-interruptions.md) for the original failure.
- What it bounds: how large a script a device with no PSRAM can take.

## 4. The runner firmware

First development milestone implemented 2026-11-04 in
`project/main/runner_host.c`: cooperative stepping with a per-step budget,
a bounded host run, serial SAY/LOG output, `UPTIME_MS` and asynchronous
`WAIT_MS`, and a script read from a flash data partition. QEMU checks
expression suspension, denial, bad arguments and runaway cancellation.
The custom layout reserves A/B script slots. The original runner loads A;
`runner-http` accepts bounded unsigned source over emulated Ethernet, writes
B, reads it back and executes it using the same timer-only host authority.
It preserves A and reports completion/cancellation over HTTP.

The separate `runner-signed` profile now verifies P-256/SHA-256 signed
manifests, mounts individual timer grants, writes/read-verifies the inactive
slot, durably records a trial and promotes only a healthy zero-exit run.
Failures and reboot-interrupted trials restore the prior active release.
NVS retains the application name and highest version admitted to a trial;
replays and a different application name are rejected. Both failure and
healthy-reset paths pass in QEMU. See SIGNING.md for the exact policy.
Production keys/provisioning, hardware Wi-Fi, fleet transport and broader
pin/topic grants remain open.

The remaining design follows:

- **The loop.** The runner calls `aether_rexxae_step(ctx, budget)` and
  yields between steps, so the watchdog stays fed and a runaway script
  only uses up its budget. Wi-Fi, OTA and the display get time in
  between. The second core is unused so far.
- **Slow waits** (`WAIT_PIN`, `MQTT_RECEIVE`, a timer) are mounted
  functions that the runner answers later. The script stays suspended
  meanwhile; RexxAE's suspend and resume on host calls already work this
  way.
- **Mounted functions:** GPIO, I2C, timers, HTTP or MQTT, storage,
  logging, and later the display (`DRAW_TEXT`, `FILL_RECT`, `ON_TOUCH`).
  A script gets only what its manifest grants, down to which pins or
  topics. An unmounted `ADDRESS` command is refused.
- **The script store:** a flash data partition with two slots, A and B,
  and a manifest giving name, version, signature and grants. An update is
  checked against its signature, written to the spare slot, switched to
  and run on trial. If it errors or fails a health check within N
  seconds, the runner switches back.
- **Delivery:** `tools/rexx watch` signs changes locally and pushes them to the
  development endpoint. The signed QEMU lane checks activation, rollback
  after a bad edit, and recovery after a corrected save. HTTPS or MQTT pull
  remains the fleet delivery direction. The OpenSSL-free Aether TLS probe
  served TLS 1.3 in ESP32 QEMU. Combining it with the signed runner exposed
  shared execution state across FreeRTOS tasks in the cooperative build.
  [HTTPS measurements and port gaps](asks/aether-esp32-https.md)
  The separate `psk-probe` lane passed TLS 1.3 external PSK authentication
  using ESP-TLS, a key provisioned at flash time, and a real HTTP status
  handler. The `runner-signed-psk` profile and watcher share the
  flash-provisioned credential format. The signed bundle workflow remains
  independent of transport authentication. `rexx sync` uses the verified
  active fingerprint to skip matching devices and reconciles each device
  against its own version floor.
- **Planned firmware updates** will use ESP-IDF's two app slots with rollback.
  16 MB of flash holds two app slots plus the script slots.
- **Output:** `SAY` and `LOG_*` go to the serial port or a remote log.
- **First milestone (done):** the loop plus two mounted functions plus a script
  loaded from a flash partition, all in QEMU. Development HTTP delivery also
  passes on plain ESP32's emulated Ethernet (`open_eth`). The signed profile
  also tests timer grants, durable slot trials, replay state and rollback.
  Production provisioning and authenticated fleet transport remain open. QEMU's esp32s3
  has no Wi-Fi; physical networking still waits for a board.
- Toit's `lib/` (MIT) may be ported with provenance: its service
  interfaces (`lib/system/api/`), CoAP and NTP are the candidates. Its
  VM and tools are LGPL and must not be ported. Its BSD-0-Clause examples
  may also be ported, retaining their licences and original copyright
  holders. Check separate package dependencies independently. See
  [PROVENANCE.md](PROVENANCE.md) for the policy and inspected revision.

## 5. The Guition board

Guition JC3248W535C (ESP32-S3-N16R8: 16 MB flash, 8 MB octal PSRAM, 3.5"
320x480 IPS, capacitive touch). Ordered at about £19.

- **First flash:** this lane's RexxAE firmware as it is, so the first
  thing on the board is known to work. Then the real numbers: parse and
  run times at 240 MHz, PSRAM's cost against internal RAM.
- **Display:** the AXS15231B controller (over QSPI), which also does the
  touch. It is less common than the usual controllers, so most Arduino
  examples won't work on it as they are. The route: Espressif's
  `esp_lcd_axs15231b` component for ESP-IDF, with settings from Guition's
  sample code and rzeldent's `esp32-smartdisplay`. Confirm the controller
  once the board is here.
- Then the display's mounted functions (strand 4); LVGL later, if needed.

## 6. The CYD

The attached CYD is the Freenove FNK0114B with classic ESP32, 4 MB flash,
a 240×320 ST7789 and XPT2046 touch. Hardware tests cover loading a Rexx
script from flash, timers, the USB Rexx prompt, and raw touch delivered
both at idle and through `WAIT_TOUCH`. The full quota stress suite exposed
watchdog warnings on this board. Use the short board smoke profile for
bring-up.

The `runner-cyd-wifi` development image combines the signed script store,
ESP-TLS PSK endpoint, Wi-Fi station networking and display output. Next
verify an actual Wi-Fi connection and signed activation and rollback on the
CYD. The separate `runner-cyd-usb` profile already exercises signing,
activation, rollback and reboot recovery over USB on the same board.
The signed USB runner supports a `TOUCH_EVENT` grant and runs the active
release on a tap. The `signed_tap.rexx` example distinguishes a tap from a
deployment trial or boot. Signed USB and network regression lanes cover
the profile boundary, and an on-device trial without the grant rolls back.
A physical tap on release v7 displayed the updated message and two raw
coordinates after a signed USB update.

The signed two-button demo draws labels and a status line through the
separate `DISPLAY_BUTTONS` grant. Its Rexx script owns the portrait touch
calibration and hit testing. The native RexxAE test covers both hit
regions and the gaps. Both on-screen buttons worked on the FNK0114B
after power was restored.

The three-button demo declares rectangles and IDs in Rexx. The CYD host
calibrates a tap and resolves it against the visible release's layout.
A signed USB update moved HELLO to the upper right. A physical press on
release v16 displayed the new action, `Moved by USB`.

The signed USB runner can now route `ADDRESS LABELS` to a second Rexx
program under the `ADDRESS_LABELS` grant. The firmware runs it in a
fresh, bounded context. LABELS has an independently signed USB release,
its own A/B slots and version floor, and a firmware-owned v1 contract.
The built-in peer supplies the default before the first installation. The pinned RexxAE ABI supports literal
and variable-led text commands. RexxJS heredoc command syntax and
structured `RESULT` values remain parity gaps.

`ADDRESS SWITCH` applies the same host routing to a simulated load.
Separate signed grants support a read-only panel and an active control
panel. The firmware owns the shutoff deadline and keeps control disabled
during deployment trials. An opt-in FNK0114B bench image drives the
onboard red LED through an exclusively owned GPIO22 output. The
ownership regression adapts the corresponding Toit 0BSD test.

`ADDRESS SETTINGS` exposes one bounded NVS-backed setting,
`auto_off_seconds`. The signed switch panel reads it. A separately
granted maintenance app can stage a change during a touch invocation
and commit it on success.

## 7. Time, scheduling and logging

- **Done in RexxAE:** `LOG_TRACE` to `LOG_FATAL`, with the threshold set
  by the host, and `CRON_VALID` / `CRON_NEXT` in UTC (see
  `../RexxAE/docs/cron-and-logging.md`). On the device, the runner would
  route logs to serial or a remote log, and use `CRON_NEXT` to wake
  scripts.
- **Next, in RexxAE:** `DATE` and `TIME` with the classic option letters,
  the host owning "now" and the time zone (see `../RexxAE/TODO.md`). On
  the device, "now" comes from NTP or an RTC through the firmware, and
  the engine needs no time-zone database.
- In RexxJS, `DATE('B')` counts from 1901, not year 1. That gets fixed
  there as part of the same shared spec.

## 8. Decisions for Paul

- **`INTERPRET` on a device.** RexxJS has it; RexxAE doesn't yet. It runs
  any string as code, so on a device that takes only signed scripts it
  would let unsigned code run. The options: leave it out of the device
  build, or make it a grant in the manifest.
- **Protocols stay native.** Neither engine has the byte functions
  (`C2D`, `X2C`, `BITAND`...), so CoAP, DNS and the like stay in the
  firmware and reach scripts as mounted functions. Adding the byte
  functions would be a change to the dialect, made in both engines.
- **Signing scope decided, 2026-11-04:** one release-signing authority per
  fleet. Its private key stays off-device; devices hold trusted public
  verification keys. Device identity/authentication credentials are separate.
  Use key identifiers and a provisioned trust set so keys can rotate and the
  scope can change later. See [SIGNING.md](SIGNING.md).
- **Dialect surprises** to know before porting classic Rexx (RexxJS
  behaviour, which RexxAE follows): `%` is remainder, `//` starts a
  comment, `POS` differs, and a template that starts with a literal
  leaves the literal in the first variable. These are listed in the
  README.

## 9. Upstream loose ends

- aether's `make test-json-conformance` and `test-json-asan` don't link on
  main: they compile `aether_json.c` without the string runtime.
- The generated C and the runtime are not warning-clean under xtensa gcc,
  so they build with `-w`. The host builds with `-Werror`.
