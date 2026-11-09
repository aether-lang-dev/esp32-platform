# Reuse and provenance

The Toit reference checkout is `/Users/paul/scm/not-ours/toit`, inspected at
`ec8860aa25d35e54f5cb3bcd437133054eccae8c`. Treat it as read-only.

- `lib/LICENSE` is MIT, copyright (c) 2021 Toit contributors. Individual
  files also name their original holders (for example `lib/coap/message.toit`
  names Copyright (C) 2019 Toitware ApS. All rights reserved).
- `examples/LICENSE` is Zero-Clause BSD, copyright (C) 2021 Toit contributors.
  Example headers retain their own holders and years; `examples/http/http.toit`
  names 2021 Toitware ApS and `examples/ntp/ntp.toit` names 2022 Toitware ApS.
- **Do not port LGPL pieces**, including Toit's VM and tools.

Before porting a file, check its actual applicable licence. Keep its original
licence and copyright notices with the port, include the licence text, and
record source path, upstream revision and the nature of the adaptation here.
This retention policy applies to BSD-0-Clause examples too.

Example dependencies need their own review: the HTTP example depends on
`github.com/toitlang/pkg-http`, and the NTP example on
`github.com/toitlang/pkg-ntp`. The examples' BSD-0-Clause licence does not
establish the licences of those packages.

## Ports present

- [LED demo](scripts/toit/led.rexx) adapts Toit's `examples/gpio.toit` from
  `ec8860aa25d35e54f5cb3bcd437133054eccae8c`. It preserves the original
  two-second on/off sequence, using two bounded waits and the runner's
  fixed-output `LED_WRITE` capability. The port keeps the Toitware ApS
  copyright and [Zero-Clause BSD licence](scripts/toit/LICENSE).
- [Bubble sort](scripts/toit/bubble_sort.rexx) adapts Toit's
  `examples/bubble_sort.toit` from the same revision. It preserves the
  adjacent-swap sort and before/after output, using fixed input so the
  result can be checked without a randomness capability.
- [Mandelbrot](scripts/toit/mandelbrot.rexx) adapts Toit's
  `examples/mandelbrot.toit` from the same revision. It keeps the escape-time
  loop and renders a single small ASCII grid for the bounded serial runner.
  Both ports retain the Toitware ApS copyright and Zero-Clause BSD licence.
- [Hello](scripts/toit/hello.rexx) adapts Toit's `examples/hello.toit`
  from the same revision, using Rexx `SAY` for its greeting.
- [Logging](scripts/toit/service_log.rexx) adapts the application behavior
  of Toit's `examples/service/log.toit` from the same revision. The
  Rexx version sends both messages through the runner's existing log
  event sink and uses its monotonic uptime in milliseconds in place of
  the Toit provider's microsecond clock. It does not port the Toit
  service framework. Both ports retain the original Toitware ApS
  copyrights and Zero-Clause BSD licence.
- [I²C scanner](scripts/toit/i2c_scan.rexx) adapts `examples/i2c.toit`.
  The runner owns the bus and probes standard unreserved 7-bit addresses.
  The serial report uses decimal addresses.
- [UART loopback](scripts/toit/uart_loopback.rexx) adapts `examples/uart.toit`.
  It sends and verifies three lines with bounded reads on a dedicated UART.
- [Random die](scripts/toit/random_die.rexx) adapts the random client
  workload from `examples/service/random.toit`. The host supplies bounded
  random values. The port covers the two draw ranges and omits the
  cross-process resource proxy and notifications.
- [HTTP body counter](scripts/toit/http.ae) adapts `examples/http/http.toit`
  as a desktop Aether program with a configurable URL and streaming reads.
  It uses Aether's HTTP client. No Toit package code is included.

These four ports use the same inspected revision and retain the original
Toitware ApS copyrights and Zero-Clause BSD licence. The ESP-IDF adapters
and loopback test are original integration code.

- [Pulse counter](scripts/toit/pulse_counter.rexx) adapts `examples/pcnt.toit`
  from the same revision. ESP-IDF generates the square wave and counts edges.
  Rexx keeps the original five 500 ms samples and checks the counts increase.
