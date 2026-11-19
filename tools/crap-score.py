#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "crapper @ git+https://github.com/unclebob/crapper.git@c757ec79a6c15dc815238bbd9c2f4601eb54d3cc",
#   "lizard==1.23.0",
# ]
# ///
# SPDX-License-Identifier: GPL-3.0-only
"""Score first-party C functions using crapper's formula and a strict CRAP < 10 gate."""
import argparse
import csv
import fnmatch
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import lizard
from crapper.crap import crap_score


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True)


def functions(path, source):
    # Lizard's C parser also handles our textual .inc fragments.
    return lizard.analyze_file.analyze_source_code(str(path) + ".c", source).function_list


def selected_files(root, config):
    tracked = git(root, "ls-files", "-z").split("\0")
    new = git(root, "ls-files", "--others", "--exclude-standard", "-z").split("\0")
    for name in sorted(set(tracked + new)):
        path = Path(name)
        if path.suffix not in (".c", ".h", ".inc") or not (root / path).is_file():
            continue
        if not any(name.startswith(prefix) for prefix in config["paths"]):
            continue
        if any(fnmatch.fnmatch(name, pattern) for pattern in config.get("exclude", [])):
            continue
        yield name


def baseline_functions(root, base, path):
    if base is None:
        return set()
    result = subprocess.run(["git", "-C", str(root), "show", f"{base}:{path}"],
                            capture_output=True, text=True)
    if result.returncode:
        return set()
    lines = result.stdout.splitlines()
    return {(f.name, "\n".join(lines[f.start_line - 1:f.end_line]))
            for f in functions(path, result.stdout)}


def source_name(name, root, mappings):
    for before, after in mappings:
        if name.startswith(before):
            name = after + name[len(before):]
            break
    path = Path(name)
    if path.is_absolute():
        try:
            return str(path.resolve().relative_to(root))
        except ValueError:
            return None
    return str(path)


def read_lcov(path, root, mappings):
    files = {}
    lines = None
    for row in path.read_text().splitlines():
        if row.startswith("SF:"):
            name = source_name(row[3:], root, mappings)
            lines = files.setdefault(name, {}) if name is not None else None
        elif row.startswith("DA:") and lines is not None:
            line, count, *_ = row[3:].split(",")
            number = int(line)
            lines[number] = max(lines.get(number, 0), int(count))
        elif row == "end_of_record":
            lines = None
    return files


def read_functions_tsv(path, root, mappings):
    rows = {}
    with path.open() as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            name = source_name(row["file"], root, mappings)
            if name is not None and int(row["lines"]):
                key = (name, row["function"], int(row["first"]), int(row["last"]))
                rows[key] = 100 * int(row["covered"]) / int(row["lines"])
    return rows


def coverage_percent(file, function, lines, totals):
    key = (file, function.name, function.start_line, function.end_line)
    if key in totals:
        return totals[key]
    # LLVM may start a function region on the final line of a multiline
    # signature. Require the same name and a range contained in this body.
    matches = [coverage for (name, symbol, first, last), coverage in totals.items()
               if name == file and symbol == function.name and
               function.start_line <= first <= last <= function.end_line]
    if len(matches) == 1:
        return matches[0]
    counts = [count for line, count in lines.get(file, {}).items()
              if function.start_line <= line <= function.end_line]
    if not counts:
        return None
    return 100 * sum(count > 0 for count in counts) / len(counts)


def analyze(root, config, lines, totals):
    rows = []
    hashes = {}
    base = config.get("base")
    if base:
        git(root, "rev-parse", "--verify", base + "^{commit}")
    for path in selected_files(root, config):
        source = (root / path).read_text()
        hashes[path] = hashlib.sha256((root / path).read_bytes()).hexdigest()
        old = baseline_functions(root, base, path)
        text = source.splitlines()
        for function in functions(path, source):
            body = "\n".join(text[function.start_line - 1:function.end_line])
            if (function.name, body) in old:
                continue
            cc = function.cyclomatic_complexity
            cov = coverage_percent(path, function, lines, totals)
            score = crap_score(cc, cov)
            upper = crap_score(cc, 0)
            rows.append(dict(file=path, function=function.name, first=function.start_line,
                             last=function.end_line, complexity=cc, coverage=cov, crap=score,
                             uncovered_upper_bound=upper,
                             fails=(score if score is not None else upper) >= config["threshold"]))
    rows.sort(key=lambda row: row["crap"] if row["crap"] is not None else row["uncovered_upper_bound"],
              reverse=True)
    return rows, hashes


