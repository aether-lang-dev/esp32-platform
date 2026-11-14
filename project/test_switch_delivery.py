"""Opt-in SWITCH integration test. Physical device outlet MUST be empty."""
import http.client
import json
import time
from urllib.parse import urlsplit


def run(endpoint, context):
    target = urlsplit(endpoint)
    sid = ""
    sequence = 0

    def request(path, body=None):
        connection = http.client.HTTPSConnection(target.hostname, target.port, context=context, timeout=5)
        headers = {"X-Repl-Session": sid, "X-Repl-Sequence": str(sequence), "X-Repl-After": "0"}
        try:
            connection.request("GET" if body is None else "POST", path, body, headers)
            response = connection.getresponse()
            return response.status, json.loads(response.read())
        finally:
            connection.close()

    def opened(grants):
        nonlocal sid, sequence
        status, result = request("/repl/open", str(grants).encode())
        assert status == 201, result
        sid, sequence = result["session"], 0

    def close():
        request("/repl/close", b"")
        deadline = time.monotonic() + 10
        while request("/repl/events")[0] != 409:
            assert time.monotonic() < deadline
            time.sleep(.15)

    def submit(source):
        nonlocal sequence
        sequence += 1
        status, result = request("/repl/command", source.encode())
        assert status == 202, result

    def wait():
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            status, result = request("/repl/events")
            assert status == 200, result
            if result["state"] == "done" and result["sequence"] == sequence:
                return result
            time.sleep(.15)
        raise AssertionError("SWITCH completion timeout")

    def command(source, expected, exit_code=0):
        submit(source)
        result = wait()
        text = "".join(bytes.fromhex(e["hex"]).decode() for e in result["events"])
        assert result["exitCode"] == exit_code and text == expected, (source, result)

    before = request("/status")[1]
    opened(0)
    try:
        command("ADDRESS SWITCH 'STATUS'\nSAY RC", "13")
        command("ADDRESS SWITCH 'ON 1'\nSAY RC", "13")
    finally:
        close()
    opened(64)
    try:
        command("ADDRESS SWITCH 'STATUS'\nSAY RESULT", "OFF")
        command("ADDRESS SWITCH 'ON 1'\nSAY RC", "13")
    finally:
        close()
    opened(128)
    try:
        command("ADDRESS SWITCH 'STATUS'\nSAY RC", "13")
        command("ADDRESS SWITCH 'ON 61'\nSAY RC", "4")
        command("ADDRESS SWITCH 'ON'\nSAY RC", "4")
    finally:
        close()
    print("SWITCH separate read/control grants and bounded duration PASS", flush=True)

    opened(195)
    try:
        command("ADDRESS SWITCH 'ON 1'\nSAY RC\nSAY RESULT", "0ON")
        command("CALL WAIT_MS 750\nCALL WAIT_MS 750\nADDRESS SWITCH 'STATUS'\nSAY RESULT", "OFF")
        command("ADDRESS SWITCH 'ON 60'\nEXIT 7", "", 7)
        command("ADDRESS SWITCH 'STATUS'\nSAY RESULT", "OFF")
        command("ADDRESS SWITCH 'ON 60'", "")
        command("ADDRESS SWITCH 'STATUS'\nSAY RESULT", "ON")
        command("EXIT 7", "", 7)
        command("ADDRESS SWITCH 'STATUS'\nSAY RESULT", "OFF")
        command("ADDRESS SWITCH 'ON 60'", "")
        submit("SAY 'running'\nDO FOREVER\nCALL WAIT_MS 100\nEND")
        deadline = time.monotonic() + 10
        while True:
            result = request("/repl/events")[1]
            if result.get("events"):
                break
            assert time.monotonic() < deadline
            time.sleep(.15)
        assert request("/repl/cancel", b"")[0] == 202
        assert wait()["exitCode"] == -1
        command("ADDRESS SWITCH 'STATUS'\nSAY RESULT", "OFF")
        command("ADDRESS SWITCH 'ON 60'", "")
        assert request("/repl/cancel", b"")[0] == 202
        time.sleep(.3)
        command("ADDRESS SWITCH 'STATUS'\nSAY RESULT", "OFF")
        command("ADDRESS SWITCH 'ON 60'", "")
    finally:
        close()
    opened(64)
    try:
        command("ADDRESS SWITCH 'STATUS'\nSAY RESULT", "OFF")
    finally:
        close()
    assert request("/status")[1] == before, "relay session changed managed-app metadata"
    print("SWITCH independent timeout, owner failure, cancellation, close and app isolation PASS", flush=True)
