# Native controller

app.ae is the Aether UI entry point. The development iPhone bundle now has
local-only Keychain import and a pure-Aether TLS1.3 external-PSK provider.
The native adapter reaches the real S60 from the Mac. The updated app is
signed and installed; final phone-side validation needs an unlocked phone.
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

Next: finish the phone-side synthetic and real-device proofs, then add the
read-only Rexx console operation.
Read ../../docs/REPOSITORY.md for the boundary and ../core/README.md for shared
logic. Working browser UI remains at ../web/.
