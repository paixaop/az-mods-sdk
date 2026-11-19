# SPDX-License-Identifier: GPL-3.0-only
"""Coverage manifests bind fresh reports to successful runs and stable inputs."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / "tools/crap-coverage.py"


class CrapCoverageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.git_init(self.repo)
        (self.repo / ".gitignore").write_text("staged/\n")
        (self.repo / "source.c").write_text("int value(void) { return 7; }\n")
        self.out = self.root / "coverage"
        self.report = self.out / "functions.tsv"

    def git_init(self, path):
        subprocess.run(["git", "init", "-q", str(path)], check=True)

    def collect(self, script, *options):
        return subprocess.run(
            [sys.executable, str(TOOL), "--output", str(self.out),
             "--external-functions", str(self.report), *options, "--",
             sys.executable, "-c", script], cwd=self.repo, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)

    def write_report(self):
        return f"from pathlib import Path; Path({str(self.report)!r}).write_text('file\\tfunction\\tfirst\\tlast\\tlines\\tcovered\\n')"

    def manifest(self):
        return json.loads((self.out / "manifest.json").read_text())

    def test_external_harness_binds_fresh_reports_and_extra_source_roots(self):
        dependency = self.root / "dependency"
        dependency.mkdir()
        self.git_init(dependency)
        (dependency / "api.h").write_text("#define API_VERSION 1\n")
        (self.repo / "fields.def").write_text("FIELD(position)\n")
        (self.repo / "Makefile").write_text("test:\n\ttrue\n")
        result = self.collect(self.write_report(), "--source-root", "../dependency")
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = self.manifest()
        self.assertEqual(manifest["test_exit"], 0)
        self.assertTrue(manifest["source_stable"])
        self.assertEqual(manifest["functions_sha256"], hashlib.sha256(self.report.read_bytes()).hexdigest())
        self.assertEqual(set(manifest["sources"]), {"source.c", "fields.def", "Makefile", "../dependency/api.h"})

    def test_failed_harness_retains_its_failure_status(self):
        result = self.collect(self.write_report() + "; raise SystemExit(7)")
        self.assertEqual(result.returncode, 7)
        self.assertEqual(self.manifest()["test_exit"], 7)

    def test_source_mutation_during_collection_is_rejected(self):
        result = self.collect(self.write_report() + "; Path('source.c').write_text('int value(void) { return 8; }\\n')")
        self.assertEqual(result.returncode, 1)
        self.assertIn("changed during", result.stderr)
        self.assertFalse(self.manifest()["source_stable"])

    def test_staged_copy_must_match_fingerprinted_production(self):
        (self.repo / "src").mkdir()
        (self.repo / "source.c").rename(self.repo / "src/source.c")
        script = self.write_report() + "; Path('staged').mkdir(); Path('staged/source.c').write_text('int value(void) { return 8; }\\n')"
        result = self.collect(script, "--verify-mirror", "staged=src")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(self.manifest()["source_stable"])

    def test_identical_staged_copy_can_supply_coverage(self):
        (self.repo / "src").mkdir()
        (self.repo / "source.c").rename(self.repo / "src/source.c")
        script = self.write_report() + "; Path('staged').mkdir(); Path('staged/source.c').write_bytes(Path('src/source.c').read_bytes())"
        result = self.collect(script, "--verify-mirror", "staged=src")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.manifest()["source_stable"])

    def test_preexisting_external_report_is_rejected_before_running_tests(self):
        self.report = self.root / "old.tsv"
        self.report.write_text("old coverage\n")
        result = self.collect("raise SystemExit(8)")
        self.assertEqual(result.returncode, 1)
        self.assertIn("already exists", result.stderr)
        self.assertFalse((self.out / "tests.log").exists())

    def test_missing_external_report_cannot_pass(self):
        result = self.collect("pass")
        self.assertEqual(result.returncode, 1)
        self.assertIn("did not create", result.stderr)
        self.assertNotIn("functions_sha256", self.manifest())

    @unittest.skipUnless(shutil.which("clang"), "Clang is required for native coverage")
    def test_native_run_retains_binary_after_test_removes_it(self):
        # Exercise the production function, retain real instrumentation, and
        # remove the executable as temporary-directory based suites do.
        (self.repo / "main.c").write_text(
            "#include <assert.h>\nint value(void);\nint main(void) { assert(value() == 7); return 0; }\n")
        script = ("import os, pathlib, subprocess; "
                  "subprocess.run([os.environ['CC'], 'main.c', 'source.c', '-o', 'test'], check=True); "
                  "subprocess.run(['./test'], check=True); pathlib.Path('test').unlink()")
        # The collector also resolves the real compiler when this test itself
        # runs under an outer collector.
        result = subprocess.run([sys.executable, str(TOOL), "--output", str(self.out), "--",
                                 sys.executable, "-c", script], cwd=self.repo,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.repo / "test").exists())
        self.assertTrue(list((self.out / "objects").glob("*.bin")))
        report = self.out / "coverage.lcov"
        self.assertIn("source.c", report.read_text())
        self.assertIn("FNDA:1,value", report.read_text())
        self.assertEqual(self.manifest()["coverage_sha256"], hashlib.sha256(report.read_bytes()).hexdigest())

    @unittest.skipUnless(sys.platform == "linux" and shutil.which("gcc"), "GNU GCC coverage requires Linux")
    def test_gcc_shared_library_keeps_its_public_exports(self):
        # Both direct-source links and object-only links are used by mod tests.
        compiler = shutil.which("gcc", path=os.environ.get("CRAP_BASE_PATH"))
        subprocess.run([compiler, "-fPIC", "-shared", "source.c", "-o", "baseline.so"], cwd=self.repo, check=True)
        baseline = subprocess.check_output(["nm", "-D", "--defined-only", str(self.repo / "baseline.so")], universal_newlines=True)
        expected = [line.split()[-1] for line in baseline.splitlines()]
        script = ("import ctypes, os, subprocess; cc = os.environ['CC']; "
                  "subprocess.run([cc, '-fPIC', '-shared', 'source.c', '-o', 'direct.so'], check=True); "
                  "subprocess.run([cc, '-fPIC', '-c', 'source.c', '-o', 'source.o'], check=True); "
                  "subprocess.run([cc, '-shared', 'source.o', '-o', 'object.so'], check=True); "
                  "assert ctypes.CDLL('./direct.so').value() == 7; "
                  "assert ctypes.CDLL('./object.so').value() == 7")
        result = subprocess.run([sys.executable, str(TOOL), "--engine", "gcc", "--output", str(self.out), "--",
                                 sys.executable, "-c", script], cwd=self.repo,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        for library in ("direct.so", "object.so"):
            symbols = subprocess.check_output(["nm", "-D", "--defined-only", str(self.repo / library)], universal_newlines=True)
            self.assertEqual([line.split()[-1] for line in symbols.splitlines()], expected)
        self.assertIn("SF:" + str(self.repo / "source.c") + "\nDA:1,1\n", (self.out / "coverage.lcov").read_text())

    @unittest.skipUnless(sys.platform == "linux" and shutil.which("gcc"), "GNU GCC coverage requires Linux")
    def test_gcc_mixed_c_and_assembly_link_retains_c_coverage(self):
        (self.repo / "main.c").write_text(
            "#include <assert.h>\nextern int first, second;\nint value(void);\n"
            "int main(void) { assert(first + second == value()); return 0; }\n")
        for name, symbol, value in (("first.S", "first", 3), ("second.s", "second", 4)):
            (self.repo / name).write_text(".data\n.balign 4\n.globl %s\n%s: .long %d\n" % (symbol, symbol, value))
        script = ("import os, pathlib, subprocess; "
                  "subprocess.run([os.environ['CC'], 'main.c', 'source.c', 'first.S', 'second.s', '-o', 'test'], check=True); "
                  "subprocess.run(['./test'], check=True); pathlib.Path('test').unlink()")
        result = subprocess.run([sys.executable, str(TOOL), "--engine", "gcc", "--output", str(self.out), "--",
                                 sys.executable, "-c", script], cwd=self.repo,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.repo / "test").exists())
        self.assertEqual(len(list((self.out / "objects").glob("*.gcda"))), 2)
        self.assertIn("SF:" + str(self.repo / "source.c") + "\nDA:1,1\n", (self.out / "coverage.lcov").read_text())
        self.assertTrue({"first.S", "second.s"}.issubset(self.manifest()["sources"]))

    @unittest.skipUnless(sys.platform == "linux" and shutil.which("gcc"), "GNU GCC coverage requires Linux")
    def test_gcc_keeps_counters_after_temporary_objects_and_binary_are_removed(self):
        (self.repo / "main.c").write_text(
            "#include <assert.h>\nint value(void);\nint main(void) { assert(value() == 7); return 0; }\n")
        script = ("import os, pathlib, subprocess; "
                  "subprocess.run([os.environ['CC'], '-c', 'source.c', '-o', 'temporary.o'], check=True); "
                  "subprocess.run([os.environ['CC'], 'main.c', 'temporary.o', '-o', 'test'], check=True); "
                  "subprocess.run(['./test'], check=True); "
                  "pathlib.Path('test').unlink(); pathlib.Path('temporary.o').unlink()")
        result = subprocess.run([sys.executable, str(TOOL), "--engine", "gcc", "--output", str(self.out), "--",
                                 sys.executable, "-c", script], cwd=self.repo,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.repo / "test").exists())
        self.assertFalse((self.repo / "temporary.o").exists())
        self.assertEqual(len(list((self.out / "objects").glob("*.gcda"))), 2)
        coverage = (self.out / "coverage.lcov").read_text()
        self.assertIn("SF:" + str(self.repo / "source.c") + "\nDA:1,1\n", coverage)


if __name__ == "__main__":
    unittest.main()
