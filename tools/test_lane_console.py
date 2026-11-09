"""Check the real lane validator against successful and misleading logs."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

class ConsoleTests(unittest.TestCase):
    def validate(self, log, status='0', program='runner-signed', marker='SIGNED RUNNER LANE OK'):
        lane=(Path(__file__).resolve().parents[1]/'lane.sh').read_text()
        start=lane.index('check() {')
        function=lane[start:lane.index('\nfor p in $want; do',start)]
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'out';out.mkdir()
            (out/(program+'.log')).write_text(log)
            (out/(program+'.build.log')).write_text('aether_s3.bin binary size 0x1000 bytes\n')
            if status is not None:(out/(program+'.harness-status')).write_text(status+'\n')
            script='set -eu\npass=0; fail=0\nok() { pass=$((pass+1)); }\nbad() { fail=$((fail+1)); }\n'+function
            script+='\ncheck "$PROGRAM" "$MARKER"\nprintf "\\nCOUNTS=%s,%s\\n" "$pass" "$fail"\n'
            env=dict(os.environ,tmp=folder,TARGET='esp32',PROGRAM=program,MARKER=marker)
            result=subprocess.run(['sh','-c',script],env=env,text=True,capture_output=True,check=True)
            return result.stdout.rsplit('COUNTS=',1)[1].strip()
    def test_success_requires_both_markers_and_zero_harness_exit(self):
        log='Calling app_main\nSIGNED RUNNER LANE OK\nHTTP CHECKS COMPLETE\n'
        self.assertEqual(self.validate(log),'1,0')
        self.assertEqual(self.validate(log,status='1'),'0,1')
        self.assertEqual(self.validate(log,status=None),'0,1')
    def test_guest_prefix_and_panic_cannot_pass(self):
        log='Calling app_main\nRUNNER> SIGNED RUNNER LANE OK\nHTTP CHECKS COMPLETE\n'
        self.assertEqual(self.validate(log),'0,1')
        log='Calling app_main\nSIGNED RUNNER LANE OK\nGuru Meditation\nHTTP CHECKS COMPLETE\n'
        self.assertEqual(self.validate(log),'0,1')
    def test_plain_firmware_needs_no_http_harness(self):
        self.assertEqual(self.validate('Calling app_main\nHello, Aether!\nReturned from app_main\n',
                                      status=None,program='hello',marker='Hello, Aether!'),'1,0')

if __name__=='__main__':unittest.main()
