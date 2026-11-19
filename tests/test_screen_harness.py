# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class ScreenHarnessTests(unittest.TestCase):
    def test_dynamic_descriptor_validation(self):
        compiler = shutil.which("cc")
        if not compiler or sys.platform not in ("darwin", "linux"):
            self.skipTest("POSIX host compiler required")
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            flags = ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                     "-I" + str(root / "include")]
            link = ["-pthread", "-lm"] + (["-ldl"] if sys.platform == "linux" else [])
            binary = folder / "descriptor"
            subprocess.run([compiler, *flags, str(root / "tests/fixtures/harness-descriptor.c"),
                            *link, "-o", str(binary)], check=True)
            libraries = []
            for name, extra in [("missing", ["-DNO_ENTRY"]), ("descriptor", [])]:
                library = folder / (name + ".so")
                subprocess.run([compiler, *flags, "-shared", "-fPIC", *extra,
                                str(root / "tests/fixtures/descriptor-library.c"),
                                "-o", str(library)], check=True)
                libraries.append(str(library))
            subprocess.run([str(binary), *libraries], check=True)

    def test_legacy_descriptor_and_settings_shortcut(self):
        compiler = shutil.which("cc")
        if not compiler or sys.platform not in ("darwin", "linux"):
            self.skipTest("POSIX host compiler required")
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "screen"
            flags = ["-ldl"] if sys.platform == "linux" else []
            subprocess.run([compiler, "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(root / "include"),
                            str(root / "tests/fixtures/screen-harness.c"),
                            "-pthread", "-lm", *flags, "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
