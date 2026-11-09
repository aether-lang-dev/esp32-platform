"""Two independently provisioned QEMU devices with distinct version floors."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, "/tools")
from rexx_psk import load

Runner = runpy.run_path("/tools/rexx")["Runner"]
qemu, clean_flash, signing_key = sys.argv[1:]
python = os.environ.get("REXX_TEST_PYTHON", sys.executable)


@contextmanager
def boot(flash, port, runner):
    command = [qemu, "-nographic", "-serial", "stdio", "-monitor", "none", "-machine", "esp32",
               "-drive", f"file={flash},if=mtd,format=raw",
               "-nic", f"user,model=open_eth,hostfwd=tcp:127.0.0.1:{port}-:443"]
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                assert process.poll() is None, "fleet QEMU exited"
                try:
                    if runner.status()["state"] == "idle":
                        break
                except (OSError, ValueError):
                    time.sleep(0.1)
            else:
                raise AssertionError("fleet device did not boot")
            yield
        except BaseException:
            log.seek(0)
            print(log.read().decode(errors="replace"), flush=True)
            raise
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            process.stdin.close()


def provision(root, name):
    credential = root / f"{name}.json"
    record = root / f"{name}.psk"
    subprocess.run([python, "/tools/rexx_psk.py", str(credential), str(record), "--generate", name], check=True)
    flash = root / f"{name}.bin"
    shutil.copyfile(clean_flash, flash)
    with flash.open("r+b") as file:
        file.seek(0x330000)
        file.write(record.read_bytes())
    return credential, flash


def rexx(command, expected, *markers):
    result = subprocess.run([python, "/tools/rexx", *map(str, command)], text=True,
                            capture_output=True, timeout=60)
    print(result.stdout, end="", flush=True)
    assert result.returncode == expected, (result.returncode, result.stderr, result.stdout)
    assert all(marker in result.stdout for marker in markers), (markers, result.stdout)


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    a_key, a_flash = provision(root, "alpha")
    b_key, b_flash = provision(root, "beta")
    assert load(a_key)[1] != load(b_key)[1]
    a_url = "https://127.0.0.1:18081"
    b_url = "https://127.0.0.1:18082"
    a, b = Runner(a_url, a_key), Runner(b_url, b_key)
    fleet = root / "fleet.json"
    fleet.write_text(json.dumps({
        "alpha": {"device": a_url, "credential": a_key.name},
        "beta": {"device": b_url, "credential": b_key.name},
    }))
    source = root / "desired.rexx"
    sign = ["--private-key", signing_key, "--key-id", "fleet-current", "--name", "test-script"]
    sync = ["sync", source, "--fleet", fleet, *sign]
    source.write_text("SAY 'previous release'\nEXIT 0\n")
    with boot(a_flash, 18081, a):
        rexx(["watch", source, "--once", "--device", a_url, "--device-credential", a_key, *sign],
             0, "v1 activated")
        source.write_text("SAY 'fleet release'\nEXIT 0\n")
        rexx(sync, 1, "alpha: v2 activated", "beta: unreachable")
        before = a.status()
        assert before["activeVersion"] == before["highestVersion"] == 2

    with boot(a_flash, 18081, a), boot(b_flash, 18082, b):
        assert a.status()["activeFingerprint"] == before["activeFingerprint"]
        rexx(sync, 0, "alpha: up to date (active v2)", "beta: v1 activated")
        rexx(sync, 0, "alpha: up to date (active v2)", "beta: up to date (active v1)")
        assert a.status()["highestVersion"] == 2 and b.status()["highestVersion"] == 1
        rexx(sync + ["--grant", "UPTIME_MS"], 0, "alpha: v3 activated", "beta: v2 activated")
        source.write_text("SAY (\n")
        rexx(sync, 1, "alpha: v4 rolled back (active v3", "beta: v3 rolled back (active v2")
        assert a.status()["activeVersion"] == 3 and b.status()["activeVersion"] == 2

print("FLEET independent keys, offline retry, reboot, grants and rollback PASS", flush=True)
