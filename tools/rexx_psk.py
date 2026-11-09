"""Device credential format shared by the watcher and flash provisioning."""
import argparse
import json
import os
from pathlib import Path
import re
import secrets
import ssl

IDENTITY = re.compile(r"[A-Za-z0-9_.-]{1,64}", re.ASCII)
KEY_HEX = re.compile(r"[a-fA-F0-9]{64}", re.ASCII)
MAGIC = b"RXPSK001"


def load(path):
    with path.open("rb") as file:
        raw = file.read(1025)
    try:
        data = json.loads(raw)
        identity = data["identity"]
        key = data["key"]
        if (len(raw) > 1024 or not IDENTITY.fullmatch(identity)
                or not KEY_HEX.fullmatch(key)):
            raise ValueError
        secret = bytes.fromhex(key)
        if secret in (bytes(32), b"\xff" * 32):
            raise ValueError
    except (ValueError, TypeError, KeyError):
        # Never print the content of a malformed credential.
        raise ValueError("invalid device credential file") from None
    return identity, secret


def client_context(identity, key):
    if not getattr(ssl, "HAS_PSK", False):
        raise ValueError("PSK requires Python 3.13 or newer with SSL PSK support")
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_3
    # Keep certificate verification enabled with an empty trust store.
    # A peer that tries certificate authentication must fail.
    context.set_psk_client_callback(lambda hint: (identity, key))
    return context


def write_new(path, data):
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as file:
        file.write(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("credential", type=Path)
    parser.add_argument("flash_record", type=Path)
    parser.add_argument("--generate", metavar="IDENTITY", help="create a fresh credential file")
    args = parser.parse_args()
    try:
        if args.credential.resolve() == args.flash_record.resolve():
            raise ValueError("credential and flash record must be different files")
        if args.generate is not None:
            if not IDENTITY.fullmatch(args.generate):
                raise ValueError("invalid device identity")
            write_new(args.credential, json.dumps(
                {"identity": args.generate, "key": secrets.token_hex(32)}).encode() + b"\n")
        identity, key = load(args.credential)
        write_new(args.flash_record, MAGIC + identity.encode("ascii").ljust(65, b"\0") + key)
    except (OSError, ValueError) as error:
        parser.exit(1, f"provision: {error}\n")


if __name__ == "__main__":
    main()
