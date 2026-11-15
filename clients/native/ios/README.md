# iPhone platform integration

This directory will hold the application bundle/project and native adapters.
There is no installable iPhone app in this directory yet.

Reuse Aether UI's UIKit backend for app.ae. Use a thin Swift/Objective-C
boundary for:

- Network.framework TLS-PSK proof and later enrolled public-key authentication.
- Local-only Keychain storage and non-exportable key operations where supported.
- In-app QR scanning/display, avoiding Photos and clipboard flows.
- Local-network and camera permission descriptions, lifecycle and packaging.

The adapter supplies opaque handles and bounded operations to portable Aether.
It does not share secrets with UI state or Rexx. No iCloud synchronization,
analytics or automatic credential backup is part of this design. Explicit
recovery export/import is a separate owner-authorized operation.

Before device distribution, implement the actual transport and authorization,
prove a real session on the plug, and test enrollment/revocation. Do not present
this skeleton as functioning phone control or assume WebKit can do TLS-PSK.
