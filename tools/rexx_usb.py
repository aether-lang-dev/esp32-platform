"""Provision and update the signed Rexx runner over USB serial."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

from rexx_bundle import IDENTIFIER, P256_SPKI_PREFIX, GRANTS, openssl, pack
from rexx_psk import write_new

LABELS_BUNDLE = "peer.labels.v1"


def trust_record(key_id, public_der):
    if not IDENTIFIER.fullmatch(key_id):
        raise ValueError("invalid signing key identifier")
    if (len(public_der) != len(P256_SPKI_PREFIX) + 65
            or not public_der.startswith(P256_SPKI_PREFIX + b"\x04")):
        raise ValueError("signing public key must be P-256")
    return b"RXTRST01" + key_id.encode("ascii").ljust(65, b"\0") + public_der[-65:]


def open_port(name):
    import serial
    port = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=3)
    port.port = name
    port.dtr = port.rts = False
    port.open()
    return port


class UsbRunner:
    def __init__(self, port):
        self.port = port
        self.partial = bytearray()

    def frame(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            part = self.port.read_until(b"\n", size=1024)
            self.partial.extend(part)
            if len(self.partial) > 16384:
                self.partial.clear()
                raise ValueError("USB runner line exceeds 16 KiB")
            if not part.endswith(b"\n"):
                continue
            line = bytes(self.partial).rstrip(b"\r\n")
            self.partial.clear()
            if line in (b"RX ready", b"RX peer-ready"):
                return line[3:].decode("ascii"), None
            if line.startswith((b"RX status ", b"RX result ", b"RX peer-status ", b"RX peer-result ")):
                _, kind, raw = line.split(b" ", 2)
                payload = json.loads(raw)
                if not isinstance(payload, dict):
                    raise ValueError("invalid USB runner receipt")
                return kind.decode("ascii"), payload
            if line:
                print(line.decode("utf-8", errors="replace"), file=sys.stderr)
        raise TimeoutError("USB runner did not respond")

    def status(self, *, peer=False):
        # Opening some USB bridges resets the board. Read-only status can be
        # retried while the runner boots, before attempting an upload.
        for _ in range(10):
            self.port.write(b"RXPEERSTATUS\n" if peer else b"RXSTATUS\n")
            try:
                kind, state = self.frame(1)
                if kind == ("peer-status" if peer else "status"):
                    return state
            except TimeoutError:
                continue
        raise TimeoutError("USB runner did not return status")

    def upload(self, bundle, *, peer=False):
        prefix = "peer-" if peer else ""
        verb = "RXPEERPUT" if peer else "RXPUT"
        self.port.write(f"{verb} {len(bundle)}\n".encode("ascii"))
        while True:
            kind, payload = self.frame(5)
            if kind == prefix + "ready":
                break
            if kind == prefix + "result":
                return payload
        self.port.write(bundle)
        while True:
            kind, payload = self.frame(15)
            if kind == prefix + "result":
                return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    provision = commands.add_parser("provision", help="create a flash record from a public PEM key")
    provision.add_argument("public_key", type=Path)
    provision.add_argument("key_id")
    provision.add_argument("record", type=Path)
    status = commands.add_parser("status")
    deploy = commands.add_parser("deploy")
    peer_status = commands.add_parser("peer-status", help="show the LABELS service release")
    peer_deploy = commands.add_parser("peer-deploy", help="deploy a zero-grant LABELS service release")
    for command in (status, deploy, peer_status, peer_deploy):
        command.add_argument("--port", required=True)
    for command in (deploy, peer_deploy):
        command.add_argument("source", type=Path)
        command.add_argument("--private-key", type=Path, required=True)
        command.add_argument("--key-id", required=True)
    deploy.add_argument("--name", required=True)
    deploy.add_argument("--grant", action="append", choices=sorted(GRANTS), default=[])
    args = parser.parse_args()
    try:
        if args.command == "provision":
            der = openssl("pkey", "-pubin", "-in", args.public_key, "-pubout", "-outform", "DER")
            write_new(args.record, trust_record(args.key_id, der))
            return 0
        with open_port(args.port) as port:
            runner = UsbRunner(port)
            peer = args.command.startswith("peer-")
            state = runner.status(peer=peer)
            if args.command in ("status", "peer-status"):
                print(json.dumps(state, sort_keys=True))
                return int(state.get("storeFault") is not False)
            if state.get("storeFault") is not False:
                raise ValueError("USB runner reports a storage fault")
            if args.source.samefile(args.private_key):
                raise ValueError("source must be separate from the signing key")
            with args.source.open("rb") as file:
                source = file.read(8193)
            floor = state.get("highestVersion")
            if type(floor) is not int or not 0 <= floor < 0xffffffff:
                raise ValueError("invalid or exhausted version floor")
            name = LABELS_BUNDLE if peer else args.name
            grants = [] if peer else args.grant
            result = runner.upload(pack(source, name=name, version=floor + 1,
                                        key_id=args.key_id, grants=grants, private_key=args.private_key),
                                   peer=peer)
            print(json.dumps(result, sort_keys=True))
            return int(result.get("outcome") != "activated" or result.get("activeVersion") != floor + 1)
    except subprocess.CalledProcessError:
        parser.exit(1, "USB runner: signing or public key operation failed\n")
    except (OSError, ValueError, ImportError) as error:
        parser.exit(1, f"USB runner: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
