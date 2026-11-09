import unittest

from rexx_bundle import P256_SPKI_PREFIX
from rexx_wifi import encode


PUBLIC = P256_SPKI_PREFIX + b"\x04" + bytes(range(64))


class WiFiRecordTests(unittest.TestCase):
    def test_firmware_field_offsets(self):
        record = encode("test-é", "example-pass", "fleet-dev", PUBLIC)
        self.assertEqual(len(record), 235)
        self.assertEqual(record[:10], b"RXWIFI01\x07\x0c")
        self.assertEqual(record[10:42], "test-é".encode().ljust(32, b"\0"))
        self.assertEqual(record[42:105], b"example-pass".ljust(63, b"\0"))
        self.assertEqual(record[105:170], b"fleet-dev".ljust(65, b"\0"))
        self.assertEqual(record[170:], PUBLIC[-65:])

    def test_maximum_lengths(self):
        record = encode("s" * 32, "p" * 63, "k" * 64, PUBLIC)
        self.assertEqual(record[8:10], bytes((32, 63)))
        self.assertEqual(record[169], 0)

    def test_invalid_fields(self):
        for args in (
            ("", "example-pass", "dev", PUBLIC),
            ("é" * 17, "example-pass", "dev", PUBLIC),
            ("bad\0ssid", "example-pass", "dev", PUBLIC),
            ("test", "short", "dev", PUBLIC),
            ("test", "é" * 10, "dev", PUBLIC),
            ("test", "example-pass", "../bad", PUBLIC),
            ("test", "example-pass", "dev", PUBLIC[:-1]),
        ):
            with self.subTest(args=args), self.assertRaises(ValueError):
                encode(*args)


if __name__ == "__main__":
    unittest.main()
