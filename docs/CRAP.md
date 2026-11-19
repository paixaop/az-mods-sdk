# C CRAP checks

The C adapter in `tools/crap-score.py` calls the scorer from
[unclebob/crapper](https://github.com/unclebob/crapper), pinned to
`c757ec79a6c15dc815238bbd9c2f4601eb54d3cc`, and uses Lizard 1.23.0 for C
cyclomatic complexity. Upstream crapper does not currently parse C.

The configured target is **CRAP < 10**, from
[Robert C. Martin's published guidance](https://github.com/unclebob/ub-mode).
A score of exactly 10 fails. Comparisons use the unrounded score:

```
CRAP = complexity² × (1 − coverage_fraction)³ + complexity
```

Each repository's `.crap.json` selects first-party source paths and an immutable
baseline commit. Only new or changed function bodies are scored against that
commit, including new functions in an otherwise existing file. The community
mods have no upstream baseline; all their first-party functions are in scope.
Tests, bundled codecs, and other third-party source are excluded explicitly.
Fetch the upstream history if the configured baseline is absent from a checkout;
an unknown baseline is an error, not an empty tree.

## Run

Install [uv](https://docs.astral.sh/uv/getting-started/installation/). The script's
inline metadata installs its pinned scorer and C parser in an isolated environment.
From the SDK root:

```sh
uv run --script tools/crap-score.py --output /tmp/sdk-crap.json
```

This initial scan has no coverage. It reports `N/A` and the conservative upper
bound at zero coverage, rather than presenting a measured score. Missing coverage
cannot make a risky function pass. A sufficiently simple function can pass when
even its zero-coverage upper bound is below 10.

Collect fresh coverage and score it:

```sh
python3 tools/crap-coverage.py --output /tmp/sdk-coverage -- \
    python3 -m unittest discover -s tests -v
uv run --script tools/crap-score.py \
    --lcov /tmp/sdk-coverage/coverage.lcov \
    --coverage-manifest /tmp/sdk-coverage/manifest.json \
    --output /tmp/sdk-crap.json
```

The collection directory must not exist. The collector instruments compilers
found through PATH and `CC`, `TEST_CC`, or `HOST_CC`; it retains linked test
binaries before temporary test directories disappear. It merges fresh LLVM
profiles and emits LCOV plus a source hash manifest. A failed test command returns
its failure status. Every coverage input requires a matching manifest. The scorer rejects a manifest from a failed run,
whose recorded source files have changed, or whose hash does not match the LCOV
report. The collector also checks source stability when the command ends. It
fingerprints C sources, headers, definition fragments, test scripts, and build
files. Add `--source-root ../az-mods-sdk` when collecting a mod's coverage to bind
the SDK dependency; repeat this option for other source repositories used by a
test command. Repeat both `--lcov` and `--coverage-manifest` to combine independently
collected reports, such as macOS and Linux runs. Every manifest must match the
current sources, and each report must match one verified manifest. Recollect
after refactoring.

For native suites that require GNU C, use `--engine gcc` inside the Linux build
environment. The collector compiles each translation unit into a separate
retained object, so counters survive a fixture deleting its temporary object or
executable. It exports those counters with the matching `gcov`. GCC 7 and the
build image's Python 3.6 are supported. `CRAP_GCC` and `CRAP_GCOV` can select an
explicit matching compiler/tool pair. GCC reports use actual source paths;
altered fixture copies are not mapped onto production files.

For mods, run the collector from their repository with the appropriate `make`
targets. Use a fresh `OUT` for object-based builds, or force rebuilding: cached
uninstrumented objects do not provide coverage. Compile and run the actual
production code through the existing behavior tests.

The core's existing `COVERAGE=1 tests/run.sh` additionally maps extracted host
fixtures back to release source. Run it through the collector's external-report
mode to preserve its own compiler setup and bind its report to a verified source
manifest. From the core repository, using a fresh directory under the project's
`temp/`:

```sh
report_dir="$(pwd)/../temp/core-coverage"
python3 ../az-mods-sdk/tools/crap-coverage.py --output "$report_dir" \
    --source-root ../az-mods-sdk \
    --verify-mirror tests/.work/tree/guest=src \
    --external-lcov "$report_dir/report/coverage.lcov" -- \
    env COVERAGE=1 COVERAGE_DIR="$report_dir/report" tests/run.sh
uv run --script ../az-mods-sdk/tools/crap-score.py \
    --lcov "$report_dir/report/coverage.lcov" \
    --coverage-manifest "$report_dir/manifest.json" \
    --map guest/=src/ \
    --output "$report_dir/crap.json"
```

`--functions-tsv` remains available for standalone function totals, with a
verified manifest containing their `functions_sha256` digest. It cannot be
combined with `--lcov`: aggregate percentages do not identify their executable
lines, so their overlap and denominators cannot be merged safely. Use the
core report's mapped `coverage.lcov` to combine host and native tests instead.
`--external-lcov` binds that report to its source manifest. External reports must not exist before
the command runs. `--verify-mirror` verifies that the core harness's staged source
copy still matches the original fingerprinted files. An unverified function
summary cannot supply passing coverage.

Inspect the coverage run's exit status and skips before using its report. Its
Linux-only suites need their own instrumented runs. The adapter also accepts
multiple `--lcov` inputs and explicit `--map FROM=TO` source-prefix mappings.
Do not map modified regression-control copies onto production files. Keep
coverage reports tied to the source revision that produced them.

Exit codes are 0 for passing the configured gate, 2 for threshold failures, and
1 for invalid input or a failed freshness check. JSON includes every selected
function, its source range, complexity, measured coverage, unmeasured upper
bound, and gate result, plus source hashes and HEAD. The printed table is only
the first 20 entries; use `--limit` to change its length.

## Interpretation

Coverage is executable **line coverage within the function**, merged across
translation units and input reports. Explicit zero-hit lines remain in the
denominator; functions absent from actual test coverage remain unmeasured.
Lizard parses source rather than a preprocessed translation
unit: configuration-specific branches and macro expansion may differ from the
compiled control-flow graph. The score is a refactoring signal; it does not
replace semantic tests, sanitizers, target builds, or device validation.

A function whose complexity is 10 or higher needs structural changes even at
100% coverage. Split it by domain responsibilities, preserve evaluation order
and ownership, and test missing behavior. Do not suppress functions, change the
baseline, weaken the threshold, or add empty tests to obtain a passing report.

The scorer's regression checks can be run with its dependencies:

```sh
uv run --with lizard==1.23.0 \
    --with 'crapper @ git+https://github.com/unclebob/crapper.git@c757ec79a6c15dc815238bbd9c2f4601eb54d3cc' \
    python3 -m unittest discover -s tests -p test_crap_score.py -v
```
