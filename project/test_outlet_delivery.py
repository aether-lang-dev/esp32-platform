"""Opt-in timed-outlet test. Physical device outlet MUST be empty."""
import http.client
import json
import time
from urllib.parse import urlsplit


def run(endpoint, context):
    target = urlsplit(endpoint)
    sid, sequence = "", 0

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

    def command(source, expected=None, exit_code=0):
        nonlocal sequence
        sequence += 1
        assert request("/repl/command", source.encode())[0] == 202
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            status, result = request("/repl/events")
            assert status == 200, result
            if result["state"] == "done" and result["sequence"] == sequence:
                text = "".join(bytes.fromhex(e["hex"]).decode() for e in result["events"])
                assert result["exitCode"] == exit_code, result
                if expected is not None:
                    assert text == expected, (source, result)
                return text
            time.sleep(.15)
        raise AssertionError("OUTLET completion deadline")

    def outlet_state():
        return json.loads(command("ADDRESS OUTLET 'STATUS'\nSAY RESULT"))

    before = request("/status")[1]
    for grants in (0, 8192, 32768):
        opened(grants)
        try:
            command("ADDRESS OUTLET 'START 5'\nSAY RC", "13")
            if grants != 8192:
                command("ADDRESS OUTLET 'STATUS'\nSAY RC", "13")
            if grants != 32768:
                command("ADDRESS OUTLET 'CANCEL'\nSAY RC", "13")
        finally:
            close()
    opened(64707)
    try:
        command("ADDRESS OUTLET 'START 61'\nSAY RC", "4")
        command("ADDRESS OUTLET 'START 60'")
        assert outlet_state()["state"] == "running"
        command("ADDRESS SWITCH 'ON 1'\nSAY RC", "8")
        command("EXIT 7", "", 7)
        assert outlet_state()["state"] == "running", "caller failure stopped native app"
    finally:
        close()
    opened(8192)
    try:
        assert outlet_state()["state"] == "running", "closing caller stopped native app"
    finally:
        close()
    opened(32768)
    try:
        command("ADDRESS OUTLET 'CANCEL'")
    finally:
        close()
    print("OUTLET distinct grants, caller-independent lifecycle and exclusive output PASS", flush=True)
    opened(64707)
    try:
        command("ADDRESS OUTLET 'START 1'")
        command("CALL WAIT_MS 750\nCALL WAIT_MS 750")
        assert outlet_state()["state"] == "idle"
        command("ADDRESS OUTLET 'START 60'")
        reply = json.loads(command("ADDRESS APPS 'CALL main outlet STATUS'\nSAY RESULT"))
        assert reply["state"] == "running", reply
        command("ADDRESS APPS 'CALL main outlet START 1'\nSAY RC", "13")
        assert outlet_state()["state"] == "running"
        reply = json.loads(command("ADDRESS APPS 'CALL main outlet CANCEL'\nSAY RESULT"))
        assert reply["state"] == "idle", reply
        assert outlet_state()["state"] == "idle"
    finally:
        command("ADDRESS OUTLET 'CANCEL'")
        close()
    assert request("/status")[1] == before, "OUTLET changed managed-app metadata"
    print("OUTLET timeout, signed peer status/cancel and no inherited start authority PASS", flush=True)
