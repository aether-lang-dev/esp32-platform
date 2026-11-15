"""Independent native-adapter tests with synthetic keys only. Pass compiled probe."""
import json
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

probe = sys.argv[1]


def exercise(mode):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_3
    context.set_psk_server_callback(lambda identity: bytes([17])*32 if identity == 'native-test' else b'')
    listener = socket.socket()
    listener.settimeout(15)
    listener.bind(('127.0.0.1', 0))
    listener.listen()
    port = listener.getsockname()[1]
    stop = threading.Event()
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        credential = root/'synthetic.json'
        credential.write_text(json.dumps({'identity':'native-test','key':('22' if mode=='wrong-key' else '11')*32}))
        errors = []

        def serve():
            try:
                connection, _ = listener.accept()
                connection.settimeout(12)
                if mode == 'stall':
                    with connection:
                        stop.wait(13)
                    return
                with context.wrap_socket(connection, server_side=True) as tls:
                    assert tls.version() == 'TLSv1.3'
                    assert tls.recv(4096).startswith(b'GET /status ')
                    body = b'{"activeVersion":7,"highestVersion":8}'
                    if mode == 'oversize':
                        header = b'HTTP/1.1 200 OK\r\nContent-Length: 20000\r\n\r\n'
                    elif mode == 'redirect':
                        header = b'HTTP/1.1 302 Found\r\nContent-Length: 0\r\nLocation: http://127.0.0.1:1/\r\n\r\n'
                        body = b''
                    elif mode == 'chunked':
                        header = b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n'
                    else:
                        size = len(body)+(1 if mode=='truncated' else 0)
                        header = b'HTTP/1.1 200 OK\r\nContent-Length: '+str(size).encode()+b'\r\n\r\n'
                    tls.sendall(header+body)
            except (ssl.SSLError, OSError) as error:
                errors.append(type(error).__name__)
            finally:
                listener.close()

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        start = time.monotonic()
        result = subprocess.run([probe,str(credential),'127.0.0.1',str(port)],capture_output=True,text=True,timeout=15)
        elapsed = time.monotonic()-start
        stop.set()
        worker.join(2)
        assert not worker.is_alive()
        if mode == 'correct':
            assert result.returncode == 0 and 'active=7 floor=8' in result.stdout, (result.stdout,result.stderr,errors)
        else:
            assert result.returncode != 0, (mode,result.stdout)
        if mode == 'stall':
            assert 8 <= elapsed < 13, elapsed
            assert 'timed out' in result.stderr, result.stderr
        print('Native adapter',mode,'PASS',flush=True)


for case in ('correct','wrong-key','redirect','oversize','truncated','chunked','stall'):
    exercise(case)
