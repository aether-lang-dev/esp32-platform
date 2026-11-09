"""Pack a signed Rexx runner bundle using an off-device P-256 PEM private key."""
import argparse
import hashlib
import re
import struct
import subprocess
from pathlib import Path

MAGIC = b"RXAEUP01"
HEADER = struct.Struct("<8sIIHHI")
GRANTS = {"UPTIME_MS": 1, "WAIT_MS": 2, "TOUCH_EVENT": 8, "DISPLAY_BUTTONS": 16, "ADDRESS_LABELS": 32,
          "SWITCH_READ": 64, "SWITCH_CONTROL": 128,
          "SETTINGS_READ": 256, "SETTINGS_WRITE": 512}
IDENTIFIER = re.compile(r"[A-Za-z0-9_.-]{1,64}\Z", re.ASCII)
P256_SPKI_PREFIX = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d030107034200")

def openssl(*args, data=None):
    return subprocess.run(["openssl", *map(str, args)], input=data, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=True).stdout

def public_point(private_key):
    public = openssl("pkey", "-in", private_key, "-pubout", "-outform", "DER")
    if len(public) != len(P256_SPKI_PREFIX) + 65 or not public.startswith(P256_SPKI_PREFIX + b"\x04"):
        raise ValueError("signing key must be P-256")
    return public[-65:]

def raw_signature(der):
    # OpenSSL emits DER ECDSA signatures; the wire format uses fixed r || s.
    if len(der) < 6 or der[0] != 0x30 or der[1] != len(der) - 2:
        raise ValueError("unexpected ECDSA signature")
    cursor, parts = 2, []
    for _ in range(2):
        if cursor + 2 > len(der) or der[cursor] != 2:
            raise ValueError("invalid signature integer")
        length = der[cursor + 1]
        start = cursor + 2
        integer = der[start:start + length]
        if not 1 <= length <= 33 or len(integer) != length or integer[0] & 128:
            raise ValueError("invalid signature integer")
        value = int.from_bytes(integer, "big")
        if not 0 < value < 1 << 256:
            raise ValueError("signature integer out of range")
        parts.append(value.to_bytes(32, "big"))
        cursor = start + length
    if cursor != len(der):
        raise ValueError("trailing signature data")
    return b"".join(parts)

def unsigned_message(source, *, name, version, key_id, grants):
    if not IDENTIFIER.fullmatch(name) or not IDENTIFIER.fullmatch(key_id):
        raise ValueError("name and key id must be 1..64 ASCII identifier characters")
    if not 0 < version <= 0xffffffff:
        raise ValueError("version must be 1..4294967295")
    if not 0 < len(source) <= 8192 or b"\0" in source:
        raise ValueError("source must be 1..8192 bytes without NUL")
    mask = 0
    for grant in grants:
        if grant not in GRANTS:
            raise ValueError(f"unknown grant: {grant}")
        if mask & GRANTS[grant]:
            raise ValueError(f"duplicate grant: {grant}")
        mask |= GRANTS[grant]
    key, label = key_id.encode("ascii"), name.encode("ascii")
    return HEADER.pack(MAGIC, version, mask, len(key), len(label), len(source)) + key + label + source


def fingerprint(source, *, name, key_id, grants):
    message = unsigned_message(source, name=name, version=1, key_id=key_id, grants=grants)
    return hashlib.sha256(message[:8] + message[12:]).hexdigest()


def pack(source, *, name, version, key_id, grants, private_key):
    message = unsigned_message(source, name=name, version=version, key_id=key_id, grants=grants)
    public_point(private_key)  # Reject a different curve before signing.
    signature = openssl("dgst", "-sha256", "-sign", private_key, data=message)
    return message + raw_signature(signature)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", type=int, required=True)
    parser.add_argument("--grant", action="append", choices=sorted(GRANTS), default=[])
    args = parser.parse_args()
    try:
        if args.output.resolve() in {args.source.resolve(), args.private_key.resolve()}:
            raise ValueError("output must not overwrite source or private key")
        blob = pack(args.source.read_bytes(), name=args.name, version=args.version,
                    key_id=args.key_id, grants=args.grant, private_key=args.private_key)
        args.output.write_bytes(blob)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"bundle: {error}\n")

if __name__ == "__main__":
    main()
