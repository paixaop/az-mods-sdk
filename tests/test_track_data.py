# SPDX-License-Identifier: GPL-3.0-only
"""The SDK harness must exercise the new public data contract without native firmware."""
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


class TrackDataHarnessTests(unittest.TestCase):
    def setUp(self):
        if sys.platform not in ("linux", "darwin"):
            self.skipTest("POSIX harness requires Linux or macOS")
        self.compiler = shutil.which("cc")
        if not self.compiler:
            self.skipTest("C compiler unavailable")
        self.root = pathlib.Path(__file__).resolve().parents[1]

    def compile_fixture(self, source, binary, extra_flags=(), compiler=None):
        subprocess.run(
            [compiler or self.compiler, "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
             *extra_flags, "-I" + str(self.root / "include"), str(source),
             "-pthread", "-lm", "-o", str(binary)] + (["-ldl"] if sys.platform == "linux" else []),
            check=True, capture_output=True, text=True)

    def test_synthetic_provider_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "track-data"
            self.compile_fixture(self.root / "tests/fixtures/harness-track-data.c", binary)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)

    def test_loop_watch_declares_the_status_service_minimum(self):
        manifest = json.loads((self.root / "examples/loop-watch/azmod.json").read_text())
        header = (self.root / "include/azmod-mod.h").read_text()
        minimum = re.search(r'^#define AZ_DECK_STATUS_MIN_CORE "([0-9.]+)"', header, re.MULTILINE)
        self.assertIsNotNone(minimum)
        self.assertEqual(manifest["min_core"], minimum.group(1))

    def test_deck_status_clients_use_the_same_callback_contract(self):
        cxx = shutil.which("c++")
        if not cxx:
            self.skipTest("C++14 compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            host = pathlib.Path(directory) / "status-host"
            client = pathlib.Path(directory) / "status-client.so"
            fixture = self.root / "tests/fixtures/deck-status-client.c"
            self.compile_fixture(self.root / "tests/fixtures/harness-status-client.c", host)
            transcripts = []
            for compiler, language, standard in [(self.compiler, "c", "c11"), (cxx, "c++", "c++14")]:
                for flags in [[], ["-DWITH_HELPERS"]]:
                    with self.subTest(language=language, helpers=bool(flags)):
                        subprocess.run([compiler, "-x", language, "-std=" + standard, "-Wall", "-Wextra",
                                        "-Werror", "-O2", "-fPIC", "-fno-optimize-sibling-calls", "-shared",
                                        "-I" + str(self.root / "include"), *flags, str(fixture), "-o", str(client)],
                                       check=True, capture_output=True, text=True)
                        run = subprocess.run([str(host), str(client)], capture_output=True, text=True)
                        self.assertEqual(run.returncode, 0, run.stderr)
                        transcripts.append(run.stdout.splitlines())
            for rows in transcripts:
                self.assertEqual(rows, transcripts[0])
                self.assertEqual(rows.count("released"), 1)
                events = [row.split() for row in rows if row.startswith("event ")]
                self.assertEqual([int(row[3]) for row in events], [-1, 0, 0, 1, 0, 1, 1, 1])
                self.assertEqual([row[4] for row in events[2:]], ["0.0"] * 6)
                self.assertEqual(sum(row[5] != "-1" for row in events), 2)

    def test_sanitized_provider_contract(self):
        compiler = shutil.which("clang")
        if not compiler:
            self.skipTest("Clang is required for short-object UBSan coverage")
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "track-data-asan"
            self.compile_fixture(self.root / "tests/fixtures/harness-track-data.c", binary,
                                 ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"], compiler)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)

    def test_regression_controls(self):
        original = (self.root / "tools/harness/track-data.h").read_text()
        controls = [
            ("track identity", "ht_request", "!ref->source_id ||", "0 ||"),
            ("waveform identity", "ht_wave_request", "!ref->source_id ||", "0 ||"),
            ("track stored size", "ht_request", "v->track.size = sizeof v->track;", ""),
            ("waveform stored size", "ht_wave_request", "v->track.size = sizeof v->track;", ""),
            ("command envelope", "ht_command", "!r->request_id ||", "0 ||"),
            ("packed waveform offset", "ht_wave_request", "q->first_column * v->bytes_per_column", "q->first_column"),
            ("command finite arguments", "ht_command", "!isfinite(r->position_s)", "0"),
            ("command flags", "ht_command", "(r->flags & ~(AZ_COMMAND_PERSIST | AZ_COMMAND_NATIVE_LOAD | AZ_COMMAND_NATIVE_CUE_IDS))", "0"),
            ("unknown quantize", "ht_deck", "v.quantize =", ""),
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            harness = root / "tools/harness"
            harness.mkdir(parents=True)
            for filename in ("harness.c", "track-timing.h", "deck-status.h"):
                shutil.copyfile(self.root / "tools/harness" / filename, harness / filename)
            fixture = root / "tests/fixtures/harness-track-data.c"
            fixture.parent.mkdir(parents=True)
            shutil.copyfile(self.root / "tests/fixtures/harness-track-data.c", fixture)
            for label, function, old, new in controls:
                with self.subTest(control=label):
                    start = original.index("static int " + function + "(")
                    end = original.index("\n}", start)
                    body = original[start:end]
                    self.assertEqual(body.count(old), 1)
                    (harness / "track-data.h").write_text(original[:start] + body.replace(old, new) + original[end:])
                    binary = root / "control"
                    self.compile_fixture(fixture, binary)
                    result = subprocess.run([str(binary)], capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0, label)
                    self.assertIn("assert", result.stderr.lower(), result.stderr)
            compiler = shutil.which("clang")
            if compiler:
                with self.subTest(control="short snapshot prefix"):
                    (harness / "track-data.h").write_text(original)
                    old = "memcpy(&size, record, sizeof size);"
                    self.assertEqual(original.count(old), 1)
                    (harness / "track-data.h").write_text(original.replace(old, "size = ((const az_track_snapshot *) record)->size;"))
                    binary = root / "prefix-control"
                    self.compile_fixture(fixture, binary, ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"], compiler)
                    result = subprocess.run([str(binary)], capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("runtime error:", result.stderr, result.stderr)
