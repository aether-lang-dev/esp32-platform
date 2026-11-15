# PSK REPL sessions

The PSK firmware profiles expose a volatile REPL beside the installed
application. Possession of the TLS PSK authorizes its operations. A session
has a random identifier, fixed host-service grants and increasing command sequence
numbers. The existing runner task executes both installed apps and REPL
commands, keeping RexxAE contexts on their owning task.

POST /repl/open takes an ASCII grant mask, using timer bits 1 and 2, APPS inspection bit 1024 and APPS run bit 2048 and APPS call bit 4096, SWITCH read bit 64 and SWITCH control bit 128.
The local master controller requests all service bits (7363).
The response contains a session identifier. Subsequent operations supply
X-Repl-Session.

POST /repl/command takes UTF-8 Rexx source and X-Repl-Sequence, starting
at 1. An admitted sequence cannot be repeated. Source is limited to
8192 bytes. GET /repl/events supplies X-Repl-After, initially 0 for each
command. Its cursor advances through a bounded event ring. Event text is
hex encoded so quotes and newlines cannot alter the JSON structure.
A lost flag reports overwritten output.

POST /repl/cancel cancels the current or pending command. POST /repl/close
cancels work and releases the session on the runner task. Idle sessions
expire after two minutes. Each command has the runner's five-second,
instruction and memory budgets. The session's retained state and transient
copies share a 64 KiB parent account, leaving room for TLS.

Successful commands preserve variables through RexxAE's create_next ABI.
Failed commands discard their variable changes. Resource-limit failures
reset session variables and emit an explicit message. Routine definitions
are scoped to the submitted source. Opening a fresh session resets variables.

REPL requests do not write script slots, NVS release metadata or replay
floors. The signed application update path continues to use its own slots
and lifecycle.

project/test_repl_delivery.py runs through the real PSK TLS interface in
the signed QEMU lane. It checks variable retention, errors, grant denial,
streamed output, cancellation, repeated contexts, Unicode chunk boundaries,
output overrun, memory-limit recovery, fresh sessions and unchanged app
state/storage.

Verified on the physical Sonoff S60TPG on 2026-11-13, including the local
Datastar controller, cancellation, variable retention, Unicode output and
memory-limit recovery. Installed release v4 and its replay floor survived
the firmware update and remained unchanged through the REPL checks. The
diagnostic pauses between polls, matching the controller's Wi-Fi cadence.

## Managed apps through ADDRESS

`ADDRESS APPS 'LIST'` returns the managed-app inventory in RESULT.
`ADDRESS APPS 'STATUS main'` returns its installed name, versions, slot,
runner state and request identifier. Both require grant 1024.

`ADDRESS APPS 'RUN main'` requires grant 2048 and returns a queued request
identifier. It runs after the current REPL command, on the normal app task
with the installed app's own grants and budget. It preserves slot contents
and replay metadata. RC 8 means busy, 13 denied, 4 invalid and 20 a store or
queue fault. Inspect RC and RESULT with SAY after the ADDRESS command.

The current inventory contains one logical app, main; its two A/B slots
are release history. Stop and replacement are not APPS commands yet. These grants belong to the interactive session, not signed manifests.

### Signed request services

`ADDRESS APPS 'CALL main echo hello'` requires the separate call grant 4096.
RESULT is the service reply and RC its exit code. The signed source must start
with the exact line `/* ADDRESS SERVICE main v1 */` followed by a newline.
See scripts/examples/s60_service.rexx. This marker is signed along with the
source; ordinary installed programs are rejected rather than executed as services.

The host verifies and pins the installed bundle while holding its app-operation
reservation. Each call uses a fresh Rexx context, seeded with ADDRESS_COMMAND.
One SAY supplies the reply (at most 128 bytes), then EXIT supplies RC. No host
functions or ADDRESS routes are mounted inside this context. The existing peer
step bound and caller's deadline, cancellation and memory account apply. Calls
do not share app variables or alter its installed lifecycle status. This is a
stateless request service, not a resident app actor. Callbacks and nested calls
are denied. A busy app-operation reservation returns RC 8; missing opt-in or
invalid requests return 4; execution or storage failure returns 20.

