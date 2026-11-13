# PSK REPL sessions

The PSK firmware profiles expose a volatile REPL beside the installed
application. Possession of the TLS PSK authorizes its operations. A session
has a random identifier, fixed timer grants and increasing command sequence
numbers. The existing runner task executes both installed apps and REPL
commands, keeping RexxAE contexts on their owning task.

POST /repl/open takes an ASCII grant mask, currently 0 through 3.
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
