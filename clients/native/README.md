# Native controller

app.ae is the Aether UI entry point. The development iPhone bundle now has
native Keychain import and a bounded status transport adapter; it is signed
and installed, but the real TLS1.3-PSK connection is not yet proven. The UI has rendered on the unlocked phone, but the synthetic TLS test also
failed to complete there. The native TLS interoperability gap is recorded under asks/apple-external-psk-tls13.md. No real plug key was imported.

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

Next: finish the native TLS1.3-PSK proof using synthetic credentials, then
exercise the real device console.
Read ../../docs/REPOSITORY.md for the boundary and ../core/README.md for shared
logic. Working browser UI remains at ../web/.
