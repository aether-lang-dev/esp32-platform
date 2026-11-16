"""Independent TLS1.3 PSK peer exercising the real native console state machine."""
import json
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

mode = sys.argv[2] if len(sys.argv)>2 else "correct"

context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_3
context.set_psk_server_callback(lambda identity: b'\x11'*32 if identity == 'console-test' else b'')
listener = socket.socket()
listener.bind(('127.0.0.1', 0))
listener.listen()
listener.settimeout(15)
port = listener.getsockname()[1]
errors = []
commands = []
sessions = 0
sequence = cursor = 0
source = ''
cancelled = False
stream_sent = False
stopping = threading.Event()

def serve():
    global sessions, sequence, cursor, source, cancelled, stream_sent
    try:
        while not stopping.is_set():
            try:
                raw, _ = listener.accept()
            except socket.timeout:
                continue
            with context.wrap_socket(raw, server_side=True) as tls:
                tls.settimeout(10)
                request = b''
                while b'\r\n\r\n' not in request:
                    request += tls.recv(4096)
                head, body = request.split(b'\r\n\r\n', 1)
                lines = head.decode().split('\r\n')
                method, path, _ = lines[0].split(' ')
                headers = dict(line.split(': ', 1) for line in lines[1:])
                length = int(headers.get('Content-Length', 0))
                while len(body) < length:
                    body += tls.recv(4096)
                assert len(body) == length
                code = 200
                if path == '/repl/open':
                    assert method == 'POST' and body == b'25667'
                    sessions += 1
                    sequence = cursor = 0
                    response = {'session':f'{sessions:032x}', 'sequence':0}
                    code = 201
                else:
                    assert headers['X-Repl-Session'] == f'{sessions:032x}'
                    if path == '/repl/command':
                        assert method == 'POST'
                        assert int(headers['X-Repl-Sequence']) == sequence+1
                        sequence += 1
                        cursor = 0
                        source = body.decode()
                        commands.append(source)
                        cancelled = False
                        stream_sent = False
                        response = {'sequence':sequence}
                        code = 202
                    elif path == '/repl/cancel':
                        assert method == 'POST'
                        cancelled = True
                        response = {'accepted':True}
                        code = 202
                    elif path == '/repl/close':
                        assert method == 'POST'
                        response = {'accepted':True}
                        code = 202
                    elif path == '/repl/events':
                        assert method == 'GET'
                        if int(headers['X-Repl-After']) != cursor:
                            payload = b'{"error":"invalid event cursor"}'
                            tls.sendall(b'HTTP/1.1 400 Bad Request\r\nContent-Length: '+str(len(payload)).encode()+b'\r\n\r\n'+payload)
                            continue
                        running = 'DO FOREVER' in source and not cancelled
                        text = ''
                        if running and not stream_sent:
                            text = 'streaming'
                            stream_sent = True
                        elif not running:
                            if source.startswith('x = 41'): text = 'RC = RC\nRESULT = RESULT'
                            elif source.startswith('SAY x + 1'): text = '42'
                            elif source.startswith('ADDRESS APPS'): text = '{"activeVersion":6}'
                            elif source.startswith('ADDRESS SWITCH'): text = '13'
                            elif source.startswith("SAY 'recovered'"): text = 'recovered'
                            elif source == 'SAY x': text = 'X'
                        events = [{'kind':'stdout','end':True,'hex':text.encode().hex()}] if text else []
                        cursor += len(events)
                        response = {'sequence':sequence,'cursor':cursor,'state':'running' if running else 'done','events':events,'cancelled':cancelled,'lost':False,'exitCode':0,'runtimeOutcome':0}
                        if mode == 'bad-hex': response['events'] = [{'hex':'zz','end':True}]
                        if mode == 'stale-sequence': response['sequence'] = sequence-1
                        if mode == 'bad-schema': response['lost'] = {}
                    else:
                        raise AssertionError(path)
                payload = json.dumps(response).encode()
                tls.sendall(f'HTTP/1.1 {code} OK\r\nContent-Length: {len(payload)}\r\n\r\n'.encode()+payload)
    except Exception as error:
        if not stopping.is_set(): errors.append(repr(error))

worker = threading.Thread(target=serve, daemon=True)
worker.start()
with tempfile.TemporaryDirectory() as folder:
    credential = Path(folder)/'synthetic.json'
    credential.write_text(json.dumps({'identity':'console-test','key':'11'*32}))
    result = subprocess.run([sys.argv[1], str(credential),'127.0.0.1',str(port)],capture_output=True,text=True,timeout=45)
    stopping.set()
    listener.close()
    if mode != 'correct':
        expected = 'Invalid output encoding' if mode == 'bad-hex' else 'Invalid console event response'
        assert result.returncode != 0 and expected in result.stderr, (mode,result.stdout,result.stderr,errors)
        assert len(commands)==1, commands
        print('Native console rejects',mode,'without replaying the command PASS')
    else:
        assert result.returncode == 0, (result.stdout,result.stderr,errors)
        assert not errors, errors
        assert sessions == 2 and len(commands) == 8, (sessions,commands)
        assert commands[0].endswith("\nSAY 'RC =' RC\nSAY 'RESULT =' RESULT")
        assert commands[1] == 'SAY x + 1'
        print(result.stdout.splitlines()[0])
        print('Independent peer: ordered sequences, event cursors, bounded routes and RC/RESULT options PASS')
