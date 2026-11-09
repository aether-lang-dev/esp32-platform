import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from rexx_psk import load


class Credentials(unittest.TestCase):
    def test_generation_and_flash_record(self):
        with tempfile.TemporaryDirectory() as directory:
            credential = Path(directory) / "device.json"
            record = Path(directory) / "device.psk"
            tool = Path(__file__).with_name("rexx_psk.py")
            command = [sys.executable, str(tool), str(credential), str(record)]
            subprocess.run(command + ["--generate", "unit-42"], check=True)
            identity, key = load(credential)
            self.assertEqual(identity, "unit-42")
            self.assertEqual(record.read_bytes(), b"RXPSK001" + b"unit-42".ljust(65, b"\0") + key)
            original = credential.read_bytes()
            failed = subprocess.run(command + ["--generate", "other"], capture_output=True)
            self.assertNotEqual(failed.returncode, 0)
            self.assertEqual(credential.read_bytes(), original)
            self.assertEqual(record.read_bytes()[-32:], key)

    def test_invalid_credentials_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            credential = Path(directory) / "device.json"
            for identity, key in [("bad id", "21" * 32), ("unit", "21" * 31),
                                  ("unit", "00" * 32), ("unit", "ff" * 32)]:
                with self.subTest(identity=identity, length=len(key)):
                    credential.write_text(json.dumps({"identity": identity, "key": key}))
                    with self.assertRaisesRegex(ValueError, "^invalid device credential file$"):
                        load(credential)


if __name__ == "__main__":
    unittest.main()
