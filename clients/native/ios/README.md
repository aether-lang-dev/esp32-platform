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
replace, restart or change the installed signed app. The b011 initial grant mask1027 permitted timers and read-only ADDRESS APPS
LIST/STATUS. Build b018 adds SWITCH_READ and OUTLET_READ (mask9283) for named
status actions. Relay/countdown control, APPS RUN and APPS CALL remain denied. This console is not yet a relay remote.

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
expire before opening another controller. Commands are never automatically
retried and sessions are never silently replaced. Read-only idle refreshes may
retry as described below. Credentials remain in the local-only Keychain.

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

Build b013 starts UIKit before building gesture-bearing views. The prior
b011/b012 startup order raised a UIKit exception on physical taps inside scroll
containers, before any command callback. The local toolkit fix adds an entry
shim and constructs the Aether UI from didFinishLaunching. The iPhone returned42
and v6 app status, and Paul confirmed Check plug status and Run stay open and
update their results. Details: asks/ios-scroll-touch-startup.md at repo root.

A reusable real-touch test targets the already-installed app:

```
IOS_TEAM=your-team IOS_DEVICE=your-device \
  python3 clients/native/ios/tests/touch/run.py
```

The runner is bounded to300seconds and requires a fresh TOUCH_RESULT path if
re-running with an existing xcresult. Its initial run could not enable iOS
automation mode. The b014 retry on2026-11-15 passed on the physical iPhone:
Status and Run updated results,42 appeared, both switches toggled, and Cancel
and Reset kept the app in the foreground (one test, zero failures).


Build b014 follows new console output at the bottom. Scroll up to read earlier
history and incoming output preserves that position; pressing Run explicitly
resumes following. Native code checks the prior position before changing the
label, then resolves the new Auto Layout height before scrolling. It uses no
scroll animation and does not fight a drag or deceleration already in progress.
The near-bottom threshold is24points. The dedicated UIKit test covers append,
scrollback preservation, explicit follow, short output, insets and changing
Auto Layout content height:

```
sh clients/native/ios/tests/check_history.sh
```


## Everyday console (b017)

The foreground console sends an idle events read every20seconds to keep the
device's120second session lease alive while you read or type. Running commands
already refresh their lease through output polling. Only one idle read is in
flight; a newer command/reset invalidates its late callback. Transient idle
transport failures retain the handle and retry after5seconds, with a visible
reconnecting status. Confirmed expiry or unexpected session state requires an
explicit Reset; no command is replayed and no fresh session is silently opened.
This is foreground keepalive: iOS suspension or terminating the app can still
expire the lease. The failure reason remains in session status even if output
is cleared. Public diagnostics include that status without exposing handles.

Clear output changes only the transcript. Jump to latest returns to the bottom
and reveals the output area in the page. Run also reveals new output; manual
scrollback remains undisturbed by ordinary streamed updates. Previous/Next recall
the last20 raw commands from RAM, with an unsent draft restored at the newest
position. Consecutive duplicate commands are folded. Clear command empties the
editor; the code keyboard includes a Done control. None of these controls adds
relay or app-management grants, persists command history or changes the signed app.

Verification on2026-11-15:

- Native tests cover keepalive timing, duplicate idle reads, a stale-expiry race
  against a new command, transient read recovery, explicit expiry, failure
  visibility after Clear, recall and draft restoration. Disabling the heartbeat
  in a scratch mutation makes the regression test fail.
- UIKit tests cover following, preserving scrollback, explicit reveal when output
  is below the page fold, bounds/insets and changing Auto Layout content height.
- The independent TLS console lifecycle and all three malformed/stale negative
  cases pass against the rebuilt native adapter.
- The physical iPhone XCTest passed the complete everyday flow: typed answer=41,
  stayed idle125seconds, then returned42 from SAY answer+1; Clear, Jump,
  Previous/Next/draft restoration, cooperative cancellation, Reset and an unset
  ANSWER variable after reset all passed. No relay operation was permitted.

The real-device suite defaults to125seconds idle. Set CONSOLE_IDLE_SECONDS to a
smaller value for a quick UI smoke run; that does not prove lease retention past
the firmware's idle limit. The runner activates an already-running app when
possible, avoiding the intermittent cold-launch control connection failure seen
with this Xcode/iOS combination. Use a fresh TOUCH_RESULT output path per run.


## Named read-only actions (b018)

The Smart plug panel supplies App status, Relay status and Countdown status.
Each button maps an allowlisted identifier to fixed Rexx source. The fragments
ADDRESS APPS STATUS main, SWITCH STATUS or OUTLET STATUS, then emit a framed
RC/RESULT reply. No user source is interpolated and no new HTTP endpoint or
device firmware is added. The existing REPL session's host grant mask9283 consists
only of timers, APPS_READ, SWITCH_READ and OUTLET_READ. Typed console commands
also gain those two read capabilities; control remains denied by the host.

A named invocation captures only its own output, bounded to16384UTF-16 units,
across event polls. Source echoed in history, earlier output and later commands
are excluded. Missing/duplicate markers, nonzero RC, invalid typed status data,
lost output, runtime failure or cancellation cannot produce a successful panel
result. Each status has its own check time; unavailable replaces that action's
old value rather than leaving a stale success. Actions are manual snapshots,
not a live relay/countdown monitor. Busy requests are rejected without queueing.

