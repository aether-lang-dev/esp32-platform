"""Off-device bundle format and signing tests; firmware verification is in QEMU."""
import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from rexx_bundle import HEADER, MAGIC, fingerprint, pack, openssl

class BundleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory()
        cls.root = Path(cls.scratch.name)
        cls.key = cls.root/'key.pem'
        cls.key.write_bytes(openssl('genpkey','-algorithm','EC','-pkeyopt','ec_paramgen_curve:P-256'))
        cls.key.chmod(0o600)
        cls.public = cls.root/'public.pem'
        cls.public.write_bytes(openssl('pkey','-in',cls.key,'-pubout'))
    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()
    def bundle(self, source=b'EXIT 0\n', **options):
        args = dict(name='sensor', version=12, key_id='fleet-1', grants=['WAIT_MS'], private_key=self.key)
        args.update(options)
        return pack(source, **args)
    def test_signature_and_header_are_independently_verifiable(self):
        blob = self.bundle()
        self.assertEqual(HEADER.unpack(blob[:24]), (MAGIC,12,2,7,6,7))
        self.assertEqual(blob[24:-64], b'fleet-1sensorEXIT 0\n')
        # Independently re-encode wire integers as DER for OpenSSL's verifier.
        integers = []
        for part in [blob[-64:-32],blob[-32:]]:
            integer = part.lstrip(b'\0') or b'\0'
            if integer[0]&128: integer=b'\0'+integer
            integers.append(b'\x02'+bytes([len(integer)])+integer)
        sequence=b''.join(integers)
        signature=self.root/'signature.der'
        signature.write_bytes(b'\x30'+bytes([len(sequence)])+sequence)
        self.assertIn(b'Verified OK',openssl('dgst','-sha256','-verify',self.public,'-signature',signature,data=blob[:-64]))
        changed=bytearray(blob[:-64]);changed[8]^=2
        with self.assertRaises(subprocess.CalledProcessError):
            openssl('dgst','-sha256','-verify',self.public,'-signature',signature,data=changed)
    def test_fingerprint_ignores_version_and_signature(self):
        first = self.bundle(version=12)
        second = self.bundle(version=13)
        expected = fingerprint(b"EXIT 0\n", name="sensor", key_id="fleet-1", grants=["WAIT_MS"])
        for blob in (first, second):
            self.assertEqual(hashlib.sha256(blob[:8] + blob[12:-64]).hexdigest(), expected)

    def test_source_and_manifest_validation(self):
        for source in [b'',b'EXIT 0\0',b'x'*8193]:
            with self.subTest(source_length=len(source)),self.assertRaises(ValueError):self.bundle(source)
        for options in [dict(name='../bad'),dict(key_id=''),dict(version=0),dict(version=2**32),
                        dict(grants=['GPIO_WRITE']),dict(grants=['WAIT_MS','WAIT_MS'])]:
            with self.subTest(options=options),self.assertRaises(ValueError):self.bundle(**options)
    def test_touch_grant_composes_with_timers(self):
        blob = self.bundle(grants=["TOUCH_EVENT", "WAIT_MS"])
        self.assertEqual(struct.unpack_from("<I", blob, 12)[0], 10)

    def test_display_and_touch_grants_are_independent(self):
        blob = self.bundle(grants=["DISPLAY_BUTTONS", "TOUCH_EVENT"])
        self.assertEqual(struct.unpack_from("<I", blob, 12)[0], 24)

    def test_zero_grants_and_maximum_source(self):
        blob=self.bundle(b'x'*8192,grants=[])
        self.assertEqual(struct.unpack_from('<I',blob,12)[0],0)
        self.assertEqual(struct.unpack_from('<I',blob,20)[0],8192)
    def test_cli_defaults_to_no_grants_and_protects_private_key(self):
        source=self.root/'script.rexx';source.write_bytes(b'EXIT 0\n')
        output=self.root/'script.bundle'
        command=[sys.executable,str(Path(__file__).with_name('rexx_bundle.py')),
                 str(source),str(output),'--private-key',str(self.key),
                 '--key-id','fleet-1','--name','sensor','--version','1']
        subprocess.run(command,check=True,capture_output=True)
        self.assertEqual(struct.unpack_from('<I',output.read_bytes(),12)[0],0)
        before=self.key.read_bytes()
        command[3]=str(self.key)
        self.assertNotEqual(subprocess.run(command,capture_output=True).returncode,0)
        self.assertEqual(self.key.read_bytes(),before)

    def test_other_curves_are_refused(self):
        key=self.root/'wrong-curve.pem'
        key.write_bytes(openssl('genpkey','-algorithm','EC','-pkeyopt','ec_paramgen_curve:P-384'))
        with self.assertRaises(ValueError):self.bundle(private_key=key)

if __name__=='__main__':unittest.main()
