"""Independent bounded-control peer; this never connects to a real outlet."""
import json
import math
import os
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

mode = sys.argv[2] if len(sys.argv)>2 else 'correct'
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_3
context.set_psk_server_callback(lambda identity: b'\x11'*32 if identity=='control-test' else b'')
listener = socket.socket()
listener.bind(('127.0.0.1',0))
listener.listen()
listener.settimeout(1)
port = listener.getsockname()[1]
stop = threading.Event()
commands, errors = [], []
opens = closes = 0
until = 0
suffix = "\nSAY 'AE_ACTION_RC=' RC\nSAY 'AE_ACTION_RESULT=' RESULT"

def serve():
    global opens, closes, until
    sequence = 0
    cursor = 0
    response_text = ''
    try:
        while not stop.is_set():
            try: raw, _ = listener.accept()
            except socket.timeout: continue
            with context.wrap_socket(raw,server_side=True) as tls:
                tls.settimeout(5)
                data = b''
                while b'\r\n\r\n' not in data:
                    part = tls.recv(4096)
                    assert part
                    data += part
                head, body = data.split(b'\r\n\r\n',1)
                lines = head.decode().split('\r\n')
                method, route, _ = lines[0].split(' ')
                headers = dict(line.split(': ',1) for line in lines[1:])
                length = int(headers.get('Content-Length',0))
                while len(body)<length:
                    part = tls.recv(4096)
                    assert part
                    body += part
                assert len(body)==length
                code = 200
                if route=='/repl/open':
                    assert method=='POST' and body==b'25667'
                    opens += 1
                    sequence = 0
                    result = {'session':f'{opens:032x}','sequence':0}
                    code = 201
                else:
                    assert headers['X-Repl-Session']==f'{opens:032x}'
                    if route=='/repl/command':
                        assert method=='POST' and int(headers['X-Repl-Sequence'])==sequence+1
                        sequence += 1
                        cursor = 0
                        source = body.decode()
                        commands.append(source)
                        command = source.split('\n')[0]
                        assert command in ("ADDRESS OUTLET 'CANCEL'","ADDRESS OUTLET 'START 10'","ADDRESS OUTLET 'START 30'","ADDRESS OUTLET 'STATUS'","ADDRESS SWITCH 'STATUS'","ADDRESS SWITCH 'ON 1'")
                        if command=="ADDRESS SWITCH 'ON 1'":
                            assert source==command+"\nSAY 'CONTROL_RC=' RC"
                            response_text='CONTROL_RC= 13\n'
                        else:
                            assert source==command+suffix
                            if command=="ADDRESS OUTLET 'CANCEL'": until=0
                            if command.startswith("ADDRESS OUTLET 'START "):
                                seconds = 10 if '10' in command else 30
                                until = time.monotonic()+seconds
                                if mode=='drop-on-reply': continue
                            remaining = max(0,math.ceil(until-time.monotonic()))
                            value = ('ON' if remaining else 'OFF') if command=="ADDRESS SWITCH 'STATUS'" else json.dumps({'app':'outlet','state':'running' if remaining else 'idle','remaining':remaining,'busy':False})
                            response_text=f'AE_ACTION_RC= 0\nAE_ACTION_RESULT= {value}\n'
                        result = {'sequence':sequence}
                        code = 202
                    elif route=='/repl/events':
                        assert method=='GET' and int(headers['X-Repl-After'])==cursor
                        cursor = 1
                        result = {'sequence':sequence,'cursor':cursor,'state':'done','lost':False,'cancelled':False,'exitCode':0,'runtimeOutcome':0,'events':[{'kind':'stdout','hex':response_text.encode().hex(),'end':False}]}
                    elif route=='/repl/close':
                        assert method=='POST'
                        closes += 1
                        if closes==1: assert until>time.monotonic(), 'Session close must not send Off'
                        result = {'accepted':True}
                        code = 202
                    else: raise AssertionError(route)
                payload = json.dumps(result).encode()
                tls.sendall(f'HTTP/1.1 {code} OK\r\nContent-Length: {len(payload)}\r\n\r\n'.encode()+payload)
    except Exception as error:
        if not stop.is_set(): errors.append(repr(error))

worker = threading.Thread(target=serve,daemon=True)
worker.start()
with tempfile.TemporaryDirectory() as folder:
    credential = Path(folder)/'synthetic.json'
    credential.write_text(json.dumps({'identity':'control-test','key':'11'*32}))
    result = subprocess.run([sys.argv[1],str(credential),'127.0.0.1',str(port)],env={**os.environ,'ALLOW_LIVE_OUTLET_CONTROL':'1'},capture_output=True,text=True,timeout=30)
    stop.set(); worker.join(2); listener.close()
    assert not errors, errors
    starts = [value for value in commands if "'START " in value]
    if mode=='correct':
        assert result.returncode==0, (result.stdout,result.stderr)
        assert len(starts)==2 and closes==2 and until==0, (commands,closes,until)
        print('Timed controls, independent expiry after close, explicit Off and direct-relay denial PASS')
    else:
        assert result.returncode!=0 and len(starts)==1, (result.stdout,result.stderr,commands)
        print('Lost On acknowledgement is not retried or reported as success PASS')
