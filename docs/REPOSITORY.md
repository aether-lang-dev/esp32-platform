# Firmware and controllers

This repository contains the device platform and its controllers. Both sides
share the command and enrollment contracts; their credentials and execution
lifecycles stay separate.

```
firmware/
  esp-idf/          flashable ESP-IDF application, native services and device tests
  ci/               container build/test entry point
clients/
  web/              working local Datastar controller and Python TLS-PSK adapter
  native/           Aether UI controller source and Apple platform integration
    ios/             iPhone-specific application packaging and platform adapters
  core/             home for portable controller logic as it is extracted
protocol/           device/controller contracts and enrollment design
scripts/            deployable Rexx examples and services
tools/              host-side signing, provisioning, flashing and synchronization
examples/           other platform examples
asks/               upstream implementation gaps
tests/              repository-level integration checks
```

`project`, `controller` and `in_container.sh` are relative compatibility symlinks.
Existing commands and external local deployment scripts continue to work.
New code should use the canonical paths above. `lane.sh` copies the real
firmware directory into its scratch project, not the compatibility symlink.
Its target selection and AETHER_REF pin are unchanged by this move.

The web controller remains operational. The native controller is a starting
point, not a connected iPhone app yet. No new enrollment, public-key
authentication or multi-session firmware support is claimed by this layout.

## Native client boundary

Use Aether UI for the interface, with portable Aether for device models,
command preparation and enrollment flow. The sibling aether-ui repository has
a UIKit backend and a simulator lane in ci.sh Phase1e2; reuse it instead of
building a second UI in Swift. Keep it an external dependency, not a vendored
copy. Its canonical local path is ../aether-ui (AETHER_UI_DIR can override it).

A small Swift/Objective-C adapter is appropriate for Network.framework TLS,
Keychain/Secure Enclave operations, camera QR scanning, local-network permission
and app lifecycle. Pass opaque credential handles and public metadata into
Aether; never pass private key bytes or PSKs into UI state, Rexx, WebKit,
logs or telemetry. A WebKit console can remain an optional client surface.

Public-key phone enrollment and device authentication are future protocol work.
Keep the working TLS-PSK owner/recovery path while proving the native transport.
Do not treat a public key as a PSK. The first phone milestone is real TLS-PSK
interoperability against QEMU, then the plug, before QR enrollment.

## Checks

Run tests under their canonical paths, e.g.:

```
python3 -m unittest discover -s clients/web -p test_device.py
python3 firmware/esp-idf/test_switch_driver.py
python3 tests/test_repository_layout.py
```

The old paths work during migration. Native UI checks and prerequisites are in
clients/native/README.md. Firmware lane usage remains in the root README.
