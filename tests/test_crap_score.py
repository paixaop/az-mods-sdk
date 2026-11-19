# SPDX-License-Identifier: GPL-3.0-only
"""Keep the C metric gate honest at coverage and baseline boundaries."""
import contextlib
import io
import importlib.util
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

try:
    import lizard
    from crapper.crap import crap_score
except ImportError:
    lizard = None


def load_tool():
    path = Path(__file__).resolve().parents[1] / "tools/crap-score.py"
    spec = importlib.util.spec_from_file_location("crap_score_tool", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@unittest.skipIf(lizard is None, "run with the pinned crap-score.py dependencies")
class CrapScoreTests(unittest.TestCase):
    def setUp(self):
        self.tool = load_tool()
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        self.git("init", "-q")
        self.git("config", "user.name", "Test")
        self.git("config", "user.email", "test@example.invalid")
        self.config = dict(base=None, threshold=10, paths=["src/"], exclude=["src/vendor/*"])

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], text=True)

    def source(self, name, source):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source)

    def rows(self, lines=None):
        return self.tool.analyze(self.root, self.config, lines or {}, {})[0]

    def test_unmeasured_is_not_presented_as_zero_or_full_coverage(self):
        self.source("src/a.c", "int f(int a) { if(a) return 1; if(a>2) return 3; return 0; }\n")
        row, = self.rows()
        self.assertIsNone(row["coverage"])
        self.assertIsNone(row["crap"])
        self.assertEqual(row["uncovered_upper_bound"], 12)
        self.assertTrue(row["fails"])

    def test_complete_coverage_does_not_hide_complexity_at_threshold(self):
        self.source("src/a.c", "int f(int a) { " + " ".join(f"if(a=={i}) return {i};" for i in range(9)) + " return -1; }\n")
        row, = self.rows({"src/a.c": {1: 1}})
        self.assertEqual(row["crap"], 10)
        self.assertTrue(row["fails"])

    def test_low_complexity_upper_bound_can_prove_pass_without_coverage(self):
        self.source("src/a.c", "int f(int a) { if(a) return 1; return 0; }\n")
        row, = self.rows()
        self.assertFalse(row["fails"])
        self.assertEqual(row["uncovered_upper_bound"], 6)

    def test_baseline_filters_functions_not_entire_modified_files(self):
        original = "int old(void) { return 0; }\nint edit(void) { return 1; }\n"
        self.source("src/a.c", original)
        self.git("add", ".")
        self.git("commit", "-qm", "baseline")
        self.config["base"] = self.git("rev-parse", "HEAD").strip()
        self.source("src/a.c", original.replace("return 1", "return 2") + "int added(void) { return 3; }\n")
        self.assertEqual({row["function"] for row in self.rows()}, {"edit", "added"})

    def test_invalid_baseline_fails_instead_of_scanning_everything(self):
        self.config["base"] = "missing-baseline"
        with self.assertRaises(subprocess.CalledProcessError):
            self.rows()

    def test_vendor_exclusion_and_untracked_new_functions(self):
        self.source("src/a.c", "int ours(void) { return 1; }\n")
        self.source("src/vendor/a.h", "int theirs(void) { return 1; }\n")
        self.assertEqual([row["function"] for row in self.rows()], ["ours"])

    def test_lcov_unions_repeated_translation_units_and_ignores_foreign_paths(self):
        path = self.root / "cov.lcov"
        path.write_text("SF:/staged/guest/a.c\nDA:1,0\nDA:2,1\nend_of_record\n"
                        "SF:/staged/guest/a.c\nDA:1,2\nDA:2,0\nend_of_record\n"
                        "SF:/tmp/mutant/a.c\nDA:1,100\nend_of_record\n")
        lines = self.tool.read_lcov(path, self.root, [("/staged/guest/", "src/")])
        self.assertEqual(lines, {"src/a.c": {1: 2, 2: 1}})

    def test_coverage_uses_only_executable_lines_inside_function(self):
        self.source("src/a.c", "int f(int a) {\n if (a) return 1;\n return 0;\n}\n")
        row, = self.rows({"src/a.c": {2: 1, 3: 0, 100: 1}})
        self.assertEqual(row["coverage"], 50)
        self.assertEqual(row["crap"], 2.5)

    def test_llvm_region_can_start_on_last_signature_line(self):
        source = "int f(\n int a)\n{\n return a;\n}\n"
        function, = self.tool.functions("a.c", source)
        totals = {("a.c", "f", 2, 5): 75.0}
        self.assertEqual(self.tool.coverage_percent("a.c", function, {}, totals), 75.0)
        self.assertIsNone(self.tool.coverage_percent("a.c", function, {}, {("a.c", "f", 6, 9): 100.0}))

    def test_aggregate_percentages_cannot_override_line_coverage(self):
        # TSV lacks line identities, so its denominator/overlap cannot be
        # safely combined with LCOV from another platform or configuration.
        error = io.StringIO()
        with contextlib.redirect_stderr(error), self.assertRaises(SystemExit) as result:
            self.tool.main(["--functions-tsv", str(self.root / "host.tsv"),
                            "--lcov", str(self.root / "native.lcov"),
                            "--output", str(self.root / "report.json")])
        self.assertEqual(result.exception.code, 2)
        self.assertIn("not allowed with argument", error.getvalue())
        self.assertFalse((self.root / "report.json").exists())

    def test_failed_test_run_cannot_supply_passing_coverage(self):
        manifest = self.root / "manifest.json"
        manifest.write_text(json.dumps(dict(sources={}, test_exit=1)))
        with self.assertRaisesRegex(ValueError, "failed test run"):
            self.tool.verify_manifest(manifest, self.root)

    def test_missing_test_status_and_unstable_inputs_cannot_supply_coverage(self):
        manifest = self.root / "manifest.json"
        for fields in ({}, {"test_exit": 0, "source_stable": False}):
            manifest.write_text(json.dumps(dict(sources={}, **fields)))
            with self.assertRaises(ValueError):
                self.tool.verify_manifest(manifest, self.root)

    def test_stale_coverage_cannot_be_reused_after_refactoring(self):
        self.source("src/a.c", "int f(void) { return 1; }\n")
        _, hashes = self.tool.analyze(self.root, self.config, {}, {})
        manifest = self.root / "manifest.json"
        manifest.write_text(json.dumps(dict(sources=hashes, test_exit=0)))
        self.tool.verify_manifest(manifest, self.root)
        self.source("src/a.c", "int f(void) { return 2; }\n")
        with self.assertRaisesRegex(ValueError, "stale coverage"):
            self.tool.verify_manifest(manifest, self.root)

    def test_manifest_is_bound_to_the_actual_coverage_report(self):
        report = self.root / "coverage.lcov"
        report.write_text("SF:src/a.c\nDA:1,0\nend_of_record\n")
        manifest = self.root / "manifest.json"
        manifest.write_text(json.dumps(dict(sources={}, test_exit=0,
                                            coverage_sha256=hashlib.sha256(report.read_bytes()).hexdigest())))
        self.tool.verify_manifest(manifest, self.root, [report])
        report.write_text("SF:src/a.c\nDA:1,1\nend_of_record\n")
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.tool.verify_manifest(manifest, self.root, [report])

    def test_cross_platform_reports_each_require_their_own_verified_manifest(self):
        reports, manifests = [], []
        for platform in ("mac", "linux"):
            report = self.root / (platform + ".lcov")
            report.write_text("SF:src/a.c\nDA:1," + str(len(platform)) + "\nend_of_record\n")
            manifest = self.root / (platform + ".json")
            manifest.write_text(json.dumps(dict(sources={}, test_exit=0,
                coverage_sha256=hashlib.sha256(report.read_bytes()).hexdigest())))
            reports.append(report)
            manifests.append(manifest)
        self.tool.verify_coverage_manifests(manifests, self.root, reports)
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.tool.verify_coverage_manifests(manifests[:1], self.root, reports)
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.tool.verify_coverage_manifests([], self.root, reports)

    def test_function_coverage_cannot_bypass_manifest_checks(self):
        report = self.root / "functions.tsv"
        report.write_text("file\tfunction\tfirst\tlast\tlines\tcovered\n")
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.tool.verify_coverage_manifests([], self.root, [], report)


if __name__ == "__main__":
    unittest.main()
