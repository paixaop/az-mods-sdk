# SPDX-License-Identifier: GPL-3.0-only
"""Run the core's production command-delivery fixture from SDK discovery."""
import os
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CommandCompletion(unittest.TestCase):
    def run_core(self, mode):
        selected = os.environ.get("AZ_CORE_TEST_ROOT")
        core = Path(selected) if selected else ROOT.parent / "xdj-az-mods"
        suite = core / "tests/sdk-command-delivery.sh"
        if not sys.platform.startswith("linux"):
            self.skipTest("command delivery requires Linux eventfd and timerfd")
        if not suite.is_file() and not selected:
            self.skipTest("core command-delivery fixture unavailable")
        self.assertTrue(suite.is_file(), f"required core command-delivery fixture missing from {core}")
        env = {**os.environ, "SAN": mode}
        result = subprocess.run(
            ["bash", str(suite.resolve())], cwd=core, env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90,
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("descriptor-driven cleanup passed", result.stdout)

    def test_production_descriptor_delivery(self):
        self.run_core("plain")

    def test_delivery_asan_ubsan(self):
        self.run_core("asan")

    def test_delivery_tsan(self):
        self.run_core("tsan")
