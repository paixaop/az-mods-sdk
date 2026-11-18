# SPDX-License-Identifier: GPL-3.0-only
"""Worker API calls must not inherit a concurrent draw callback's restrictions."""
import pathlib
import platform
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class HarnessThreads(unittest.TestCase):
    def test_worker_storage_during_draw_and_ui_refusal(self):
        compiler = shutil.which("clang")
        if not compiler:
            self.skipTest("host Clang unavailable")
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "fixture"
            libraries = ["-lm", "-pthread"] + ([] if platform.system() == "Darwin" else ["-ldl"])
            result = subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1",
                                     "-I" + str(ROOT / "include"),
                                     str(ROOT / "tests/fixtures/harness-disk-thread.c"),
                                     *libraries, "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=10)
