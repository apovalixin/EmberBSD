#!/usr/bin/env python3
"""A failed UEFI rebuild must not leave a stale image input."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FirmwareFailure(unittest.TestCase):
    def test_failed_rebuild_invalidates_only_the_selected_variant(self):
        for option, variant in (("", "firmware"), ("--rp1-console", "firmware-rp1-console"),
                                ("--rp1-trace", "firmware-rp1-trace")):
            with self.subTest(option=option), tempfile.TemporaryDirectory() as temporary:
                cache = Path(temporary)
                fake = cache / "bin"
                fake.mkdir()
                docker = fake / "docker"
                docker.write_text("#!/bin/sh\nexit 1\n")
                docker.chmod(0o755)
                variants = ("firmware", "firmware-rp1-console", "firmware-rp1-trace")
                for name in variants:
                    (cache / name).mkdir()
                    (cache / name / "RPI_EFI.fd").write_bytes(b"previous firmware")
                command = ["/bin/bash", str(ROOT / "ember/build-firmware.sh"), str(cache)]
                if option:
                    command.append(option)
                result = subprocess.run(command, env=dict(os.environ, PATH=str(fake) + ":/usr/bin:/bin"),
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((cache / variant / "RPI_EFI.fd").exists(),
                                 "failed rebuild left a stale UEFI available to the image builder")
                for name in variants:
                    if name != variant:
                        self.assertEqual((cache / name / "RPI_EFI.fd").read_bytes(), b"previous firmware")


if __name__ == "__main__":
    unittest.main()
