# iPhone controller

The screen is Aether UI on UIKit. The native adapter owns local-only Keychain
storage, socket deadlines and the bridge to a pure-Aether TLS1.3 client. It no
longer relies on Network.framework for external PSK negotiation.

## Build and install

The external-PSK API is currently on a local Aether branch, not a released
stdlib. Set AETHER_TLS_DIR to that checkout; AETHER_TLS_REF identifies the
exact tested commit. The builder checks the commit and clean TLS modules.
It does not fetch or modify another checkout. AE/AETHERC must match its
runtime (tested with0.917.0). The separate firmware pin is unchanged.

```sh
AETHER_TLS_DIR=/path/to/tested/aether-checkout \
  IOS_DEVICE=<udid> IOS_NO_LAUNCH=1 python3 clients/native/ios/build.py
```

The builder uses aether-ui/tools/ios-app.sh in scratch, with exact-match
integration checks, native sources and a generated Aether transport library.
Build products stay in root build/native-ios; source-directory files and
credentials are never bundled. AETHER_UI_DIR and IOS_TEAM may select the
local UIKit toolkit and development team. Native TLS code is linked into the
app; no OpenSSL provider is required on the iPhone.

## Credential boundary

For this development proof, Xcode/devicectl stages a bounded JSON file in
Library/Caches/owner-credential.json. Fields: identity,64-character hex key,
host and optional numericport. The current proof uses an IPv4 address to
avoid unbounded DNS work. This is not QR/public-key enrollment.

Import validates the record and saves it as a non-synchronizing Keychain
generic password with WhenPasscodeSetThisDeviceOnly accessibility, removing
the staging file after successful storage. Caches is not an iCloud backup
source. Credential values remain behind the native bridge, never UI or Rexx
state. The Aether transport receives bounded secret buffers, copies and wipes
handshake key material; the adapter wipes its temporary decoded key too.

Only public result text crosses into Aether UI. A development diagnostic may
write it to Library/Caches/status-result.json. CONTROLLER_PROBE_AUTORUN runs
staged import/status only when explicitly set during a developer launch;
normal use is through the buttons. The initial phone fixture is synthetic;
real-owner import must follow successful synthetic validation.

## Authentication and bounds

The client requires TLS1.3 external PSK with ephemeral key exchange. It uses
RFC8446's external binder, not a resumption ticket. Unknown/wrong identities
or keys, a certificate-only server and TLS1.2 are refused; there is no weaker
fallback. The API generates fresh ephemeral entropy and handles P256 HRR.

The native request has a ten-second absolute deadline. A duplicate socket
reference permits cancellation without touching a reused descriptor. Requests
use GET/status or fixed REPL routes, with at most16records/16384bytes, a required bounded
Content-Length, no chunked framing and no redirects. Native JSON validation
checks the status shape before displaying it.

## Verification

```
clients/native/ios/tests/check.sh
AETHER_TLS_DIR=/path/to/tested/aether python3 clients/native/ios/tests/build_probe.py
python3.13 clients/native/ios/tests/test_pure_transport.py /tmp/controller-pure-psk-probe
/tmp/controller-pure-psk-probe /absolute/private/device.json device-ip
```

The Mac tests require PSK-capable Python3.13+ for independent synthetic peers.
They cover success, wrong keys, redirects, oversized/truncated/chunked
responses and a stalled peer. The actual S60 returned active6/floor6 through
the native/Aether adapter without changing its TLS configuration. Aether's
separate tests cover X25519, P256 retry, socket ownership, identity/key refusal,
certificate/TLS1.2 refusal and input bounds. A resumption-binder mutation makes
the independent handshake fail. Existing resumption/0RTT tests also pass.

On 2026-11-15 the signed app on the attached iPhone15 Pro negotiated TLS1.3
with the independent synthetic peer, then connected to the actual S60 and
reported app v6 / replay floor6. The owner credential was imported into the
local-only Keychain after the synthetic proof. No relay command was sent. See asks/apple-external-psk-tls13.md for the original
Apple-provider finding and current validation status. QR enrollment and
per-phone public-key authentication remain separate work.


