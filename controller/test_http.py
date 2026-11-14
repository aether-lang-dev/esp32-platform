"""Opt-in controller integration check against a disposable QEMU device."""
import http.client
import json
import queue
import sys
import threading
import time
from urllib.parse import urlsplit


def run(origin):
    target = urlsplit(origin)
    state = {"sid": "", "seq": 0, "code": "", "transcript": "", "busy": False}
    updates = queue.Queue()

    def request(path, payload=None, headers=None):
        connection = http.client.HTTPConnection(target.hostname, target.port, timeout=20)
        sent = {"Origin": origin, "Content-Type": "application/json", **(headers or {})}
        connection.request("GET" if payload is None else "POST", path,
                           None if payload is None else json.dumps(payload), sent)
        return connection, connection.getresponse()

    def consume(path, payload=None, notify=False):
        connection, response = request(path, payload)
        assert response.status == 200, (response.status, response.read())
        assert response.getheader("Content-Type") == "text/event-stream"
        lines = []
        try:
            while line := response.readline():
                text = line.decode().rstrip("\r\n")
                if text.startswith("data: signals "):
                    lines.append(text[len("data: signals "):])
                elif not text and lines:
                    patch = json.loads("\n".join(lines))
                    state.update(patch)
                    if notify:
                        updates.put(dict(state))
                    lines = []
        finally:
            connection.close()

    consume("/status")
    assert state["connected"], state
    before = state["active"], state["floor"]
    for headers in [{"Origin": "http://untrusted.example"}, {"Host": "untrusted.example"}]:
        connection, response = request("/repl/open", dict(state), headers)
        assert response.status == 403, response.status
        response.read()
        connection.close()
    consume("/repl/open", dict(state))
    assert len(state["sid"]) == 32, state
    try:
        state["code"] = "x = 41"
        consume("/repl/run", dict(state))
        assert state["seq"] == 1 and state["message"] == "Exit 0" and not state["busy"], state
        state["code"] = "SAY x + 1"
        consume("/repl/run", dict(state))
        assert state["transcript"].endswith("42\n"), state
        print("Controller opens session, retains variables and streams results PASS", flush=True)

        state["code"] = "SAY 'streaming'\nDO FOREVER\nCALL WAIT_MS 100\nEND"
        errors = []

        def stream():
            try:
                consume("/repl/run", dict(state), notify=True)
            except BaseException as error:
                errors.append(error)
                updates.put(None)

        worker = threading.Thread(target=stream)
        worker.start()
        deadline = time.monotonic() + 15
        try:
            while True:
                observed = updates.get(timeout=max(.1, deadline - time.monotonic()))
                assert observed is not None, errors
                if observed["busy"] and observed["transcript"].endswith("streaming\n"):
                    break
                assert time.monotonic() < deadline
            consume("/repl/cancel", dict(state))
        finally:
            worker.join(timeout=20)
        assert not worker.is_alive(), "command stream did not finish"
        assert not errors, errors
        assert not state["busy"] and state["message"] == "Exit -1", state
        assert "cancelled from controller" in state["transcript"], state
        state["code"] = "SAY x"
        consume("/repl/run", dict(state))
        assert state["transcript"].endswith("41\n"), state
        print("Controller Cancel works during the stream; next command retains state PASS", flush=True)
        # Inspect independently controlled echoes without device effects.
        for showrc, showresult, expected in [
            (True, False, "RC = 7\n"),
            (False, True, "RESULT = toggle value\n"),
            (True, True, "RC = 7\nRESULT = toggle value\n"),
            (False, False, "RESULT = 'toggle value'\n"),
        ]:
            state.update(showrc=showrc, showresult=showresult)
            state["code"] = "RC = 7\nRESULT = 'toggle value'"
            consume("/repl/run", dict(state))
            assert state["transcript"].endswith(expected), state
        state.update(showrc=True, showresult=True)
        state["code"] = "EXIT 0"
        consume("/repl/run", dict(state))
        assert state["transcript"].endswith("rexx> EXIT 0\n"), state
        print("Controller independent RC/RESULT toggles and explicit EXIT behaviour PASS", flush=True)

    finally:
        consume("/repl/close", dict(state))
    assert not state["sid"], state
    consume("/status")
    assert (state["active"], state["floor"]) == before
    print("Controller rejects foreign origins and preserves installed-app metadata PASS", flush=True)


if __name__ == "__main__":
    run(sys.argv[1] if len(sys.argv) == 2 else "http://127.0.0.1:8788")
