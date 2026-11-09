"""Observable signed-update and service-grant checks against real firmware."""
from pathlib import Path
import socket
import http.client
import os
import ssl
import queue
import subprocess
import sys
import tempfile
import threading
import time

def check_watch(fixtures, status_when):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "script.rexx"
        def save(text):
            temporary = source.with_suffix(".tmp")
            temporary.write_text(text)
            temporary.replace(source)

        save("SAY 'watch first'\nEXIT 0\n")
        credential = os.environ.get("REXX_TEST_PSK")
        command = [os.environ.get("REXX_TEST_PYTHON", sys.executable), "/tools/rexx", "watch", str(source),
                   "--device", ("https" if credential else "http") + "://127.0.0.1:18080",
                   "--private-key", str(fixtures / "fleet-current.pem"),
                   "--key-id", "fleet-current", "--name", "test-script",
                   "--grant", "WAIT_MS"]
        if credential:
            command += ["--device-credential", credential]
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True) as watcher:
            output = queue.Queue()

            def collect():
                for line in watcher.stdout:
                    print(f"WATCH {line}", end="", flush=True)
                    output.put(line)
                output.put(None)

            def expect(prefix):
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    try:
                        line = output.get(timeout=max(0, deadline - time.monotonic()))
                    except queue.Empty:
                        break
                    assert line is not None, f"watcher exited before {prefix}"
                    if line.startswith(prefix):
                        return
                raise AssertionError(f"watcher did not report {prefix}")

            reader = threading.Thread(target=collect, daemon=True)
            reader.start()
            try:
                expect("v14 activated")
                save("SAY 'watch first'\nEXIT 0\n")
                time.sleep(0.85)
                first = status_when(lambda state: state["state"] in ("done", "error"))
                assert first["activeVersion"] == first["highestVersion"] == 14, first
                save("SAY (\n")
                expect("v15 rolled back (active v14")
                failed = status_when(lambda state: state["state"] == "error")
                assert failed["activeVersion"] == 14 and failed["highestVersion"] == 15, failed
                save("CALL WAIT_MS 1\nSAY 'watch corrected'\nEXIT 0\n")
                expect("v16 activated")
                fixed = status_when(lambda state: state["state"] == "done")
                assert fixed["activeVersion"] == fixed["highestVersion"] == 16, fixed
            finally:
                watcher.terminate()
                watcher.wait(timeout=5)
                reader.join(timeout=5)