- [Timer wake](scripts/toit/deep_sleep.rexx) adapts `examples/deep_sleep.toit`
  from the same revision. It reports the wake reason and uptime for this boot.
  The firmware owns the 10-second sleep cycle.
- [GPIO wake](scripts/toit/gpio_wake.rexx) adapts `examples/triggers/gpio.toit`
  from the same revision. It preserves GPIO 32 wake and the 30-second fallback.
  All three retain their original Toitware ApS notices and Zero-Clause BSD licence.

- [Wi-Fi scan](scripts/toit/wifi_scan.rexx) adapts `examples/wifi/scan.toit`
  from the same revision. The host performs a passive scan on channels 1–7,
  retaining up to eight access points and replacing non-printable and JSON
  delimiter bytes in SSIDs for serial display. The port retains the original
  Toitware ApS notice and Zero-Clause BSD licence.

### Standalone hardware and desktop ports

The remaining thirteen BSD-0-Clause source files at the same Toit revision
are adapted to standalone ESP-IDF examples. They use ESP-IDF drivers and
NimBLE with their original Toitware ApS notices and
[Zero-Clause BSD licence](examples/toit-idf/LICENSE).

| Toit source | Adaptation |
| --- | --- |
| `examples/wifi/connect.toit` | [Wi-Fi station reporting](examples/toit-idf/main/wifi.c) |
| `examples/ntp/ntp.toit` | [Clock synchronization](examples/toit-idf/main/wifi.c) via ESP-IDF SNTP |
| `examples/espnow.toit` | [Broadcast and receive](examples/toit-idf/main/espnow.c) |
| `examples/ethernet/simple/main.toit` | [Board entry point](examples/toit-idf/main/ethernet.c) |
| `examples/ethernet/full/olimex_poe.toit` | [Olimex PHY lifecycle](examples/toit-idf/main/olimex_poe.c) |
| `examples/ethernet/full/provider.toit` | [Ethernet provider](examples/toit-idf/main/ethernet_provider.c) |
| `examples/ethernet/full/client.toit` | [Ethernet client](examples/toit-idf/main/ethernet_client.c) |
| `examples/triggers/touch.toit` | [Touch calibration and wake](examples/toit-idf/main/touch_wake.c) |
| `examples/ble/scan.toit`, `scan-active.toit`, `connect.toit` | [BLE central modes](examples/toit-idf/main/ble_central.c) |
| `examples/ble/advertise.toit`, `heart_rate.toit` | [BLE peripheral modes](examples/toit-idf/main/ble_peripheral.c) |

The Ethernet provider and client are compiled into one app, with separate
lifecycle functions. The NTP port uses ESP-IDF's SNTP implementation.
The radio ports keep the original example roles and use bounded report
storage. No Toit VM, radio library, or external Toit package code was copied.

The [RSA example](scripts/toit/rsa/rsa_gen.py) adapts
`examples/rsa_gen.toit`, whose header points to the MIT licence in Toit's
`lib/LICENSE`. It uses the desktop OpenSSL CLI for generation, DER reload,
signing, verification, and both encryption round-trips, and keeps generated
keys in a temporary directory. It retains the Toit contributors copyright
and [MIT licence](scripts/toit/rsa/LICENSE).

### FNK0114B display console

The console uses the documented FNK0114B pin map and ESP-IDF's ST7789
driver. No Freenove sketch source is included.
[font8x8_basic.h](project/main/font8x8_basic.h) comes from Daniel Hepper's
[public-domain font8x8](
https://github.com/dhepper/font8x8/blob/8e279d2d864e79128e96188a6b9526cfa3fbfef9/font8x8_basic.h).
The upstream header retains its credits. The glyph table is declared static
and read-only in this copy.

### CYD switch output ownership test

[The ownership regression](project/test_runner_switch_output.c) adapts the
claim, conflict and release scenario from the local Toit snapshot's
`tests/hw/esp32/gpio-num-reservation-test.toit`. The snapshot directory
has no Git metadata. The port retains the 2026 Toit contributors source
notice and the original 0BSD test license and copyright in the test file.
It exercises the firmware-owned logical output with a deterministic
backend. The ESP32 driver uses the active-low GPIO22 red LED identified
in Freenove's RGB and Wi-Fi LED sketches.
