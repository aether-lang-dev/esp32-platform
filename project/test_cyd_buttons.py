"""Run the signed button app against a native RexxAE library and a mock CYD."""
import ctypes
import json
import sys
from pathlib import Path


def check(library):
    engine = ctypes.CDLL(str(library))
    handle, string, integer = ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32

    def bind(name, result, *arguments):
        function = getattr(engine, "aether_rexxae_" + name)
        function.restype, function.argtypes = result, arguments
        return function

    create = bind("create", handle, string)
    destroy = bind("destroy", None, handle)
    register = bind("register_function", integer, handle, string, integer, integer)
    step = bind("step", integer, handle, integer)
    function = bind("function", string, handle)
    arguments = bind("arguments_json", string, handle)
    reply = bind("reply_value", integer, handle, string, string)
    exit_code = bind("exit_code", integer, handle)
    output = bind("text", string, handle)
    source = (Path(__file__).resolve().parents[1] / "scripts/examples/signed_buttons.rexx").read_bytes()

    def run(tap):
        context = create(source)
        assert context
        try:
            assert register(context, b"TOUCH_EVENT", 0, 0) == 0
            assert register(context, b"DISPLAY_BUTTONS", 4, 4) == 0
            screens = []
            for _ in range(100):
                status = step(context, 32)
                if status == 0:
                    assert exit_code(context) == 0
                    return screens
                if status == 4:
                    name = function(context).lower()
                    if name == b"touch_event":
                        value = json.dumps(tap).encode()
                    else:
                        assert name == b"display_buttons", name
                        screens.append(json.loads(arguments(context)))
                        value = b"true"
                    assert reply(context, value, b"") == 0
                else:
                    assert status in (1, 3), (status, output(context))
            raise AssertionError("script did not finish")
        finally:
            destroy(context)

    # Physical raw samples corresponding to the centre of each band,
    # the vertical gap, and the gutter to the left of a band.
    for point, message in [
        (None, "Tap a button"),
        ((2074, 1295), "Hello, CYD!"),
        ((2074, 2412), "Rexx on ESP32"),
        ((2074, 1853), None),
        ((3860, 1295), None),
    ]:
        tap = None if point is None else dict(x=point[0], y=point[1], pressure=500)
        expected = [] if message is None else [["REXXAE TOUCH", "HELLO", "ABOUT", message]]
        actual = run(tap)
        assert actual == expected, (tap, actual, expected)
    print("CYD button app trial, two hit regions, gap and gutter PASS")


if __name__ == "__main__":
    check(Path(sys.argv[1]).resolve())