The generated Rexx is visible in the console transcript, while Previous/Next
remain a history of typed commands. No named action starts the relay, changes
the countdown, runs/replaces a managed app or expands a peer's own grants.

Validation completed locally: native allowlist/framing/schema/capture tests;
exact mask9283 checked against firmware read/control permission functions with
no effects; an independent TLS peer passes all three actions and fragmented
replies, with four negative cases (denied read, malformed result, output loss,
runtime failure). The updated ordinary console lifecycle regression also runs.
The new physical-phone test compiles and signs in build-only mode.

Live proof completed2026-11-16 after both devices were reconnected. b018 is
installed on the iPhone. The real S60 reported app s60-bench v6/floor6, Relay OFF
and Countdown idle/0s; all five control commands in the native probe were denied
RC13. The focused physical-phone XCTest passed App status, Relay status and
Countdown status taps, editor/recall preservation and session reset. Public phone
results match the native probe. No firmware reflash or relay actuation was needed.
Earlier preparation was blocked by disconnected devices; that blocker is cleared.

```
clients/native/ios/tests/check.sh
AETHER_TLS_DIR=/path/to/tested/aether CONTROLLER_PROBE_SOURCE=actions_probe.m \
  CONTROLLER_PROBE_OUTPUT=/tmp/controller-actions-probe \
  python3 clients/native/ios/tests/build_probe.py
python3.13 clients/native/ios/tests/test_actions_transport.py /tmp/controller-actions-probe
python3.13 clients/native/ios/tests/test_actions_transport.py /tmp/controller-actions-probe denied-read
python3.13 clients/native/ios/tests/test_actions_transport.py /tmp/controller-actions-probe bad-result
python3.13 clients/native/ios/tests/test_actions_transport.py /tmp/controller-actions-probe lost-output
python3.13 clients/native/ios/tests/test_actions_transport.py /tmp/controller-actions-probe runtime-error
```

With the bench S60 reachable, the native actions probe also checks five denied
control commands under that mask and closes its session. With the phone ready,
use TOUCH_ONLY=TouchTests/ConsoleTouchTests/testNamedActions to run just the named
panel test. TOUCH_BUILD_ONLY=1 compiles/signs the suite without a connected phone;
that is a build check, not an interaction pass. All runs need IOS_TEAM and a
fresh TOUCH_RESULT; real runs also need IOS_DEVICE.


## Timed outlet controls (b019)

Adds On 10 seconds, On 30 seconds and Off, with the configured device endpoint
shown above the buttons. These are fixed named Rexx fragments addressing OUTLET
START 10, START 30 and CANCEL. There is no indefinite On or interpolated duration.
The host's existing timer owns the deadline; closing the console/phone does not
cancel the timer or prevent automatic Off. No firmware reflash is needed.

The owner REPL session now requests mask 25667: the b018 status/timer reads plus
OUTLET_CONTROL. Typed console code can also use OUTLET START 1..60 and CANCEL.
Direct SWITCH writes, APPS RUN/CALL and unrelated host capabilities remain denied.
Off cancels the OUTLET-owned countdown; another service's output ownership is
not bypassed. If the console is busy, the action is rejected; cancel/wait for the
running command and press Off again. This is not a separate emergency channel.

Replies must confirm a valid outlet state within the selected preset's bound.
Off requires idle / 0 seconds and no competing owner; it never reports success for
an active timer or busy output. An On acknowledgement is never automatically
retried if its response is lost. Results are labelled Last command with a check
time, rather than presenting old acknowledgements as live relay state. A control
attempt invalidates prior relay/countdown snapshots; use their status buttons to
refresh them. Commands are not broadcast or queued for another device.

Verification completed2026-11-16 with the user-confirmed empty S60 outlet:

- Native unit tests cover preset allowlisting, bounded acknowledgements, Off
  validation and ownership conflicts. Exact firmware permission functions confirm
  OUTLET control is allowed while direct relay/app control remains denied.
- An independent TLS peer passes timed controls and expiry after session close;
  a lost On reply produces failure with exactly one On submission.
- The real S60 passed Off, On 10 seconds, Relay ON, session close, automatic Relay
  OFF/countdown idle after 12 seconds, On 30 seconds, explicit Off and final OFF/idle.
  Direct relay control was denied RC13. The test session was closed at the end.
- Existing named status/error regressions pass. b019 and its timed-control XCTest
  compile and sign. Phone installation/taps are pending because USB disconnected;
  the currently installed phone build remains b018.

Live control tests are opt-in so ordinary UI regression runs cannot energize an
outlet. After confirming the outlet is empty:

```
ALLOW_LIVE_OUTLET_CONTROL=1 /tmp/controller-outlet-control-probe /private/device.json device-ip
IOS_TEAM=your-team IOS_DEVICE=your-device ALLOW_LIVE_OUTLET_CONTROL=1 \
  TOUCH_ONLY=TouchTests/ConsoleTouchTests/testTimedOutletControls \
  TOUCH_RESULT=/tmp/fresh-timed-control.xcresult \
  python3 clients/native/ios/tests/touch/run.py
```

Build the native probe with CONTROLLER_PROBE_SOURCE=outlet_control_probe.m and
CONTROLLER_PROBE_OUTPUT=/tmp/controller-outlet-control-probe using build_probe.py.
The independent peer tests are test_outlet_control_transport.py(probe), with
an optional drop-on-reply argument for the no-retry negative case. They use only
synthetic credentials and do not connect to a real outlet.
