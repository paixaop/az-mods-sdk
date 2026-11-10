# SPDX-License-Identifier: GPL-3.0-only
"""Mods may load settings on a host without a native cue store."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class NativeCueHarnessTests(unittest.TestCase):
    def test_native_cue_service_fails_closed(self):
        compiler = shutil.which("cc")
        if not compiler or sys.platform not in ("darwin", "linux"):
            self.skipTest("POSIX host compiler required")
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "native-cues"
            flags = ["-ldl"] if sys.platform == "linux" else []
            subprocess.run([compiler, "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(root / "include"), str(root / "tests/fixtures/native-cue-harness.c"),
                            "-pthread", "-lm", *flags, "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
