#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run C tests with compiler coverage and retain their temporary build artifacts.

Usage: crap-coverage.py --output /new/report/directory -- make unit
Run from the repository whose sources should be fingerprinted. Commands that
invoke cc, gcc or clang through PATH (or $CC/$TEST_CC/$HOST_CC) are instrumented.
Use --external-functions or --external-lcov for an existing coverage harness:
its command runs unchanged, and freshly created reports are bound to the run.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid


def compiler():
    args = sys.argv[2:]
    if os.environ.get("CRAP_ENGINE") == "gcc":
        return gcc_compile(args)
    status = subprocess.call([os.environ["CRAP_CLANG"], *args,
                              "-fprofile-instr-generate", "-fcoverage-mapping"])
    if status or "-c" in args or "-E" in args or "-S" in args or "-o" not in args:
        return status
    binary = Path(args[args.index("-o") + 1])
    if binary.is_file() and binary.stat().st_size:
        shutil.copy2(binary, Path(os.environ["CRAP_ARTIFACTS"]) / (uuid.uuid4().hex + ".bin"))
    return status


def gcc_compile(args):
    """Keep GCC's objects/counters alive when fixtures remove their build dirs."""
    real = os.environ["CRAP_GCC"]
    if "-E" in args or "-S" in args:
        return subprocess.call([real, *args])
    output = None
    sources, rest = [], []
    i = 0
    while i < len(args):
        arg = args[i]
        if arg == "-o":
            i += 1
            output = args[i]
        elif arg.startswith("-o") and len(arg) > 2:
            output = arg[2:]
        elif not arg.startswith("-") and Path(arg).suffix in (".c", ".cc", ".cpp", ".cxx", ".S", ".s"):
            sources.append(str(Path(arg).resolve()))
        else:
            rest.append(arg)
        i += 1
    flags = ["--coverage", "-O0", "-fprofile-update=atomic"]
    # Instrumentation must not add public symbols to a plugin's ABI.
    link_flags = ["-Wl,--exclude-libs,libgcov.a"] if "-shared" in args else []
    if not sources or output is None:
        return subprocess.call([real, *args, "--coverage", *link_flags])
    if "-c" in rest and len(sources) != 1:
        return subprocess.call([real, *args, *flags])
    objects = []
    for source in sources:
        retained = Path(os.environ["CRAP_ARTIFACTS"]) / (uuid.uuid4().hex + ".o")
        status = subprocess.call([real, *rest, *flags, "-c", source, "-o", str(retained)])
        if status:
            return status
        objects.append(str(retained))
    if "-c" in rest:
        shutil.copy2(objects[0], output)
        return 0
    return subprocess.call([real, *objects, *rest, *flags, *link_flags, "-o", output])


def gcov_lines(path):
    source, lines = None, {}
    for row in path.read_text(errors="replace").splitlines():
        parts = row.split(":", 2)
        if len(parts) != 3 or not parts[1].strip().isdigit():
            continue
        number = int(parts[1])
        if number == 0:
            if parts[2].startswith("Source:"):
                source = parts[2][7:]
            continue
        count = parts[0].strip().rstrip("*")
        if count == "-":
            continue
        lines[number] = 0 if count in ("#####", "=====") else int(count)
    return source, lines


def export_gcc(out, binaries):
    notes = sorted(binaries.glob("*.gcno"))
    if not notes:
        raise ValueError("no GCC coverage collected; see tests.log")
    files = {}
    gcov = os.environ.get("CRAP_GCOV") or tool("gcov")
    with (out / "export.log").open("w") as log:
        for note in notes:
            directory = out / "gcov" / note.stem
            directory.mkdir(parents=True)
            subprocess.run([gcov, "-p", "-b", "-c", "-o", str(note.parent), str(note.with_suffix(".o"))],
                           cwd=directory, stdout=log, stderr=subprocess.STDOUT, check=True)
            for report in directory.glob("*.gcov"):
                source, counts = gcov_lines(report)
                if source is None:
                    continue
                lines = files.setdefault(source, {})
                for number, count in counts.items():
                    lines[number] = max(lines.get(number, 0), count)
    report = out / "coverage.lcov"
    with report.open("w") as stream:
        for source, lines in sorted(files.items()):
            stream.write("SF:" + source + "\n")
            for line, count in sorted(lines.items()):
                stream.write("DA:%d,%d\n" % (line, count))
            stream.write("end_of_record\n")
    return len(notes)


def tool(name):
    found = shutil.which(name, path=os.environ.get("CRAP_BASE_PATH"))
    if found:
        return found
    return subprocess.check_output(["xcrun", "--find", name], universal_newlines=True).strip()


def fingerprint(root):
    files = subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                                    cwd=root, universal_newlines=True).split("\0")
    return {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
            for name in sorted(set(files))
            if (Path(name).suffix in (".c", ".h", ".inc", ".def", ".cc", ".cpp", ".cxx", ".hpp", ".py", ".sh", ".S", ".s", ".mk")
                or Path(name).name in ("Makefile", "CMakeLists.txt")) and (root / name).is_file()}


def source_snapshot(root, dependencies):
    sources = fingerprint(root)
    for dependency in dependencies:
        prefix = os.path.relpath(dependency.resolve(), root)
        sources.update({str(Path(prefix) / name): digest for name, digest in fingerprint(dependency).items()})
    return sources


