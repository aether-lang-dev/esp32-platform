# Copyright (C) 2026 Toit contributors.
# SPDX-License-Identifier: MIT
# See LICENSE in this directory.
# Adapted from Toit examples/rsa_gen.toit.
"""Generate a disposable RSA key and exercise export, signing and encryption."""
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory


def main():
    with TemporaryDirectory() as scratch:
        directory = Path(scratch)

        def openssl(*args, data=None):
            result = subprocess.run(
                ["openssl", *args], cwd=directory, input=data,
                capture_output=True, timeout=20,
            )
            result.check_returncode()
            return result.stdout

        openssl("genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", "private.pem")
        openssl("pkey", "-in", "private.pem", "-outform", "DER", "-out", "private.der")
        openssl("pkey", "-in", "private.pem", "-pubout", "-outform", "DER", "-out", "public.der")
        openssl("pkey", "-inform", "DER", "-in", "private.der", "-out", "reloaded.pem")
        openssl("pkey", "-pubin", "-inform", "DER", "-in", "public.der", "-out", "public.pem")
        print("Generated 2048-bit RSA key and reloaded both DER exports")

        message = b"Hello, Toit RSA!"
        (directory / "message").write_bytes(message)
        for key in ("private.pem", "reloaded.pem"):
            openssl("dgst", "-sha256", "-sign", key, "-out", "signature", "message")
            openssl("dgst", "-sha256", "-verify", "public.pem", "-signature", "signature", "message")
        print("Verified signatures with the exported public key")

        token = b"MySecretToken123"
        for options in (
            ("-pkeyopt", "rsa_padding_mode:pkcs1"),
            ("-pkeyopt", "rsa_padding_mode:oaep", "-pkeyopt", "rsa_oaep_md:sha256"),
        ):
            encrypted = openssl("pkeyutl", "-encrypt", "-pubin", "-inkey", "public.pem", *options, data=token)
            decrypted = openssl("pkeyutl", "-decrypt", "-inkey", "reloaded.pem", *options, data=encrypted)
            if decrypted != token:
                raise RuntimeError("RSA encryption round-trip failed")
        print("Verified PKCS#1 and OAEP-SHA256 encryption round-trips")


if __name__ == "__main__":
    main()
