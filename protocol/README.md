# Shared device/controller contracts

Working today: TLS1.3-PSK device transport; HTTP status, signed bundle delivery
and volatile REPL sessions. See ../firmware/esp-idf/REPL.md and ../SIGNING.md.
The Python host implementation remains in ../tools; the web client uses it
rather than maintaining a second wire parser.

The interface belongs here conceptually, not inside a UI toolkit or board
backend. Extract reusable implementation into clients/core when a second
client actually needs it. Keep secret storage and crypto handles in the
platform adapter. Do not copy current single-owner authorization assumptions
into a new multi-user client.

## Planned phone enrollment

1. The recipient phone creates a key pair; its private key stays local.
2. It displays a versioned public enrollment request as a QR, with its public
   key, supported algorithm and a human-readable phone label. No private key.
3. An authenticated owner explicitly approves the key and a permission set
   on a chosen plug. A QR scan alone grants no authority.
4. The plug stores that credential and permissions durably. The recipient
   authenticates directly without the owner's phone being online.
5. Revocation removes that credential, terminates its existing sessions and
   prevents replay/reuse. Recovery authority is separately backed up offline.

This is a design, not a wire format or implemented authentication path. Choose
and test the proof-of-possession protocol, device binding, freshness, key
encoding, size bounds and update/revocation behavior before freezing a schema.
The firmware currently has one PSK and one interactive session. Multiple
credentials, identity-aware grants and independently owned sessions remain
required work. Public keys are not used as TLS PSKs.
