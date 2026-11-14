"""Exercise volatile REPL sessions through the device's real PSK endpoint."""
import http.client
import json
import time
from urllib.parse import urlsplit


def run(endpoint, context):
    url = urlsplit(endpoint)

    def request(path, body=None, session=None, sequence=None, after=None):
        headers = {}
        if session is not None:
            headers["X-Repl-Session"] = session
        if sequence is not None:
            headers["X-Repl-Sequence"] = str(sequence)
        if after is not None:
            headers["X-Repl-After"] = str(after)
        connection = http.client.HTTPSConnection(url.hostname, url.port, context=context, timeout=5)
        try:
            connection.request("POST" if body is not None else "GET", path, body, headers)
            response = connection.getresponse()
            return response.status, json.loads(response.read())
        finally:
            connection.close()

    def wait(session, sequence):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            code, state = request("/repl/events", session=session, after=0)
            assert code == 200, state
            if state["sequence"] == sequence and state["state"] == "done":
                return state
            time.sleep(0.05)
        raise AssertionError("REPL completion deadline")

    def text(state):
        return "".join(bytes.fromhex(event["hex"]).decode("utf-8") for event in state["events"])

    before = request("/status")[1]
    code, opened = request("/repl/open", b"3")
    assert code == 201, opened
    session = opened["session"]
    try:
        assert request("/repl/open", b"3")[0] == 409
        assert request("/repl/command", b"EXIT 0", session="0" * 32, sequence=1)[0] == 409
        assert request("/repl/command", b"x = 41", session=session, sequence=1)[0] == 202
        assert wait(session, 1)["exitCode"] == 0
        assert request("/repl/command", b"SAY x + 1", session=session, sequence=2)[0] == 202
        assert "42" in text(wait(session, 2))
        assert request("/repl/command", b"x = 0", session=session, sequence=2)[0] == 409
        assert request("/repl/command", b"IF", session=session, sequence=3)[0] == 202
        failed = wait(session, 3)
        assert failed["exitCode"] == -1 and any(e["kind"] == "error" for e in failed["events"]), failed
        assert request("/repl/command", b"SAY x", session=session, sequence=4)[0] == 202
        assert text(wait(session, 4)).endswith("41")
        assert request("/repl/command", b"x = 999\nCALL HTTP_GET 'http://invalid'", session=session, sequence=5)[0] == 202
        assert wait(session, 5)["exitCode"] == -1
        print("REPL variables, parse-error recovery, grant denial and duplicate refusal PASS", flush=True)
        source = b"x = 999\nSAY 'stream-before'\nDO FOREVER\nCALL WAIT_MS 100\nEND"
        assert request("/repl/command", source, session=session, sequence=6)[0] == 202
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            state = request("/repl/events", session=session, after=0)[1]
            if state["sequence"] == 6 and state["state"] == "running" and "stream-before" in text(state):
                break
            time.sleep(0.15)
        else:
            raise AssertionError("output was not visible before completion")
        assert request("/repl/cancel", b"", session=session)[0] == 202
        assert wait(session, 6)["cancelled"]
        assert request("/repl/command", b"SAY x", session=session, sequence=7)[0] == 202
        assert text(wait(session, 7)).endswith("41")
        for sequence in range(8, 18):
            assert request("/repl/command", b"x = x + 1", session=session, sequence=sequence)[0] == 202
            assert wait(session, sequence)["exitCode"] == 0
        unicode_text = "😺" * 130
        assert request("/repl/command", ("SAY '" + unicode_text + "'").encode(),
                       session=session, sequence=18)[0] == 202
        assert text(wait(session, 18)) == unicode_text
        assert request("/repl/command", b"DO i = 1 TO 100\nSAY i\nEND",
                       session=session, sequence=19)[0] == 202
        assert wait(session, 19)["lost"], "output overrun was hidden"
        assert request("/repl/command", b"s = 'x'\nDO FOREVER\ns = s || s\nEND",
                       session=session, sequence=20)[0] == 202
        exhausted = wait(session, 20)
        assert exhausted["exitCode"] == -1 and exhausted["runtimeOutcome"] == 4, exhausted
        assert request("/repl/command", b"x = 7\nSAY x", session=session, sequence=21)[0] == 202
        recovered = wait(session, 21)
        assert recovered["exitCode"] == 0 and text(recovered) == "7", recovered
        print("REPL Unicode chunks, output-overrun reporting and memory-limit recovery PASS", flush=True)
        assert request("/status")[1] == before, "REPL changed installed app state"
        print("REPL output streams before completion; cancellation and repeated contexts recover PASS", flush=True)
    finally:
        request("/repl/close", b"", session=session)
    deadline = time.monotonic() + 5
    while request("/repl/events", session=session, after=0)[0] != 409:
        assert time.monotonic() < deadline
        time.sleep(0.05)
    code, reopened = request("/repl/open", b"0")
    assert code == 201 and reopened["session"] != session
    assert request("/repl/command", b"SAY x", session=reopened["session"], sequence=1)[0] == 202
    reset = wait(reopened["session"], 1)
    assert text(reset) == "X", reset
    request("/repl/close", b"", session=reopened["session"])
    print("REPL session reset and installed-app isolation PASS", flush=True)

    def close_session(identifier):
        request("/repl/close", b"", session=identifier)
        deadline = time.monotonic() + 5
        while request("/repl/events", session=identifier, after=0)[0] != 409:
            assert time.monotonic() < deadline
            time.sleep(0.15)

    # Closing is asynchronous, so let the earlier session finish first.
    deadline = time.monotonic() + 5
    while request("/repl/events", session=reopened["session"], after=0)[0] != 409:
        assert time.monotonic() < deadline
        time.sleep(0.15)
    code, opened = request("/repl/open", b"0")
    assert code == 201, opened
    identifier = opened["session"]
    try:
        assert request("/repl/command", b"ADDRESS APPS 'LIST'\nSAY RC",
                       session=identifier, sequence=1)[0] == 202
        assert text(wait(identifier, 1)) == "13"
    finally:
        close_session(identifier)

    code, opened = request("/repl/open", b"1024")
    assert code == 201, opened
    identifier = opened["session"]
    try:
        assert request("/repl/command", b"ADDRESS APPS 'LIST'\nSAY RESULT",
                       session=identifier, sequence=1)[0] == 202
        registry = json.loads(text(wait(identifier, 1)))
        assert len(registry) == 1 and registry[0]["app"] == "main" and registry[0]["installed"], registry
        assert request("/repl/command", b"ADDRESS APPS 'STATUS main'\nSAY RESULT",
                       session=identifier, sequence=2)[0] == 202
        app = json.loads(text(wait(identifier, 2)))
        assert app["activeVersion"] == before["activeVersion"] and app["highestVersion"] == before["highestVersion"], app
        assert app["name"] == registry[0]["name"], app
        assert request("/repl/command", b"ADDRESS APPS 'RUN main'\nSAY RC",
                       session=identifier, sequence=3)[0] == 202
        assert text(wait(identifier, 3)) == "13"
        assert request("/status")[1] == before
    finally:
        close_session(identifier)
    print("ADDRESS APPS inventory, status and read/run grant separation PASS", flush=True)

    code, opened = request("/repl/open", b"2048")
    assert code == 201, opened
    identifier = opened["session"]
    try:
        source = b"ADDRESS APPS 'RUN main'\nticket = RESULT\nADDRESS APPS 'RUN main'\nSAY RC\nSAY ticket"
        assert request("/repl/command", source, session=identifier, sequence=1)[0] == 202
        state = wait(identifier, 1)
        outputs = [bytes.fromhex(event["hex"]).decode() for event in state["events"]]
        assert outputs[0] == "8", outputs
        ticket = json.loads(outputs[1])
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            state = request("/status")[1]
            if state["id"] == ticket["queued"] and state["state"] in ("done", "error"):
                break
            time.sleep(0.15)
        else:
            raise AssertionError("queued app did not finish")
        assert state["exitCode"] == 0 and state["runtimeOutcome"] == 0, state
        for field in ("activeVersion", "highestVersion", "activeSlot", "activeFingerprint"):
            assert state[field] == before[field], state
    finally:
        close_session(identifier)
    print("ADDRESS APPS queues the installed app, denies a busy rerun and preserves its slots PASS", flush=True)
