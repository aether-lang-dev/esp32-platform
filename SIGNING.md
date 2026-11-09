# Release signing and script rollback

Agreed with Paul on 2026-11-04: one release-signing authority per fleet,
private keys kept off-device, trusted public verification keys on devices.
Device identity/server-authentication credentials are separate. Explicit key
IDs and a host-owned trust set allow rotation and later scope changes.

## Tested implementation

```
ESP32_TARGET=esp32 ./lane.sh runner-signed
python3 tools/test_rexx_bundle.py
```

The QEMU profile generates disposable P-256 test keys in container scratch,
compiles only their public points into the firmware, and tests current/next
keys alongside an untrusted signer. It does not provision production keys.
There is no unsigned fallback in this profile. `runner-http` remains a
separate unsigned development target.

`tools/rexx_bundle.py` packages source using an off-device P-256 PEM private
key and OpenSSL's signing CLI. Firmware verification uses ESP-IDF's PSA
crypto API (ECDSA with SHA-256), with no OpenSSL dependency. The signing tool
defaults to zero mounted-function grants and refuses to overwrite its source
or private key with the output file.

The signed binary manifest binds the script name, version, key ID, grants
and exact source bytes. The signed profiles support UPTIME_MS (bit 0) and
WAIT_MS (bit 1). The FNK0114B signed USB profile also supports TOUCH_EVENT
(bit 3), DISPLAY_BUTTONS (bit 4), ADDRESS_LABELS (bit 5), SWITCH_READ
(bit 6), SWITCH_CONTROL (bit 7), SETTINGS_READ (bit 8), and SETTINGS_WRITE
(bit 9). ADDRESS_LABELS mounts the
firmware-owned LABELS peer route. The switch grants mount the SWITCH
route. SWITCH_CONTROL includes read access and permits changes during
a successful active touch invocation. The settings grants mount the SETTINGS
route. SETTINGS_WRITE includes reads and permits staging a write during
an active touch invocation. Each profile rejects
bundles carrying any other bits, even with a valid signature. Existing
RexxAE builtins and serial SAY/LOG output remain subject to the host's
existing policy. Uploads cannot replace the trust set.

## Bundle encoding, version 1

All integers are unsigned little-endian. No padding or trailing data:

| Offset | Field |
|---:|---|
| 0 | 8-byte magic `RXAEUP01` |
| 8 | u32 script version, 1..4294967295 |
| 12 | u32 grant mask |
| 16 | u16 key-ID byte length, 1..64 |
| 18 | u16 application-name byte length, 1..64 |
| 20 | u32 source byte length, 1..8192 |
| 24 | key ID, application name, then source bytes |
| end-64 | fixed 64-byte P-256 ECDSA signature, big-endian r followed by s |

Key IDs and names contain only ASCII letters, digits, `_`, `-`, `.`.
Source must be NUL-free. The signature covers every preceding byte, including
the complete header and manifest. Maximum bundle size is 8,408 bytes. The
firmware selects a provisioned public key by the signed ID, verifies before
writing flash, then verifies the bytes read back before executing them.

## Durable A/B trial policy

The signed host uses both 64 KiB script slots, with an explicit versioned NVS
record for the active slot/version, application name, trial slot and highest
version admitted to a trial. A fresh device has no signed active release;
it does not execute the unsigned development seed as a fallback.

1. Refuse a version at or below the recorded high-water mark, or a different
   application name once the first trial has bound the runner to one name.
2. Verify, write and read/verify the inactive slot. Record the new high-water
   mark and pending trial durably before executing candidate code.
3. A trial is healthy only when the bounded run exits zero, without an engine
   or Aether runtime failure. Commit activation only after that result.
4. On failure, retain the prior active release and execute it again. A reboot
   with a pending trial rejects that trial and restores the prior release.
   The high-water mark survives, including for failed/interrupted trials.

The current health predicate is successful completion of a bounded script;
a readiness callback for indefinitely running services remains future work.
A queued HTTP 202 receipt is not proof of staging, health or activation.
`GET /status` exposes active/highest/trial versions, active slot, storage
fault and runtime outcome alongside the request's execution status.
`activeFingerprint` is the lowercase SHA-256 of the active bundle's signed
bytes with the four version bytes at offsets 8–11 omitted. It includes
the signing key ID, app name, grants, and source. It is empty until an
active bundle has been verified or promoted. The runner reconstructs it
from verified flash contents at boot, using the existing NVS record.

NVS errors or malformed state fail closed; the runner never erases NVS to
silently reset the version floor. A higher valid release can repair an
unverifiable active image if the metadata store remains sound. Changing the
application identity or resetting its floor requires explicit reprovisioning.
This is a software/NVS replay barrier, not hardware secure boot or resistance
to replacing the complete flash image. Firmware OTA/secure boot, production
key provisioning, key-rotation operations and authenticated fleet transport
remain open. Flash/NVS write-failure injection and broader power-loss coverage
remain to be tested.

## Resource handling and transport

Every engine export's ABI return is checked against `aether_last_outcome()`
before another export can overwrite that outcome. In particular, zero from
an interrupted step must not be treated as successful REXXAE_DONE. OOM trials
are rejected and cannot become active releases.

Each trial runs under an Aether execution account with a 128 KiB memory cap
and a five-second elapsed deadline. Exceeding the cap or reaching the
deadline at a cooperative poll fails the trial and restores the prior active
release. The host detaches the account for teardown and checks that it holds
zero bytes after destruction.

For production HTTPS, Aether's `std.http` with its pure-Aether TLS 1.3 backend
is an option. The present lane uses a networking-disabled Aether runtime and
ESP-IDF HTTP for its native host. Evaluate the required host/runtime build
and measure emitted firmware plus runtime heap/stack; do not infer footprint
from the import list. This transport choice is independent of release signing.

## CYD Wi-Fi development profile

`runner-cyd-wifi` uses the same signed slots and device TLS PSK protocol as
`runner-signed-psk`, with ESP-IDF Wi-Fi station networking. It loads one
fleet P-256 public key and Wi-Fi station configuration from the private
`device_wifi` partition at `0x331000`. The separate `device_key` PSK
partition remains at `0x330000`. An erased or invalid Wi-Fi record stops
startup before any network service starts.

The Wi-Fi record begins with eight ASCII bytes `RXWIFI01`, then one-byte
SSID and password lengths, a 32-byte SSID field, a 63-byte password field,
a 65-byte NUL-terminated key ID field, and a 65-byte uncompressed SEC1
public point. The tool pads the text fields with zeros. This is test
provisioning for an ESP32 development board with writable flash.

## CYD USB development profile

`runner-cyd-usb` uses the same signed bundle and flash store over UART0
at 115200 baud. A line of ASCII `RXSTATUS\n` requests a JSON status
response prefixed with `RX status `. `RXPUT <length>\n` requests an upload.
The host waits for `RX ready` before sending exactly that many binary bytes.
The runner has a bounded receive window and reports one `RX result ` JSON
receipt after verification and any trial. The client never automatically
resends an upload with an unknown outcome.

The `usb_trust` partition at `0x332000` holds eight ASCII bytes `RXTRST01`,
a 65-byte NUL-terminated signing key identifier, and a 65-byte uncompressed
P-256 public point. The image requires this record before accepting serial
commands. The trust policy is one public key for this development profile.

The signed USB profile reserves the signed name `peer.labels.v1` for
its LABELS peer. It uses the same trusted verification key as apps and
requires a zero grant mask. The peer store maintains its own durable
trial, active version and replay floor. The host validates the v1
command contract before promotion. The active peer is verified before
mounting after reboot.
