# Native controller

app.ae is the Aether UI entry point. The development iPhone bundle now has
local-only Keychain import and a pure-Aether TLS1.3 external-PSK provider.
The native adapter reaches the real S60 from both Mac and iPhone. The signed
iPhone app passed a synthetic TLS1.3 peer and reports app v6 / replay floor6
from the plug using its existing PSK configuration.
See ios/README.md for the exact local Aether pin, tests and credential boundary.

Check the portable source against that dependency:

```
clients/native/check.sh
```

This emits portable C in scratch storage, exercising module resolution and
code generation without opening a window or claiming an installable iPhone app.
Use AETHER_UI_DIR for a different toolkit checkout and AE for the CLI path.
Verified with Aether0.917.0. The trusted toolkit host needs fs/os/extern build
capabilities; these are unrelated to the smart plug's Rexx permissions.

The desktop backend can provide rapid UI iteration; UIKit packaging belongs
under ios/. Use the toolkit's existing backend ABI and build conventions.
Do not vendor or fork the toolkit just to package this app. The firmware
AETHER_REF does not automatically select a compatible native UI toolchain.

Build b011 adds a separately owned Rexx console: command entry, bounded output
history, RC/RESULT switches, Cancel and Reset session. Its grant mask1027 permits
timers and ADDRESS APPS LIST/STATUS, without relay or app-management authority.
The native lifecycle probe passed against the actual S60; the iPhone returned42
and the v6 ADDRESS APPS status over TLS. See ios/README.md for tests and limits.
Read ../../docs/REPOSITORY.md for the boundary and ../core/README.md for shared
logic. Working browser UI remains at ../web/.

Current phone build b017 adds foreground session keepalive/status, Clear output,
Jump to latest, Previous/Next command recall with draft restoration, and a Done
control on the code keyboard. The full physical-phone XCTest passed, including
retaining a variable through125seconds idle, typing, cancellation and reset.

Installed b018 adds a Smart plug panel with named App status, Relay status and
Countdown status actions through Rexx ADDRESS. Independent TLS, firmware
permission checks, live S60 validation and the physical-phone tap suite pass.
Control stays denied. Fleet support is the next increment; see FLEET.md for
per-device ownership and isolation acceptance checks.
