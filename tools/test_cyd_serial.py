"""Exercise the FNK0114B Rexx prompt over USB serial using pyserial."""

import argparse
import time

import serial


PROMPT = b"rexx> "


def read_prompt(port, timeout=10):
    deadline = time.monotonic() + timeout
    received = bytearray()
    while time.monotonic() < deadline:
        received.extend(port.read_until(PROMPT))
        if received.endswith(PROMPT):
            return bytes(received)
    raise RuntimeError(f"Prompt timed out: {received!r}")


def check_reply(port, command, expected):
    port.write(command)
    response = read_prompt(port)
    if expected not in response or b"CYD console transfer failed" in response:
        raise RuntimeError(f"Unexpected reply to {command!r}: {response!r}")
    return response


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB serial device for the FNK0114B")
    args = parser.parse_args()
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=5)
    connection.port = args.port
    connection.dtr = False
    connection.rts = False
    connection.open()
    with connection as port:
        port.reset_input_buffer()
        port.rts = True
        time.sleep(0.15)
        port.rts = False
        boot = read_prompt(port)
        for expected in (b"RUNNER CYD console ready", b"RUNNER LANE OK"):
            if expected not in boot:
                raise RuntimeError(f"Expected {expected!r} in boot output: {boot!r}")

        check_reply(port, b"SAY 6 * 7\r\n", b"RUNNER> 42")
        check_reply(port, b"CALL WAIT_MS 20; SAY 'timer ok'\r", b"RUNNER> timer ok")
        check_reply(port, b"SAY 8\x7f9\n", b"RUNNER> 9")
        check_reply(port, b"ADDRESS APP 'forbidden'\r", b"RC -1")
        check_reply(port, b"SAY 'must not run'; " + b"x" * 161 + b"\r", b"command discarded")
        check_reply(port, b"EXIT 7\r", b"RC 7")
        check_reply(port, b"SAY JSON_STRINGIFY({\"tap\": WAIT_TOUCH(100)})\r",
                    b'RUNNER> {"tap":null}')
        check_reply(port, b"SAY WAIT_TOUCH(2001)\r", b"WAIT_TOUCH requires an integer from 0 to 2000")
        check_reply(port, b"LET marker = 5\r", PROMPT)
        check_reply(port, b"SAY marker\r", b"RUNNER> MARKER")
        check_reply(port, b"SAY 'recovered'\r", b"RUNNER> recovered")
    print("CYD serial prompt PASS: arithmetic, timer, editing, denial, overflow, touch timeout and recovery")


if __name__ == "__main__":
    main()
