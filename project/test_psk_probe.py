"""Verify that TLS authentication gates the real HTTP status handler."""
import json
from pathlib import Path
import subprocess
import sys
import threading

qemu, flash, key_file = sys.argv[1:]
credential = json.loads(Path(key_file).read_text())
key = bytes.fromhex(credential["key"])
ready = threading.Event()
qemu_process = subprocess.Popen(
    [qemu, "-nographic", "-serial", "stdio", "-monitor", "none", "-machine", "esp32",
     "-drive", f"file={flash},if=mtd,format=raw",
     "-nic", "user,model=open_eth,hostfwd=tcp:127.0.0.1:18443-:443"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
)

def console():
    for line in qemu_process.stdout:
        print(line, end="", flush=True)
        if "eth ip:" in line:
            ready.set()

def request(secret=None, identity="device-test"):
    command = ["openssl", "s_client", "-quiet", "-connect", "127.0.0.1:18443",
               "-tls1_3", "-ciphersuites", "TLS_AES_128_GCM_SHA256"]
    if secret is not None:
        # Disposable fixture only. Production clients should take a key file.
        command += ["-psk_identity", identity, "-psk", secret.hex()]
    try:
        return subprocess.run(command, input=b"GET /status HTTP/1.1\r\nHost: device\r\nConnection: close\r\n\r\n",
                              capture_output=True, timeout=10)
    except subprocess.TimeoutExpired:
        raise AssertionError("TLS client timed out") from None

def assert_status(hits):
    result = request(key)
    head, separator, body = result.stdout.partition(b"\r\n\r\n")
    assert separator and head.startswith(b"HTTP/1.1 200 "), (head, result.stderr)
    assert json.loads(body) == {"hits": hits}, body

reader = threading.Thread(target=console, daemon=True)
reader.start()
try:
    assert ready.wait(45), "QEMU did not get an IP address"
    assert_status(1)
    wrong = bytes([key[0] ^ 1]) + key[1:]
    for label, secret, identity in [
        ("wrong key", wrong, "device-test"),
        ("unknown identity", key, "other-device"),
        ("missing key", None, "device-test"),
    ]:
        result = request(secret, identity)
        assert result.returncode and not result.stdout, (label, result.stdout)
        print(f"PSK refused {label}", flush=True)
    assert_status(2)
    print("PSK PROBE OK", flush=True)
    print("HTTP CHECKS COMPLETE", flush=True)
finally:
    qemu_process.terminate()
    try:
        qemu_process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        qemu_process.kill()
        qemu_process.wait()
    reader.join(timeout=5)
