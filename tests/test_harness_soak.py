# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the complete harness soak and prove its behavior checks can fail."""
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class HarnessSoakTests(unittest.TestCase):
    def test_seeded_soaks_and_failure_controls(self):
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("host compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            host = root / "soak"
            flags = ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                     "-I" + str(ROOT / "include")]
            link = ["-pthread", "-lm"] + (["-ldl"] if sys.platform == "linux" else [])
            subprocess.run([cc, *flags, "-rdynamic", str(ROOT / "tests/fixtures/harness-soak.c"),
                            *link, "-o", str(host)], check=True, capture_output=True, text=True)
            shared = ["-shared", "-fPIC", "-fno-optimize-sibling-calls"]
            if sys.platform == "darwin":
                shared += ["-flat_namespace", "-undefined", "dynamic_lookup"]
            for fault, check in [(0, None), (7, "redraw-rate"), (8, "log-quiet")]:
                with self.subTest(fault=fault):
                    mod = root / (str(fault) + ".so")
                    subprocess.run([cc, *flags, *shared, "-DFAULT=" + str(fault),
                                    str(ROOT / "tools/harness/planted.c"), *link, "-o", str(mod)],
                                   check=True, capture_output=True, text=True)
                    result = subprocess.run([str(host), str(mod), "planted"], capture_output=True,
                                            text=True, timeout=30)
                    self.assertEqual(result.returncode, int(fault != 0), result.stdout + result.stderr)
                    self.assertEqual(result.stdout.count("  soak "), 6)
                    if check:
                        self.assertIn("CHECK " + check + " FAIL", result.stdout)
                    else:
                        self.assertNotIn(" FAIL", result.stdout)


if __name__ == "__main__":
    unittest.main()
