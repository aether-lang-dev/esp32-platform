# Local device controller

Run from the repository root with Aether 0.912.0 or later, the sibling
Datastar-Aether checkout, and Python 3.13 with TLS PSK support.

```sh
controller/run.sh https://device-address /absolute/path/device.json
```

Open http://127.0.0.1:8787. The server binds IPv4 loopback and accepts only
that Host header. Status requests go through the existing PSK transport.
The browser receives device status. Keep device credential files outside
the repository.

The page includes device status and a multiline Rexx REPL. Open a session,
run commands, and use Cancel for an active command. Variables survive
successful commands. Close and reopen to reset them. The device currently
mounts UPTIME_MS, WAIT_MS, ADDRESS APPS and ADDRESS SWITCH for the interactive session.
The master session requests separate inventory, run, service-call and switch read/control grants.
On S60, `ADDRESS SWITCH 'ON 5'` requests five seconds of relay power after
the submitted program succeeds; OFF and STATUS are also available. Closing
the session or cancelling turns its output off.
For a signed app opting into the main v1 service contract, try:

```rexx
ADDRESS APPS 'CALL main status'
SAY RC
SAY RESULT
```

Service requests use fresh bounded contexts; see ../project/REPL.md.

The REPL firmware must be installed first. Its protocol and bounds are in
../project/REPL.md. The controller reads the PSK from the configured local
file. The browser receives the session identifier and output.

This development controller uses HTTP on loopback and has no user login.
Control requests require the controller's Host and Origin headers. It is
intended for the current single-operator functional testing. Remote access,
controller TLS and a user ACL remain future work.
Set `REXX_CONTROLLER_PYTHON` if the PSK-capable Python executable has a
different name or path from `python3.13`. Set `REXX_CONTROLLER_PORT`
to run another controller instance on a separate loopback port. Its Host
and Origin checks follow the selected port.

The opt-in integration check exercises a controller connected to a disposable
QEMU device:

```sh
python3.13 controller/test_http.py http://127.0.0.1:8788
```

It checks streamed output before completion, retained variables, concurrent
Cancel, continued use after cancellation, Host/Origin rejection and unchanged
installed-app metadata. It creates and closes its own REPL session. The same check passed against
the S60 controller on 2026-11-13 after installing the REPL firmware.

The locally served Datastar client is v1.0.3 from
https://cdn.jsdelivr.net/gh/starfederation/datastar@v1.0.3/bundles/datastar.js,
SHA-256 `5d6b7794a50a83d82da962aec5e382f5ae83ac7afbc751f903f7a9c6bd433c65`.
Its original MIT notice is in assets/DATASTAR-LICENSE.
