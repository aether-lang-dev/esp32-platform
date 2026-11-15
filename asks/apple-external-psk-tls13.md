# Native Apple external PSK interoperability

2026-11-15. This is an observed integration gap, not proof that all Apple
platforms lack external TLS1.3-PSK support.

The plug uses TLS1.3 external PSK with ephemeral key exchange. Python3.13's
PSK client reaches it successfully. The new native Network.framework adapter
in clients/native/ios/ControllerTransport.m currently fails against both the
plug and an independent Python/OpenSSL synthetic TLS1.3-PSK server on macOS.
The state is waiting, SSL domain3/code-9858 (handshake failed), then our bounded
timeout. The same synthetic setup succeeds with a scratch TLS1.2 variant of
both endpoints. The repository client and device remain TLS1.3-only.

Apple's official "Configuring a Wi-Fi accessory to join a network" sample
sets minimumTLS1.2, appends PSK-AES128-GCM-SHA256 and supplies key/identity via
sec_protocol_options_add_pre_shared_key. We inspected it as a reference; no
sample implementation was copied. Configuring that legacy cipher alongside
TLS1.3 AES128 while retaining min=maxTLS1.3 did not fix the observed failure.
A certificate-only synthetic TLS1.3 server is rejected; no accept-all trust
callback was introduced. The certificate verify callback explicitly rejects.

The attached iPhone15Pro has a signed Aether UI build installed. Its actual
TLS test ran after unlocking. User confirmed the Aether UI screen and both
controls were visible. The synthetic LAN server accepted a connection, then
reported unexpected EOF during TLS. The phone's public report eventually
showed waiting/ECONNREFUSED after the one-shot server had exited. That final
POSIX code is secondary, not the initial TLS error. Only a synthetic
credential was imported to local-only Keychain; the real plug key has not
been transferred. The configured strict TLS1.3 path is not interoperable on
either tested runtime; this does not establish that every Apple configuration
or OS release has the same limitation.

If this persists, prefer a tested TLS1.3 external-PSK provider rather than
weakening firmware TLS. Aether's pure-Aether std.cryptography.tls13_client has
connect_resume PSK support but no public external-PSK connection entry point in
the inspected checkout. A potential upstream addition needs correct external
binder labels, fail-closed server rejection, interoperability and wrong-key
negative tests. Do not fake an external key as a resumption ticket.

References:
- https://developer.apple.com/documentation/security/sec_protocol_options_add_pre_shared_key(_:_:_:)
- https://developer.apple.com/documentation/networkextension/configuring-a-wi-fi-accessory-to-join-a-network