The service receives no hardware authority. Future services needing peripherals
must have their own explicit host contracts and grants.

### Session-owned S60 relay

On S60 firmware, ADDRESS SWITCH controls active-high GPIO4 through the host.
The signed app and its stateless peer contexts have no switch grants. In a
master REPL session:

```rexx
ADDRESS SWITCH 'ON 5'
SAY RC
SAY RESULT
```

ON requires a duration of 1 through 60 seconds. OFF turns it off, and STATUS
reports ON or OFF. Read grant64 and control grant128 are independent; a
control-only session cannot read STATUS. RC13 means denied, RC4 invalid,
and RC8 means another owner holds the output or it is unavailable.

The command stages its requested state and applies it only when the submitted
Rexx program completes successfully. A STATUS in the same submission sees its
staged state; STATUS in a later submission sees the host's current state.
The independent ESP timer enforces expiry every 50ms. It does not depend on a
Rexx WAIT or a browser staying connected. Expiry releases ownership. An owner
command failing, cancellation (including idle cancellation), session close or
session expiry turns the output off and releases ownership. A new boot starts
off. Closing another owner cannot change the output. Signed app updates and
ordinary peer calls do not gain the session's hardware authority.

The signed QEMU profile uses the same driver lifecycle with simulated output.
To include relay integration tests, run REXX_TEST_SWITCH=1 ./lane.sh
runner-signed-psk with the usual target and engine settings. For physical runs,
the outlet must be empty before setting REXX_TEST_SWITCH=1. Tests include
separate grants, invalid timeouts, independent expiry, failed staged commands,
owner failure, active/idle cancellation, close, and unchanged app metadata.

### Native timed-outlet application

`ADDRESS OUTLET 'START 10'` starts a host-owned countdown for 1–60 seconds.
`STATUS` returns JSON with app, state (running/idle), remaining seconds rounded
up, and busy (a different owner holds the relay). `CANCEL` stops this app's
countdown. The countdown and relay lease belong to the native application:
closing or failing the calling Rexx session does not stop it. Expiry or an
explicit cancellation stops it. These native app commands act immediately;
they are RPC effects, unlike the session-staged SWITCH interface.

OUTLET read8192, start16384 and cancel32768 are separate grants. The local
master requests all of them (total grant mask64707). RC13 is denied, RC4
invalid, RC8 competing owner and RC20 unavailable/fault. OUTLET cannot steal
a SWITCH session's lease, and SWITCH cannot steal OUTLET's active lease.
Cancelling OUTLET cannot turn off another owner's output.

On the S60, a button press sampled for at least200ms but less than5seconds
toggles this application: start its default10seconds, or cancel its active
countdown. A competing owner makes this toggle do nothing. A long hold still
returns to retained Safeboot after release and never starts a countdown.
Boot starts off; there is no persistent automatic start setting.

Signed apps can receive OUTLET_READ and OUTLET_CANCEL in their manifest.
They cannot receive OUTLET_CONTROL/start on this profile. An APPS peer call
gets only its own signed grants, masked by the host to outlet read/cancel;
it never inherits the calling REPL's grants. The example service exposes:

```rexx
ADDRESS APPS 'CALL main outlet STATUS'
SAY RESULT
ADDRESS APPS 'CALL main outlet CANCEL'
SAY RC
```

The source context is still stateless; all callers address the same native
application state. The native application owns GPIO and timer through the
base driver. RexxAE itself remains headless and platform-independent.

Unit tests cover button bounce/short/long release, native ownership, expiry
and separate grants. Set both REXX_TEST_OUTLET=1 and REXX_TEST_SWITCH=1 for
QEMU integration. Physical tests require an empty outlet. They exercise
caller close/failure without stopping the native app, relay contention,
countdown expiry, and signed peer status/cancel with denied start.
