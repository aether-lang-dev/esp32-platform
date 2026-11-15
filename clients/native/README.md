# Native controller

app.ae is the Aether UI entry point. It is an unconnected UI shell: it neither
controls devices nor stores keys yet. Prefer this interface for the iPhone app
rather than rewriting the UI in Swift. The sibling aether-ui project already
has a UIKit backend and simulator compile/link/run coverage in ci.sh Phase1e2.

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

Next: prove native TLS-PSK with the device's status operation using synthetic
QEMU credentials, then add local-only key storage and the real device console.
Read ../../docs/REPOSITORY.md for the boundary and ../core/README.md for shared
logic. Working browser UI remains at ../web/.
