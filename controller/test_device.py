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

    def test_repl_preserves_multiline_source_without_a_shell(self):
        with patch.object(device, "client_context"), patch.object(device, "load") as load, \
             patch.object(device.http.client, "HTTPSConnection") as connection:
            load.return_value = ("unit-device", bytes(range(32)))
            response = connection.return_value.getresponse.return_value
            response.status = 202
            response.read.return_value = b'{"sequence":1}'
            code = "x = 41\nSAY x + 1"
            result = device.repl_request("https://192.0.2.50", Path("/private/device.json"),
                                         "run", "a" * 32, 1, code)
            self.assertTrue(result["ok"])
            connection.return_value.request.assert_called_once_with(
                "POST", "/repl/command", code.encode(), {
                    "X-Repl-Session": "a" * 32, "X-Repl-Sequence": "1"})

    def test_repl_event_text_is_decoded_for_the_browser(self):
        with patch.object(device, "client_context"), patch.object(device, "load") as load, \
             patch.object(device.http.client, "HTTPSConnection") as connection:
            load.return_value = ("unit-device", bytes(range(32)))
            response = connection.return_value.getresponse.return_value
            response.status = 200
            response.read.return_value = b'{"events":[{"hex":"3432","end":true}]}'
            result = device.repl_request("https://192.0.2.50", Path("/private/device.json"),
                                         "events", "a" * 32)
            self.assertEqual(result["output"], "42\n")
            self.assertNotIn("events", result)



if __name__ == "__main__":
    unittest.main()
