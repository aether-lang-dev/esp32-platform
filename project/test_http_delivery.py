"""Exercise unsigned development delivery entirely inside the QEMU container."""
import json
import http.client
import socket
from pathlib import Path
import os
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

psk_file = os.environ.get("REXX_TEST_PSK")
context = None
wrong_context = None
if psk_file:
    sys.path.insert(0, "/tools")
    from rexx_psk import client_context, load
    identity, secret = load(Path(psk_file))
    context = client_context(identity, secret)
    wrong_context = client_context(identity, bytes([secret[0] ^ 1]) + secret[1:])
base = ("https" if context else "http") + "://127.0.0.1:18080"
port = 443 if context else 80
monitor = Path("/tmp/runner-monitor.sock")
monitor.unlink(missing_ok=True)
command = ["timeout", os.environ.get("QEMU_TIMEOUT", "120"), sys.argv[1],
           "-nographic", "-serial", "stdio", "-monitor", f"unix:{monitor},server=on,wait=off", "-machine", "esp32", "-drive", f"file={sys.argv[2]},if=mtd,format=raw",
           "-nic", f"user,model=open_eth,hostfwd=tcp:127.0.0.1:18080-:{port}"]
process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
lines = []
fatal = threading.Event()

def console():
    for line in process.stdout:
        lines.append(line)
        if "Guru Meditation" in line or "abort() was called" in line:
            fatal.set()
        print(line, end="", flush=True)

reader = threading.Thread(target=console, daemon=True)
reader.start()
handlers = [urllib.request.ProxyHandler({})]
if context:
    handlers.append(urllib.request.HTTPSHandler(context=context))
opener = urllib.request.build_opener(*handlers)

def request(path, body=None):
    req = urllib.request.Request(base + path, data=body,
                                 headers={"Content-Type": "text/plain", "Connection": "close"})
    try:
        with opener.open(req, timeout=3) as response:
            return response.status, json.loads(response.read())
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read())

def status_when(predicate, limit=15):
    deadline = time.monotonic() + limit
    last = None
    while time.monotonic() < deadline:
        assert process.poll() is None, "QEMU exited"
        assert not fatal.is_set(), "firmware panic"
        try:
            code, last = request("/status")
            if code == 200 and predicate(last):
                return last
        except (OSError, TimeoutError, http.client.HTTPException, json.JSONDecodeError):
            pass
        time.sleep(0.05)
    raise AssertionError(f"status deadline: {last}")

def upload(source, expected_exit, cancelled=False):
    code, receipt = request("/script", source.encode())
    assert code == 202, (code, receipt)
    result = status_when(lambda s: s["id"] == receipt["id"] and s["state"] in ("done", "error"))
    assert result["exitCode"] == expected_exit and result["cancelled"] == cancelled, result
    return receipt["id"]

try:
    if len(sys.argv) == 4:
        from test_signed_delivery import run
        def reset():
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                connection.settimeout(3)
                connection.connect(str(monitor))
                connection.recv(4096)
                connection.sendall(b"system_reset\n")
                connection.recv(4096)
        run(request, status_when, lines, sys.argv[3], sys.argv[2], reset, context, wrong_context)
    else:
        initial = status_when(lambda s: s["state"] == "idle", limit=75)
        for body, expected in [(b"", 400), (b"EXIT 0\0EXIT 1", 400), (b"x" * 8193, 413)]:
            code, _ = request("/script", body)
            assert code == expected, code
        assert request("/status")[1] == initial, "rejected bodies changed runner state"
        print("HTTP rejected empty/NUL/oversized bodies PASS", flush=True)
        first = upload("SAY 'delivery first'\nEXIT 7\n", 7)
        second = upload("SAY 'delivery second'\nEXIT 0\n", 0)
        assert second == first + 1
        print("HTTP changed-source delivery and exit status PASS", flush=True)
        upload("CALL HTTP_GET 'http://invalid'\nEXIT 0\n", -1)
        print("HTTP uploaded script cannot acquire HTTP service PASS", flush=True)
        code, receipt = request("/script", b"CALL WAIT_MS 1000\nEXIT 0\n")
        assert code == 202
        code, _ = request("/script", b"EXIT 42\n")
        assert code == 409, code
        result = status_when(lambda s: s["id"] == receipt["id"] and s["state"] == "done")
        assert result["exitCode"] == 0
        print("HTTP busy upload refused while runner yields PASS", flush=True)
        upload("DO FOREVER\nLET n = 1\nEND\n", -1, cancelled=True)
        upload("SAY 'delivery recovered'\nEXIT 0\n", 0)
        print("HTTP runaway cancelled and next upload runs PASS", flush=True)
        text = "".join(lines)
        for marker in ["RUNNER> delivery first", "RUNNER> delivery second", "RUNNER> delivery recovered"]:
            assert marker in text, marker
        assert "Guru Meditation" not in text and "abort() was called" not in text
        print("HTTP RUNNER LANE OK", flush=True)
        print("HTTP CHECKS COMPLETE", flush=True)

finally:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    reader.join(timeout=2)
