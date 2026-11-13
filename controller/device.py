"""Local controller transport. Credentials stay in the PSK-capable process."""
import argparse
import http.client
import json
from pathlib import Path
import runpy
import sys
from urllib.parse import urlsplit

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
Runner = runpy.run_path(str(TOOLS / "rexx"))["Runner"]


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", required=True)
    parser.add_argument("--credential", required=True, type=Path)
    parser.add_argument("command", choices=["status"])
    args = parser.parse_args()
    try:
        result = read_status(args.device, args.credential)
    except (OSError, ValueError, http.client.HTTPException):
        # Exceptions can contain local filenames and endpoint details.
        print(json.dumps({"connected": False, "message": "Device unavailable or authentication refused"}))
        return 1
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
