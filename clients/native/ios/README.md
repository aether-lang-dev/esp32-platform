# iPhone development app

The interface is Aether UI using the existing UIKit backend. Objective-C
adapters own Network.framework and local-only Keychain access. No Swift UI
rewrite or vendored toolkit is used.

Build/sign/install on a paired development phone:

```sh
IOS_DEVICE=<udid> IOS_NO_LAUNCH=1 python3 clients/native/ios/build.py
```

AE, AETHERC, AETHER_UI_DIR and IOS_TEAM may select the local toolchain/team.
The wrapper uses the upstream tools/ios-app.sh in scratch, with exact-match
integration checks, native sources and Network/Security frameworks. Build
products stay in root build/native-ios. App-directory files are not bundled
as resources. No credential belongs in the app bundle or source tree.

A real iPhone15Pro build was signed and installed on2026-11-15. Launch was
refused while the phone was locked; unlock it to continue. This is not yet a
working connection proof to the plug: strict TLS1.3 external PSK currently
fails on the macOS native probe. See asks/apple-external-psk-tls13.md. Do not
infer a successful connection from successful app installation.

## Credential boundary

For the development proof only, Xcode/devicectl may stage a bounded JSON file
in the app's Library/Caches/owner-credential.json. Fields are identity, a
64-character hex key, host and optional numericport. This is not QR enrollment.
The Import button validates it, stores it as a non-synchronizing generic
password with WhenPasscodeSetThisDeviceOnly accessibility, then removes the
staging file after successful storage. Caches is not an iCloud backup source.
No real device credential has been imported in the initial test; only a
synthetic fixture has been staged.

Private credential values stay in the native adapter, never in Aether UI
state. Only public result text crosses the bridge. A developer diagnostic may
write public status to Library/Caches/status-result.json for Xcode inspection.
CONTROLLER_PROBE_AUTORUN triggers staged import/status only when explicitly
set for a development launch. Default app usage is through the buttons.

## Transport checks

```
clients/native/ios/tests/check.sh
clang -fobjc-arc -fblocks -Iclients/native/ios \
  clients/native/ios/ControllerTransport.m \
  clients/native/ios/tests/transport_probe.m \
  -framework Foundation -framework Network -framework Security -o /tmp/controller-probe
/tmp/controller-probe /absolute/private/device.json device-hostname
```

These require macOS. The probe accepts an optional final port for synthetic
loopback tests. A TLS1.2 diagnostic used scratch-only sources, not the shipped
client or firmware. The client requires TLS1.3, bounds receive data and time,
rejects certificate verification, and does not follow redirects.

Next: unlock and prove this on the phone; resolve the TLS provider if the
failure reproduces. Then add read-only ADDRESS through the device REPL. QR
phone enrollment, per-phone public-key authentication and revocation remain
future work. No cloud account, automatic key sync or implicit recovery export
is implemented.
