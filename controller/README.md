# Local device controller

Run from the repository root with Aether 0.912.0 or later, the sibling
Datastar-Aether checkout, and Python 3.13 with TLS PSK support.

```sh
controller/run.sh https://device-address /absolute/path/device.json
```

Open http://127.0.0.1:8787. The server binds IPv4 loopback and accepts only
that Host header. Status requests go through the existing PSK transport.
The browser receives device status. Keep device credential files outside
the repository.

The first slice displays status. The device REPL protocol is the next slice.
Set `REXX_CONTROLLER_PYTHON` if the PSK-capable Python executable has a
different name or path from `python3.13`.

The locally served Datastar client is v1.0.3 from
https://cdn.jsdelivr.net/gh/starfederation/datastar@v1.0.3/bundles/datastar.js,
SHA-256 `5d6b7794a50a83d82da962aec5e382f5ae83ac7afbc751f903f7a9c6bd433c65`.
Its original MIT notice is in assets/DATASTAR-LICENSE.
