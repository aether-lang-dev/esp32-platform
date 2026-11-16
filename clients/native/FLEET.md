# Native fleet controller

Requested2026-11-15: a second ESP32 smart plug is on order. The phone app must
manage independently enrolled devices; b018 still targets one S60. Fleet support
is the next implementation step, not a capability already present in b018.

## First increment

- A device list with friendly names and stable local device IDs. An IP address
  is a connection setting, not the device's identity.
- Each entry has its own PSK identity/key, endpoint and local-only Keychain item.
  Keep WhenPasscodeSetThisDeviceOnly and non-synchronizing storage. No shared
  fleet PSK, cloud sync or new signing authority.
- Migrate the existing single-owner credential into the first fleet entry.
  Importing another device adds an entry rather than replacing the current one.
- Selecting an entry opens that device's named actions and Rexx console. Show
  the selected device name above every command/action group.
- Separate status timestamps, results, transcript, editor draft, command recall,
  session handle, command/event counters, keepalive and error state per device.
  Preserve each context while switching between devices in the foreground.
- Preserve explicit per-device timed OUTLET controls added in b019. Do not add
  bulk switching as part of the list/credential refactor. Bind every control
  group to its displayed target and keep the existing firmware/grant checks.

## Ownership boundary

Replace the native platform's single global console/credential/action state with
a device-context map and an explicit selected context. Secret credentials remain
inside the native provider; Aether UI receives IDs, friendly names and public
state. Bind each asynchronous request/completion to its originating context,
never to whichever device happens to be selected when the reply arrives.

Switching devices does not cancel or transfer a command. Cancel and Reset apply
to the selected device's own session. Maintain foreground keepalive for open
contexts; device failure must not prevent the other device's work. App suspension
can still expire sessions and must remain visible on return.

## Acceptance before the second plug arrives

Use two independent synthetic TLS peers with different keys/endpoints. Verify:

1. A credential for device A cannot authenticate to B.
2. Delayed A replies cannot overwrite B's selected panel or transcript.
3. Variables, drafts, recall, Clear, actions, cancellation and reset remain local
   to their device; switching during a pending action preserves its recipient.
4. An offline/expired A leaves B usable; closing A does not close B.
5. Legacy single-device enrollment migrates without printing or syncing a key.
6. Phone automation selects both fixtures and checks their distinct public
   results. Label synthetic peers explicitly in development-only fixtures.

Then enroll the second physical plug with its own credential and repeat the
cross-device checks. No second plug or real credential is needed for the
isolation tests above.