## Rexx console (b011)

The Aether UI supplies multiline command entry, Run, Cancel and Reset session,
plus independent Show RC and Show RESULT switches. The switches append SAY
statements to the submitted source; they are not a debugger and do not run after
a syntax error or an early EXIT. Output history is bounded to32000UTF-16 units
and trimmed with a visible marker. Code input disables smart quotes, smart
dashes, autocapitalization and autocorrection; Run dismisses the keyboard.

ControllerConsole owns its own device REPL session and variables. It does not
replace, restart or change the installed signed app. The initial grant mask1027
permits timers and read-only ADDRESS APPS LIST/STATUS. SWITCH, OUTLET control,
APPS RUN and APPS CALL are denied. This console is not yet a relay remote.

Commands are capped at8192UTF-8 bytes including automatic RC/RESULT statements.
Native code constructs fixed routes and session/sequence headers; source goes
only in the length-delimited body. Command sequence increases across the session;
the event cursor resets for each command. A pending Run cannot be submitted twice.
Events are polled, checked against that command and decoded from hex. Malformed
or stale replies stop observation without replaying a command. A transport error
leaves the outcome uncertain: reset before submitting again.

Cancel requests cooperative cancellation through the existing host endpoint.
Reset closes the console session; the next Run starts with fresh variables.
Cancel and wait for a running command before Reset. The firmware has one REPL
session across controllers and expires it after120seconds without requests.
Closing the phone process loses its volatile session handle; let that session
expire before opening another controller. No automatic request retry or session
replacement is attempted. Credentials remain in the local-only Keychain.

### Console verification

```
clients/native/ios/tests/check.sh
AETHER_TLS_DIR=/path/to/tested/aether CONTROLLER_PROBE_SOURCE=console_probe.m \
  CONTROLLER_PROBE_OUTPUT=/tmp/controller-console-probe \
  python3 clients/native/ios/tests/build_probe.py
python3.13 clients/native/ios/tests/test_console_transport.py /tmp/controller-console-probe
python3.13 clients/native/ios/tests/test_console_transport.py /tmp/controller-console-probe bad-hex
python3.13 clients/native/ios/tests/test_console_transport.py /tmp/controller-console-probe stale-sequence
python3.13 clients/native/ios/tests/test_console_transport.py /tmp/controller-console-probe bad-schema
```

The strict cursor fixture was proven to fail the previous native client. Native
bounds tests check empty/NUL/oversized UTF-8 source and repeated Run taps. The
independent TLS peer checks routes, grants, sequence/cursor handling, RC/RESULT
switches, cancellation and reset. Negative peers must fail without command replay.
The existing seven TLS/framing/deadline tests also pass.

On2026-11-15, the Mac adapter against the real S60 proved retained variables
(x=41, then42), ADDRESS APPS main status(v6/floor6), streaming and cancellation,
relay denial(RC13), syntax-error recovery and variable clearing on reset. The
signed iPhone b011 separately returned42, the v6/floor6 ADDRESS APPS reply,
RC0 and RESULT, exit0. Firmware and signed app remained unchanged.

CONTROLLER_CONSOLE_PROBE=1 alongside CONTROLLER_PROBE_AUTORUN=1 enables the
explicit developer-only harmless console proof on launch. Normal launches use
the buttons. Public transcript diagnostics go to Caches/console-result.json;
no credential values or session handles are included.

Build b012 fixes the narrow console layout using UIKit content/frame layout
guides in the local aether-ui checkout. The scroll container fills the phone
viewport, history wraps to available width, and its content height drives
vertical scrolling. The UIKit render/geometry probe fails before the toolkit
fix and passes after it, including resize and replacement. The initial status
now invites a status check instead of claiming the credential is missing.
CONTROLLER_LAYOUT_PROBE=1 enables a public geometry report in Caches/console-layout.json
for physical-device verification; it does not contain credentials.
