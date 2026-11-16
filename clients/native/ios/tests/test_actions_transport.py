"""Independent TLS1.3 peer for named ADDRESS actions; synthetic credentials only."""
import json
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

mode = sys.argv[2] if len(sys.argv)>2 else 'correct'
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_3
context.set_psk_server_callback(lambda identity: b'\x11'*32 if identity=='actions-test' else b'')
listener = socket.socket()
listener.bind(('127.0.0.1',0))
listener.listen()
listener.settimeout(1)
port = listener.getsockname()[1]
stop = threading.Event()
errors = []
commands = []
closed = False
suffix = "\nSAY 'AE_ACTION_RC=' RC\nSAY 'AE_ACTION_RESULT=' RESULT"
reads = {
    "ADDRESS APPS 'STATUS main'": '{"app":"main","name":"bench","activeVersion":6,"highestVersion":6}',
    "ADDRESS SWITCH 'STATUS'": 'OFF',
    "ADDRESS OUTLET 'STATUS'": '{"app":"outlet","state":"idle","remaining":0,"busy":false}',
}
denied = ["ADDRESS SWITCH 'ON 1'", "ADDRESS APPS 'RUN main'", "ADDRESS APPS 'CALL main outlet CANCEL'"]

def serve():
    global closed
    sequence = cursor = 0
    pending = []
    try:
        while not stop.is_set():
            try:
                raw, _ = listener.accept()
            except socket.timeout:
                continue
            with context.wrap_socket(raw,server_side=True) as tls:
                tls.settimeout(5)
                request = b''
                while b'\r\n\r\n' not in request:
                    part = tls.recv(4096)
                    assert part
                    request += part
                head, body = request.split(b'\r\n\r\n',1)
                lines = head.decode().split('\r\n')
                method, path, _ = lines[0].split(' ')
                headers = dict(line.split(': ',1) for line in lines[1:])
                size = int(headers.get('Content-Length',0))
                while len(body)<size:
                    part = tls.recv(4096)
                    assert part
                    body += part
                assert len(body)==size
                code = 200
                if path=='/repl/open':
                    assert method=='POST' and body==b'25667'
                    grants = int(body)
                    assert grants & 16384 and grants & (128|32768|2048|4096)==0
                    response = {'session':'a'*32,'sequence':0}
                    code = 201
                else:
                    assert headers['X-Repl-Session']=='a'*32
                    if path=='/repl/command':
                        assert method=='POST' and int(headers['X-Repl-Sequence'])==sequence+1
                        sequence += 1
                        cursor = 0
                        source = body.decode()
                        commands.append(source)
                        command = source.split('\n')[0]
                        if command in reads:
                            assert source == command+suffix
                            rc = 13 if mode=='denied-read' else 0
                            result = reads[command]
                            if mode=='bad-result': result = '{"app":"main","name":"bench","activeVersion":true,"highestVersion":6}'
                            pending = [f'AE_ACTION_RC= {rc}\n', f'AE_ACTION_RESULT= {result}\n']
                        else:
                            assert command in denied and source==command+"\nSAY 'CONTROL_RC=' RC"
                            pending = ['CONTROL_RC= 13\n']
                        response = {'sequence':sequence}
                        code = 202
                    elif path=='/repl/events':
                        assert method=='GET' and int(headers['X-Repl-After'])==cursor
                        text = pending.pop(0) if pending else ''
                        events = [{'kind':'stdout','end':False,'hex':text.encode().hex()}] if text else []
                        cursor += len(events)
                        response = {'sequence':sequence,'cursor':cursor,'state':'running' if pending else 'done','lost':mode=='lost-output','cancelled':False,'exitCode':0,'runtimeOutcome':7 if mode=='runtime-error' else 0,'events':events}
                    elif path=='/repl/close':
                        assert method=='POST'
                        closed = True
                        response = {'accepted':True}
                        code = 202
                    else:
                        raise AssertionError(path)
                payload = json.dumps(response).encode()
                reply = f'HTTP/1.1 {code} OK\r\nContent-Length: {len(payload)}\r\n\r\n'.encode()+payload
                # The HTTP parser must also handle an application response split
                # across TLS records, not just command output split across polls.
                tls.sendall(reply[:len(reply)//2])
                tls.sendall(reply[len(reply)//2:])
    except Exception as error:
        if not stop.is_set(): errors.append(repr(error))

worker = threading.Thread(target=serve,daemon=True)
worker.start()
with tempfile.TemporaryDirectory() as folder:
    credential = Path(folder)/'synthetic.json'
    credential.write_text(json.dumps({'identity':'actions-test','key':'11'*32}))
    result = subprocess.run([sys.argv[1],str(credential),'127.0.0.1',str(port)],capture_output=True,text=True,timeout=30)
    stop.set()
    worker.join(2)
    listener.close()
    assert not errors, errors
    if mode=='correct':
        assert result.returncode==0, (result.stdout,result.stderr)
        assert closed and len(commands)==6, (closed,commands)
        assert all(value in result.stdout for value in ('App: bench · v6 · floor 6','Relay: OFF','Countdown: idle · 0 s'))
        print('Named action round-trips, bounded grants, fragmented replies, denials and clean recall PASS')
    else:
        expected = {'denied-read':'Action rejected (RC 13)','bad-result':'Invalid app status','lost-output':'output lost','runtime-error':'did not complete successfully'}[mode]
        assert result.returncode!=0 and expected in result.stderr, (mode,result.stdout,result.stderr)
        assert len(commands)==1, commands
        print('Named action rejects',mode,'without reporting success or replaying PASS')
