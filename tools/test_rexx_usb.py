import unittest
from collections import deque

from rexx_usb import UsbRunner


class FakePort:
    def __init__(self, *lines):
        self.lines = deque(lines)
        self.writes = []

    def read_until(self, separator, size):
        return self.lines.popleft()

    def write(self, data):
        self.writes.append(data)


class PeerUsbTests(unittest.TestCase):
    def test_peer_upload_ignores_app_ready_and_result(self):
        port = FakePort(b"RX ready\n", b"RX peer-ready\n",
                        b'RX result {"activeVersion":55}\n',
                        b'RX peer-result {"activeVersion":2}\n')
        self.assertEqual(UsbRunner(port).upload(b"bundle", peer=True), {"activeVersion": 2})
        self.assertEqual(port.writes, [b"RXPEERPUT 6\n", b"bundle"])

    def test_peer_status_ignores_app_state(self):
        port = FakePort(b'RX status {"highestVersion":55}\n',
                        b'RX peer-status {"highestVersion":2}\n')
        self.assertEqual(UsbRunner(port).status(peer=True), {"highestVersion": 2})
        self.assertEqual(port.writes, [b"RXPEERSTATUS\n", b"RXPEERSTATUS\n"])