def verify_manifest(path, root, lcov_paths=()):
    manifest = json.loads(path.read_text())
    if manifest.get("test_exit") != 0:
        raise ValueError("coverage came from a failed test run or has no recorded successful test status")
    if manifest.get("source_stable") is False:
        raise ValueError("source files changed during coverage collection; collect again")
    for name, expected in manifest["sources"].items():
        source = root / name
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise ValueError(f"stale coverage: {name}; collect coverage again")
    for report in lcov_paths:
        expected = manifest.get("coverage_sha256")
        if not expected or hashlib.sha256(report.read_bytes()).hexdigest() != expected:
            raise ValueError(f"coverage report does not match its manifest: {report}")


def verify_coverage_manifests(paths, root, reports, functions_tsv=None):
    manifests = []
    for path in paths:
        verify_manifest(path, root)
        manifests.append(json.loads(path.read_text()))
    for report in reports:
        digest = hashlib.sha256(report.read_bytes()).hexdigest()
        if not any(manifest.get("coverage_sha256") == digest for manifest in manifests):
            raise ValueError(f"coverage report does not match a verified manifest: {report}")
    if functions_tsv:
        digest = hashlib.sha256(functions_tsv.read_bytes()).hexdigest()
        if not any(manifest.get("functions_sha256") == digest for manifest in manifests):
            raise ValueError(f"function coverage does not match a verified manifest: {functions_tsv}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--config", type=Path)
    coverage = parser.add_mutually_exclusive_group()
    coverage.add_argument("--lcov", type=Path, action="append", default=[])
    coverage.add_argument("--functions-tsv", type=Path)
    parser.add_argument("--map", action="append", default=[], metavar="FROM=TO")
    parser.add_argument("--coverage-manifest", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--limit", type=int, default=20)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    config = json.loads((args.config or root / ".crap.json").read_text())
    verify_coverage_manifests(args.coverage_manifest, root, args.lcov, args.functions_tsv)
    mappings = [value.split("=", 1) for value in args.map]
    lines = {}
    for path in args.lcov:
        for file, counts in read_lcov(path, root, mappings).items():
            dest = lines.setdefault(file, {})
            for line, count in counts.items():
                dest[line] = max(dest.get(line, 0), count)
    totals = read_functions_tsv(args.functions_tsv, root, mappings) if args.functions_tsv else {}
    rows, hashes = analyze(root, config, lines, totals)
    failures = sum(row["fails"] for row in rows)
    missing = sum(row["coverage"] is None for row in rows)
    uncertain = sum(row["fails"] and row["coverage"] is None and row["complexity"] < config["threshold"]
                    for row in rows)
    report = dict(config=config, head=git(root, "rev-parse", "HEAD").strip(),
                  sources=hashes, functions=rows, failures=failures, missing_coverage=missing,
                  proven_failures=failures - uncertain, insufficient_coverage=uncertain)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{root.name}: {len(rows)} functions, {failures} gate failures for CRAP < {config['threshold']} "
          f"({failures - uncertain} proven, {uncertain} need coverage); {missing} unmeasured")
    for row in rows[:args.limit]:
        cov = "N/A" if row["coverage"] is None else f"{row['coverage']:.1f}%"
        score = (f"<= {row['uncovered_upper_bound']:.3f} (unmeasured)" if row["crap"] is None
                 else f"{row['crap']:.3f}")
        print(f"CC {row['complexity']:3} coverage {cov:>6} CRAP {score:>8}  "
              f"{row['file']}:{row['first']} {row['function']}")
    return 2 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
