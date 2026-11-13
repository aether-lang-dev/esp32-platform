"""Local controller transport. Credentials stay in the PSK-capable process."""
import argparse
import http.client
import json
import re
from pathlib import Path
import runpy
import sys
from urllib.parse import urlsplit

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
Runner = runpy.run_path(str(TOOLS / "rexx"))["Runner"]
from rexx_psk import client_context, load


def read_status(device, credential):
    if urlsplit(device).scheme != "https":
        raise ValueError("controller devices require HTTPS")
    state = Runner(device, credential).status()
    return {
        "connected": True,
        "message": "",
        "state": state["state"],
        "active": state["activeVersion"],
        "floor": state["highestVersion"],
        "trial": state["trialVersion"],
        "exit": state["exitCode"],
    }


def repl_request(device, credential, action, session="", sequence=0, source=""):
    url = urlsplit(device)
    if url.scheme != "https" or not url.hostname or url.username or url.path not in ("", "/") or url.query or url.fragment:
        raise ValueError("invalid device origin")
    routes = {"open": "/repl/open", "run": "/repl/command", "events": "/repl/events",
              "cancel": "/repl/cancel", "close": "/repl/close"}
    headers = {}
    if action != "open":
        if not re.fullmatch("[0-9a-f]{32}", session):
            raise ValueError("invalid session")
        headers["X-Repl-Session"] = session
    body = None
    if action == "open":
        body = b"3"
    elif action == "run":
        body = source.encode("utf-8")
        if not 1 <= len(body) <= 8192:
            raise ValueError("command must be 1..8192 bytes")
        headers["X-Repl-Sequence"] = str(sequence)
    elif action == "events":
        headers["X-Repl-After"] = str(sequence)
    else:
        body = b""
    connection = http.client.HTTPSConnection(url.hostname, url.port,
                     context=client_context(*load(credential)), timeout=5)
    try:
        connection.request("GET" if body is None else "POST", routes[action], body, headers)
        response = connection.getresponse()
        if response.status == 404:
            return {"ok": False, "error": "REPL firmware is not installed on this device"}
        raw = response.read(10001)
        if len(raw) > 10000:
            raise ValueError("response too large")
        result = json.loads(raw)
        if not isinstance(result, dict):
            raise ValueError("invalid response")
        result["ok"] = response.status < 300
        if action == "events" and result["ok"]:
            result["output"] = "".join(
                bytes.fromhex(event["hex"]).decode("utf-8", errors="replace") +
                ("\n" if event["end"] else "")
                for event in result["events"])
            del result["events"]
        return result
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", required=True)
    parser.add_argument("--credential", required=True, type=Path)
    parser.add_argument("command", choices=["status", "open", "run", "events", "cancel", "close"])
    parser.add_argument("--session", default="")
    parser.add_argument("--sequence", type=int, default=0)
    parser.add_argument("--source", default="")
    args = parser.parse_args()
    try:
        if args.command == "status":
            result = read_status(args.device, args.credential)
        else:
            result = repl_request(args.device, args.credential, args.command,
                                  args.session, args.sequence, args.source)
    except (OSError, ValueError, http.client.HTTPException):
        # Exceptions can contain local filenames and endpoint details.
        print(json.dumps({"connected": False, "message": "Device unavailable or authentication refused", "ok": False}))
        return 1
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