def run(request, status_when, lines, fixture_dir, flash_path, reset, context=None, wrong_context=None):
    fixtures = Path(fixture_dir)
    def storage_snapshot():
        with open(flash_path, 'rb') as flash:
            flash.seek(0x9000)
            metadata = flash.read(0x6000)
            flash.seek(0x310000)
            return metadata + flash.read(0x20000)
    def data(name):
        return (fixtures / (name + '.bundle')).read_bytes()
    def accepted(name, exit_code=0, cancelled=False, runtime_outcome=0):
        code, receipt = request('/script', data(name))
        assert code == 202, (name, code, receipt)
        state = status_when(lambda s: s['id'] == receipt['id'] and s['state'] in ('done', 'error'))
        assert state['exitCode'] == exit_code and state['cancelled'] == cancelled, (name, state)
        if runtime_outcome is not None:
            assert state['runtimeOutcome'] == runtime_outcome, (name, state)
        return receipt['id']
    initial = status_when(lambda s: s['state'] == 'idle', limit=75)
    untouched = storage_snapshot()
    if wrong_context:
        wrong = http.client.HTTPSConnection("127.0.0.1", 18080, context=wrong_context, timeout=3)
        try:
            wrong.request("POST", "/script", data("valid"))
        except ssl.SSLError:
            pass
        else:
            raise AssertionError("wrong PSK connected")
        finally:
            wrong.close()
        assert request("/status")[1] == initial
        assert storage_snapshot() == untouched
        print("SIGNED wrong PSK left flash and version floor unchanged PASS", flush=True)
    rejected = ['raw-source', 'unknown-key', 'forged-key-id', 'unknown-grant', 'usb-touch-grant',
                'usb-display-grant', 'usb-address-grant', 'usb-switch-read', 'usb-switch-control',
                'usb-settings-read', 'usb-settings-write', 'trailing', 'truncated',
                'tampered-magic', 'tampered-version', 'tampered-grants', 'tampered-key-id',
                'tampered-name', 'tampered-source', 'tampered-signature', 'remapped-key',
                'zero-key-length', 'huge-key-length', 'zero-name-length', 'huge-name-length',
                'zero-source-length', 'huge-source-length']
    for name in rejected:
        code, _ = request('/script', data(name))
        assert code == 403, (name, code)
        assert request('/status')[1] == initial, (name, 'rejection changed state')
        assert storage_snapshot() == untouched, (name, 'rejection modified flash')
    assert request('/script', data('labels-peer'))[0] == 409
    assert request('/status')[1] == initial
    assert storage_snapshot() == untouched
    assert request('/script', data('oversized'))[0] == 413
    assert request('/script', b'')[0] == 400
    # A peer disappearing mid-body must release the sole upload token.
    with socket.create_connection(('127.0.0.1',18080),timeout=3) as peer:
        if context:
            peer = context.wrap_socket(peer, server_hostname="127.0.0.1")
        with peer:
            peer.sendall((f"POST /script HTTP/1.1\r\nHost: device\r\nContent-Length: {len(data('valid'))}\r\n\r\n").encode()+data('valid')[:10])
    status_when(lambda s: s==initial)
    assert storage_snapshot()==untouched
    print('SIGNED disconnected upload releases request ownership PASS', flush=True)
    print('SIGNED raw/forged/tampered/unsupported bundles rejected PASS', flush=True)
    first = accepted('valid', 7)
    assert request('/status')[1]['activeVersion'] == 0
    assert request('/status')[1]['highestVersion'] == 1
    second = accepted('next-key')
    assert second == first + 1
    assert request('/status')[1]['activeVersion'] == 2
    print('SIGNED current and rotation keys accepted PASS', flush=True)
    accepted('no-wait', -1)
    assert request('/status')[1]['activeVersion'] == 2
    assert request('/script', data('no-wait'))[0] == 409
    accepted('no-uptime', -1)
    accepted('with-uptime')
    code, receipt = request('/script', data('with-wait'))
    assert code == 202, code
    assert request('/script', data('busy-candidate'))[0] == 409
    complete = status_when(lambda s: s['id'] == receipt['id'] and s['state'] == 'done')
    assert complete['exitCode'] == 0 and complete['highestVersion'] == 6
    print('SIGNED individual manifest grants and busy denial PASS', flush=True)
    # A rejected update must leave both the current state and flash candidate intact.
    before = request('/status')[1]
    stored = storage_snapshot()
    assert request('/script', data('tampered-source'))[0] == 403
    assert request('/status')[1] == before
    assert storage_snapshot() == stored, 'bad update modified a stored candidate'
    accepted('memory-limit', -1, runtime_outcome=4)
    accepted('after-memory')
    print('SIGNED allocation refusal is an error, followed by recovery PASS', flush=True)
    accepted('runaway', -1, True, runtime_outcome=None)
    accepted('recover')
    active = request('/status')[1]
    assert active['activeSlot'] in (0, 1)
    with open(flash_path, 'rb') as flash:
        flash.seek(0x310000 + active['activeSlot'] * 0x10000)
        assert flash.read(len(data('recover'))) == data('recover'), 'active bundle not stored intact'
    state = request('/status')[1]
    assert state['activeVersion'] == 10 and state['highestVersion'] == 10 and not state['storeFault'], state
    unchanged = storage_snapshot()
    for name in ['valid', 'recover', 'wrong-app']:
        assert request('/script', data(name))[0] == 409, name
        assert storage_snapshot() == unchanged, name
    print('SIGNED persistent version floor and application binding PASS', flush=True)
    code, receipt = request('/script', data('interrupted-trial'))
    assert code == 202
    trial = status_when(lambda s: s['trialVersion'] == 11 and s['state'] == 'running')
    assert trial['activeVersion'] == 10 and trial['highestVersion'] == 11
    reset()
    booted = status_when(lambda s: s['id'] == 0 and s['state'] == 'idle' and s['trialVersion'] == 0, limit=30)
    assert booted['activeVersion'] == 10 and booted['highestVersion'] == 11 and not booted['storeFault'], booted
    assert request('/script', data('interrupted-trial'))[0] == 409
    assert sum('SIGNED restored active v10 exit=0' in line for line in lines) >= 1
    accepted('after-reset')
    assert request('/status')[1]['activeVersion'] == 12
    print('SIGNED interrupted trial rolls back on reboot; newer update recovers PASS', flush=True)
    reset()
    healthy = status_when(lambda s: s['id']==0 and s['state']=='idle' and s['activeVersion']==12, limit=30)
    assert healthy['highestVersion']==12 and healthy['trialVersion']==0
    assert request('/script',data('after-reset'))[0]==409
    assert any('SIGNED restored active v12 exit=0' in line for line in lines)
    print('SIGNED healthy release and replay floor survive reboot PASS', flush=True)
    accepted('syntax-error', -1)
    failed = request('/status')[1]
    assert failed['activeVersion']==12 and failed['highestVersion']==13 and failed['trialVersion']==0
    assert request('/script',data('syntax-error'))[0]==409
    print('SIGNED parse failure rolls back and cannot be replayed PASS', flush=True)

    check_watch(fixtures, status_when)
    print('SIGNED rexx watch activation, rollback and corrected edit PASS', flush=True)

    accepted('maintenance-window')
    maintenance = request('/status')[1]
    assert maintenance['activeVersion'] == 17 and maintenance['highestVersion'] == 17, maintenance
    text = ''.join(lines)
    assert 'RUNNER log 2: Next maintenance window: 2026-11-09T02:30:00.000Z' in text
    assert 'maintenance schedule evaluated' not in text
    print('SIGNED maintenance schedule and log threshold PASS', flush=True)
    for marker in ['RUNNER> signed first', 'RUNNER> signed rotated', 'RUNNER> signed recovered', 'RUNNER> watch corrected',
                   'SIGNED trial test-script v1 key=fleet-current grants=0',
                   'SIGNED trial test-script v2 key=fleet-next grants=0']:
        assert marker in text, marker
    assert 'Guru Meditation' not in text and 'abort() was called' not in text
    print('SIGNED rejection isolation, cancellation and recovery PASS', flush=True)
    print('SIGNED RUNNER LANE OK', flush=True)
    print('HTTP CHECKS COMPLETE', flush=True)