def sources_unchanged(root, args, sources):
    if sources != source_snapshot(root, args.source_root):
        return False
    for value in args.verify_mirror:
        mirror, source = value.split("=", 1)
        prefix = source.rstrip("/") + "/"
        selected = {name: digest for name, digest in sources.items() if name.startswith(prefix)}
        if not selected:
            raise ValueError(f"source mirror has no fingerprinted inputs: {value}")
        for name, expected in selected.items():
            staged = root / mirror / name[len(prefix):]
            if not staged.is_file() or hashlib.sha256(staged.read_bytes()).hexdigest() != expected:
                return False
    return True


def write_manifest(out, sources, command, status, stable, reports=()):
    manifest = dict(sources=sources, command=command, test_exit=status, source_stable=stable)
    for key, report in reports:
        manifest[key] = hashlib.sha256(report.read_bytes()).hexdigest()
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def external_reports(args, command, root, sources):
    reports = [(key, path.resolve()) for key, path in (
        ("functions_sha256", args.external_functions), ("coverage_sha256", args.external_lcov)) if path]
    for _, path in reports:
        if path.exists():
            raise ValueError(f"external report already exists: {path}; collect into a fresh directory")
    with (args.output / "tests.log").open("w") as log:
        status = subprocess.call(command, stdout=log, stderr=subprocess.STDOUT)
    stable = sources_unchanged(root, args, sources)
    present = [(key, path) for key, path in reports if path.is_file()]
    write_manifest(args.output, sources, command, status, stable, present)
    if not stable:
        raise ValueError("source files changed during coverage collection; collect again")
    if len(present) != len(reports):
        raise ValueError(f"test exit {status}: external coverage command did not create every requested report")
    print(f"Test exit {status}; {len(reports)} external reports; manifest {args.output / 'manifest.json'}")
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--engine", choices=("clang", "gcc"), default="clang",
                        help="compiler coverage implementation (default: clang)")
    parser.add_argument("--external-functions", type=Path, help="fresh functions.tsv generated by the test command")
    parser.add_argument("--external-lcov", type=Path, help="fresh LCOV generated by the test command")
    parser.add_argument("--source-root", type=Path, action="append", default=[],
                        help="additional Git source root to fingerprint, e.g. ../az-mods-sdk (repeatable)")
    parser.add_argument("--verify-mirror", action="append", default=[], metavar="STAGED=SOURCE",
                        help="verify a harness's staged source copy against the original source prefix")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a test command is required after --")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    args.output = out
    root = Path.cwd()
    sources = source_snapshot(root, args.source_root)
    if args.external_functions or args.external_lcov:
        return external_reports(args, command, root, sources)
    wrappers, binaries, profiles = (out / name for name in ("bin", "objects", "profiles"))
    for directory in (wrappers, binaries, profiles):
        directory.mkdir()
    # Resolve the real compiler before prepending the wrappers to PATH.
    env = dict(os.environ, CRAP_ENGINE=args.engine, CRAP_ARTIFACTS=str(binaries),
               CRAP_BASE_PATH=os.environ.get("CRAP_BASE_PATH", os.environ["PATH"]),
               LLVM_PROFILE_FILE=str(profiles / "%p-%m.profraw"))
    compiler_name = "CRAP_GCC" if args.engine == "gcc" else "CRAP_CLANG"
    env[compiler_name] = os.environ.get(compiler_name) or tool(args.engine)
    import shlex
    wrapper = "#!/bin/sh\nexec " + " ".join(shlex.quote(arg) for arg in
        [sys.executable, str(Path(__file__).resolve()), "--compiler"]) + ' "$@"\n'
    for name in ("cc", "clang", "gcc"):
        path = wrappers / name
        path.write_text(wrapper)
        path.chmod(0o755)
    env.update(PATH=str(wrappers) + os.pathsep + env["PATH"],
               CC=str(wrappers / "cc"), TEST_CC=str(wrappers / "cc"), HOST_CC=str(wrappers / "cc"))
    with (out / "tests.log").open("w") as log:
        status = subprocess.call(command, env=env, stdout=log, stderr=subprocess.STDOUT)
    stable = sources_unchanged(root, args, sources)
    write_manifest(out, sources, command, status, stable)
    if not stable:
        raise ValueError("source files changed during coverage collection; collect again")
    if args.engine == "gcc":
        count = export_gcc(out, binaries)
        write_manifest(out, sources, command, status, stable, [("coverage_sha256", out / "coverage.lcov")])
        print("Test exit %d; %d GCC objects; report %s" % (status, count, out))
        return status
    raw = sorted(profiles.glob("*.profraw"))
    objects = sorted(binaries.glob("*.bin"))
    if not raw or not objects:
        print(f"No C coverage collected; test exit {status}. See {out / 'tests.log'}", file=sys.stderr)
        return status or 1
    subprocess.run([tool("llvm-profdata"), "merge", "-sparse", *map(str, raw),
                    "-o", str(out / "merged.profdata")], check=True)
    cmd = [tool("llvm-cov"), "export", "-format=lcov", "-instr-profile=" + str(out / "merged.profdata"),
           str(objects[0])]
    for binary in objects[1:]:
        cmd.extend(["-object", str(binary)])
    with (out / "coverage.lcov").open("w") as report, (out / "export.log").open("w") as log:
        subprocess.run(cmd, stdout=report, stderr=log, check=True)
    write_manifest(out, sources, command, status, stable, [("coverage_sha256", out / "coverage.lcov")])
    print(f"Test exit {status}; {len(objects)} binaries, {len(raw)} profiles; report {out}")
    return status


if __name__ == "__main__":
    if sys.argv[1:2] == ["--compiler"]:
        sys.exit(compiler())
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
