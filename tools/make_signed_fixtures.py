"""Generate disposable test keys and signed upload fixtures inside the container."""
import struct
import sys
from pathlib import Path
from rexx_bundle import pack, public_point, openssl

root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
keys = {}
header = ['/* Generated public test trust set; private keys stay in container scratch. */',
          '#include "runner_bundle.h"']
for index, name in enumerate(["fleet-current", "fleet-next", "outsider"]):
    private = root / (name + '.pem')
    private.write_bytes(openssl("genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256"))
    private.chmod(0o600)
    keys[name] = private
    if name != "outsider":
        point = public_point(private)
        header.append(f'static const unsigned char test_public_{index}[] = {{' + ','.join(hex(c) for c in point) + '};')
header.append('static const RunnerTrustedKey test_trusted_keys[] = {')
for index, name in enumerate(["fleet-current", "fleet-next"]):
    header.append(f'{{"{name}", test_public_{index}}},')
header.append('};')
Path(sys.argv[2]).write_text('\n'.join(header) + '\n')

def fixture(name, source, version, grants=(), key_id="fleet-current", signer=None):
    data = pack(source.encode(), name="test-script", version=version, key_id=key_id,
                grants=grants, private_key=keys[signer or key_id])
    (root / (name + '.bundle')).write_bytes(data)
    return data

valid = fixture('valid', "SAY 'signed first'\nEXIT 7\n", 1)
fixture('next-key', "SAY 'signed rotated'\nEXIT 0\n", 2, key_id='fleet-next')
fixture('unknown-key', 'EXIT 0\n', 3, key_id='outsider')
fixture('forged-key-id', 'EXIT 0\n', 3, signer='outsider')
fixture('no-wait', 'CALL WAIT_MS 1\nEXIT 0\n', 3)
fixture('with-wait', 'CALL WAIT_MS 1000\nEXIT 0\n', 6, ['WAIT_MS'])
fixture('no-uptime', 'x = UPTIME_MS()\nEXIT 0\n', 4, ['WAIT_MS'])
fixture('with-uptime', 'x = UPTIME_MS()\nEXIT 0\n', 5, ['UPTIME_MS'])
fixture('usb-touch-grant', 'EXIT 0\n', 3, ['TOUCH_EVENT'])
fixture('usb-display-grant', 'EXIT 0\n', 3, ['DISPLAY_BUTTONS'])
fixture('usb-address-grant', 'EXIT 0\n', 3, ['ADDRESS_LABELS'])
fixture('usb-switch-read', 'EXIT 0\n', 3, ['SWITCH_READ'])
fixture('usb-switch-control', 'EXIT 0\n', 3, ['SWITCH_CONTROL'])
fixture('usb-settings-read', 'EXIT 0\n', 3, ['SETTINGS_READ'])
fixture('usb-settings-write', 'EXIT 0\n', 3, ['SETTINGS_WRITE'])
fixture('runaway', 'DO FOREVER\nLET n = 1\nEND\n', 9)
fixture('recover', "SAY 'signed recovered'\nEXIT 0\n", 10)
# Mutate each signed field independently; lengths remain structurally valid.
for name, offset in [('magic',0), ('version',8), ('grants',12), ('key-id',24),
                     ('name',24+len('fleet-current')), ('source',len(valid)-65), ('signature',len(valid)-1)]:
    changed = bytearray(valid)
    changed[offset] ^= 2 if name == 'version' else 1
    (root/('tampered-'+name+'.bundle')).write_bytes(changed)
(root/'trailing.bundle').write_bytes(valid + b'x')
(root/'truncated.bundle').write_bytes(valid[:-1])
(root/'oversized.bundle').write_bytes(b'x' * 8409)
(root/'raw-source.bundle').write_bytes(b'EXIT 0\n')
# Correct signatures do not override the host's supported-grant policy.
message = bytearray(valid[:-64])
struct.pack_into('<I', message, 12, 4)
from rexx_bundle import raw_signature
(root/'unknown-grant.bundle').write_bytes(message + raw_signature(openssl('dgst','-sha256','-sign',keys['fleet-current'],data=message)))

# Structural length boundaries, including values that would overflow loose parsers.
for label, offset, fmt, value in [
    ('zero-key-length',16,'<H',0), ('huge-key-length',16,'<H',65535),
    ('zero-name-length',18,'<H',0), ('huge-name-length',18,'<H',65535),
    ('zero-source-length',20,'<I',0), ('huge-source-length',20,'<I',0xffffffff)]:
    changed = bytearray(valid)
    struct.pack_into(fmt, changed, offset, value)
    (root/(label+'.bundle')).write_bytes(changed)
fixture('remapped-key', 'EXIT 0\n', 3, key_id='fleet-next', signer='fleet-current')

fixture('memory-limit', "SAY LENGTH(COPIES('" + 'x'*1024 + "',300))\nEXIT 0\n", 7)
fixture('after-memory', "SAY 'after memory refusal'\nEXIT 0\n", 8)

fixture('interrupted-trial', "SAY 'trial running'\nDO i=1 TO 4\nCALL WAIT_MS 1000\nEND\nEXIT 0\n", 11, ['WAIT_MS'])
fixture('after-reset', "SAY 'after reset update'\nEXIT 0\n", 12)
(root/'wrong-app.bundle').write_bytes(pack(b'EXIT 0\n',name='other-app',version=100,key_id='fleet-current',grants=[],private_key=keys['fleet-current']))

fixture('busy-candidate', 'EXIT 0\n', 99)

fixture('syntax-error', 'IF\n', 13, ['UPTIME_MS'])

fixture('maintenance-window',
        Path('/scripts/examples/maintenance_window.rexx').read_text(), 17)

(root / 'labels-peer.bundle').write_bytes(pack(
    b'EXIT 0\n', name='peer.labels.v1', version=1, key_id='fleet-current',
    grants=[], private_key=keys['fleet-current']))
