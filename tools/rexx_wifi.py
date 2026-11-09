"""Create the private CYD Wi-Fi and public signing trust flash record."""
import argparse
import json
from pathlib import Path
import subprocess

from rexx_bundle import IDENTIFIER, P256_SPKI_PREFIX, openssl
from rexx_psk import write_new


MAGIC = b"RXWIFI01"


def encode(ssid, password, key_id, public_der):
    if not isinstance(ssid, str) or not isinstance(password, str):
        raise ValueError("Wi-Fi credentials must be strings")
    name = ssid.encode("utf-8")
    secret = password.encode("utf-8")
    if not 1 <= len(name) <= 32 or b"\0" in name:
        raise ValueError("SSID must be 1..32 bytes without NUL")
    if not 8 <= len(secret) <= 63 or any(byte < 32 or byte > 126 for byte in secret):
        raise ValueError("password must be 8..63 printable ASCII characters")
    if not IDENTIFIER.fullmatch(key_id):
        raise ValueError("invalid signing key identifier")
    if len(public_der) != len(P256_SPKI_PREFIX) + 65 or not public_der.startswith(P256_SPKI_PREFIX + b"\x04"):
        raise ValueError("signing public key must be P-256")
    return (MAGIC + bytes((len(name), len(secret))) + name.ljust(32, b"\0")
            + secret.ljust(63, b"\0") + key_id.encode("ascii").ljust(65, b"\0") + public_der[-65:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("credentials", type=Path, help="private JSON file with ssid and password")
    parser.add_argument("public_key", type=Path, help="fleet P-256 public PEM key")
    parser.add_argument("key_id")
    parser.add_argument("record", type=Path)
    args = parser.parse_args()
    try:
        with args.credentials.open("rb") as file:
            raw = file.read(1025)
        if len(raw) > 1024:
            raise ValueError("Wi-Fi credential file is too large")
        try:
            credentials = json.loads(raw)
            ssid, password = credentials["ssid"], credentials["password"]
        except (ValueError, KeyError, TypeError):
            raise ValueError("invalid Wi-Fi credential file") from None
        public_der = openssl("pkey", "-pubin", "-in", args.public_key, "-pubout", "-outform", "DER")
        write_new(args.record, encode(ssid, password, args.key_id, public_der))
    except subprocess.CalledProcessError:
        parser.exit(1, "provision: could not read the signing public key\n")
    except UnicodeError:
        parser.exit(1, "provision: invalid credential encoding\n")
    except (ValueError, OSError) as error:
        parser.exit(1, f"provision: {error}\n")


if __name__ == "__main__":
    main()
