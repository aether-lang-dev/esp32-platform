import unittest
from pathlib import Path
from unittest.mock import patch
import device


class DeviceStatusTests(unittest.TestCase):
    def test_status_does_not_forward_unexpected_device_fields(self):
        with patch.object(device, "Runner") as runner:
            runner.return_value.status.return_value = {
                "state": "idle", "activeVersion": 4, "highestVersion": 5,
                "trialVersion": 0, "exitCode": 0, "credential": "unexpected-secret",
            }
            result = device.read_status("https://192.0.2.50", Path("/private/device.json"))
        self.assertEqual(result, {"connected": True, "message": "", "state": "idle",
                                 "active": 4, "floor": 5, "trial": 0, "exit": 0})
        self.assertNotIn("unexpected-secret", str(result))

    def test_plaintext_endpoint_is_refused_before_reading_a_key(self):
        with patch.object(device, "Runner") as runner:
            with self.assertRaisesRegex(ValueError, "HTTPS"):
                device.read_status("http://192.0.2.50", Path("/private/device.json"))
            runner.assert_not_called()


if __name__ == "__main__":
    unittest.main()
