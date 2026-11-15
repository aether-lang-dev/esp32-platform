"""Verify migration preserves real firmware inputs and both controller entry paths."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class RepositoryLayoutTests(unittest.TestCase):
    def test_legacy_names_resolve_to_canonical_sources(self):
        for old, new in [("project", "firmware/esp-idf"), ("controller", "clients/web"),
                         ("in_container.sh", "firmware/ci/in_container.sh")]:
            with self.subTest(old=old):
                self.assertTrue((ROOT/old).is_symlink())
                self.assertEqual((ROOT/old).resolve(), ROOT/new)

    def test_firmware_snapshot_is_a_real_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory)/"project"
            subprocess.run(["cp", "-R", str(ROOT/"firmware/esp-idf"), str(copied)], check=True)
            self.assertFalse(copied.is_symlink())
            self.assertTrue((copied/"main/runner_s60.c").is_file())
            self.assertTrue((copied/"main/runner_outlet.c").is_file())

    def test_both_web_launch_paths_use_the_repository_root(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = Path(directory)/"ae"
            fake.write_text('#!/bin/sh\npwd -P\nprintf "%s\\n" "$@"\n')
            fake.chmod(0o755)
            env = {**os.environ, "PATH": directory+os.pathsep+os.environ["PATH"]}
            for entry in ["controller/run.sh", "clients/web/run.sh"]:
                result = subprocess.run([str(ROOT/entry), "https://example.invalid", "/synthetic/device.json"],
                                        cwd=directory, env=env, check=True, capture_output=True, text=True)
                lines = result.stdout.splitlines()
                self.assertEqual(lines[:3], [str(ROOT), "run", "clients/web/main.ae"])
                self.assertEqual(lines[-2:], ["https://example.invalid", "/synthetic/device.json"])

if __name__ == "__main__":
    unittest.main()
