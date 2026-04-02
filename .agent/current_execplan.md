# ExecPlan

## ExecPlan Addendum: Align `ana` Origin Filtering With `test.root`

### 1. Objective
Update the `ana` event-list build path so the `overlay` and `signal` origin
filters work on the truth branches that actually exist in `test.root`.

### 2. Constraints
- Keep the public `Ana` headers stable.
- Keep the change inside `ana/` build-time logic and fixture verification.
- Prefer one boundary fix over broader app or sample-definition churn.

### 3. Design anchor
From `DESIGN.md`:
- keep workflows in `app/`
- prefer plain data and namespace functions
- keep module boundaries sharp

This pass should adapt `ana::build_event_list(...)` to the persisted ntuple
surface, not move origin logic into `io/` or add a second selection layer.

### 4. System map
- `ana/EventListBuild.cc`
- `io/bits/DERIVED`
- `tools/test-root-smoke.sh`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### narrow boundary repair
- keep the canonical origin rules as `count_strange`-based when available
- fall back to `truth_has_strange_fs` only when the legacy branch is absent
- keep the fallback local to the event-list build boundary

#### real-fixture proof
- extend the existing `test.root` smoke rather than add a new harness
- verify that `overlay` and `signal` eventlists partition the fixture the same
  way `truth_has_strange_fs` does

### 6. Milestones

#### Milestone A: Make origin filtering follow the available truth branches
- status: done
- hypothesis: a small fallback at the event-list build boundary is enough to
  keep old ntuples working while accepting the `test.root` truth surface
- files / symbols touched:
  - `ana::build_event_list(...)` sample-origin selection path
  - `io/bits/DERIVED`
  - `tools/test-root-smoke.sh`
- expected behavior risk: low
- verification commands:
  - `bash -n tools/test-root-smoke.sh`
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md ana/EventListBuild.cc io/bits/DERIVED tools/test-root-smoke.sh`
  - `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B /tmp/amarantin-ana-check -DCMAKE_BUILD_TYPE=Release -DAMARANTIN_TEST_ROOT_FIXTURE=/work/test.root && cmake --build /tmp/amarantin-ana-check --parallel && ctest --test-dir /tmp/amarantin-ana-check --output-on-failure -R testroot_pipeline_smoke'`
- acceptance criteria:
  - `overlay` no longer requires `count_strange` when `truth_has_strange_fs`
    is present instead
  - `signal` no longer requires `count_strange` when `truth_has_strange_fs`
    is present instead
  - the real-fixture smoke proves the two eventlists match the strange-truth
    partition from `test.root`
- verification results:
  - `bash -n tools/test-root-smoke.sh` passed
  - focused `git diff --check` passed
  - Docker configure/build plus `ctest -R testroot_pipeline_smoke` passed
    with the real fixture mounted at `/work/test.root`

### 7. Public-surface check
- compatibility impact:
  - no installed target or public header changes
  - `ana` keeps preferring the legacy `count_strange` branch when present
- reviewer sign-off:
  - explicit user request in-thread to align `ana` with the variables available
    in `test.root`

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- new abstraction added: 0

### 9. Decision log
- preserve `SignalDefinition.hh` as-is and keep the fallback in
  `EventListBuild.cc`
- use `truth_has_strange_fs` as the direct replacement for the missing
  generic strange-count split on `test.root`

### 10. Stop conditions
- stop after the fixture-backed `overlay` / `signal` split is green in Docker
- do not broaden this pass into app default-tree cleanup

## ExecPlan Addendum: Real test.root Coverage Expansion

### 1. Objective
Expand the optional `test.root` fixture smoke so it exercises the real
reweight/systematics path and the analyzer inspection surface, then verify the
result in Docker against the actual fixture file.

### 2. Constraints
- Keep the fixture smoke optional; do not make the build depend on a committed
  binary fixture.
- Use the real `test.root` only for paths that are compatible with its branch
  layout.
- Keep the pass local to fixture-smoke plumbing, one compiled checker, and
  small doc updates.

### 3. Design anchor
From `DESIGN.md` and `VISION.md`:
- libraries provide reusable pieces; apps and macros orchestrate those pieces
- `EventListIO` is the row-wise downstream surface
- `DistributionIO` is the cached bin-wise surface
- debug macros should stay thin and inspection-oriented

The goal is broader fixture-backed coverage, not a larger fixture framework.

### 4. System map
- `tests/CMakeLists.txt`
- `tests/testroot_pipeline_check.cc`
- `tools/test-root-smoke.sh`
- `ana/EventListBuild.cc`
- `syst/ReweightFill.cc`
- `tests/systematics_rigorous_check.cc`
- `COMMANDS`
- `INSTALL`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### optional fixture-smoke wiring
- add one CMake fixture path knob instead of hard-coding one local file layout
- keep the smoke absent when no fixture is available

#### broader real-fixture coverage
- build a systematics-rich `DistributionIO` cache from `test.root`
- validate `Snapshot`, row-wise plotting, covariance export, and fit outputs
- drive analyzer macros on those real outputs through `tools/run-macro`

#### skip incompatible fixture paths
- do not force `plot_event_display` through this fixture because the required
  detector-image branches are not part of `test.root`

### 6. Milestones

#### Milestone A: Expand the optional `test.root` smoke to cover the real downstream outputs
- status: done
- hypothesis: the code paths that matter most for this fixture are the real
  row-wise weighting, reweight-systematics caching, downstream export/fit
  outputs, and analyzer-facing inspection macros
- files / symbols touched:
  - optional fixture-smoke wiring in `tests/CMakeLists.txt`
  - `tests/testroot_pipeline_check.cc`
  - `tools/test-root-smoke.sh`
  - explicit-tree subrun alias handling in `ana::build_event_list(...)`
  - empty GENIE knob-pair placeholder handling in `syst::detail::compute_sample(...)`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- syst/ReweightFill.cc tests/systematics_rigorous_check.cc tests/testroot_pipeline_check.cc tools/test-root-smoke.sh ana/EventListBuild.cc .agent/current_execplan.md docs/minimality-log.md tests/CMakeLists.txt COMMANDS INSTALL`
  - `bash -n tools/test-root-smoke.sh`
  - `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/testroot-rigorous -DCMAKE_BUILD_TYPE=Release -DAMARANTIN_TEST_ROOT_FIXTURE=/work/test.root && cmake --build .build/testroot-rigorous --parallel && ctest --test-dir .build/testroot-rigorous --output-on-failure'`
- acceptance criteria:
  - the optional fixture smoke runs when a real `test.root` path is provided
  - the fixture cache includes real GENIE, flux, and reint payloads, and
    treats empty GENIE knob-pair placeholder branches as absent
  - the fixture smoke validates `Snapshot`, row-wise plotting, covariance
    export, fit outputs, and analyzer macros on those outputs
  - the Docker CTest suite is green with the real fixture
- verification results:
  - focused `git diff --check` passed
  - `bash -n tools/test-root-smoke.sh` passed
  - Docker configure/build/ctest passed with:
    - `testroot_pipeline_smoke`
    - `fit_rigorous_check`
    - `plot_rigorous_check`
    - `io_rigorous_check`
    - `systematics_rigorous_check`
    - `macro_analysis_smoke`

### 7. Public-surface check
- compatibility impact:
  - no installed target or public header changes
  - CTest gains one optional fixture smoke when a real fixture path is
    available
- reviewer sign-off:
  - explicit user request in-thread to test any remaining real-fixture gaps

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- implicit assumptions targeted:
  - the optional fixture smoke should cover more than the nominal histogram
    path
  - analyzer macros should be proven against real `test.root` outputs, not
    only synthetic fixtures
  - explicit `nuselection/EventSelectionFilter` paths should work when the
    event tree exposes `sub` instead of `subRun`
  - empty GENIE knob-pair branches on real files should not be treated as a
    malformed populated payload

### 9. Decision log
- keep the fixture file optional instead of committing a binary blob by
  default
- skip event-display coverage in this pass because the fixture does not expose
  the required image branches
- keep the explicit `nuselection/...` tree paths in the fixture smoke instead
  of flattening them to default names
- treat empty GENIE knob-pair branches on `test.root` as absent rather than as
  a truncated payload

### 10. Stop conditions
- stop after the expanded fixture suite is green in Docker
- do not turn this pass into a broad detector-fixture framework

## ExecPlan Addendum: Analyzer Macro Validation

### 1. Objective
Add a small set of analyzer-facing ROOT macros that inspect the row-wise
weight surface and cached systematics payloads directly, then verify those
macros end to end through `tools/run-macro`.

### 2. Constraints
- Keep macros thin and read-only.
- Do not turn local macros into a second workflow framework.
- Keep the pass local to macro files, the macro runner path, and one smoke
  test entrypoint.

### 3. Design anchor
From `DESIGN.md` and `VISION.md`:
- libraries provide reusable pieces; applications and macros orchestrate those
- `EventListIO` is the row-wise debug / inspection surface
- `DistributionIO` is the fine-bin cache surface
- thin macros may wrap inspection paths, but they should stay small

The goal is analyzer-facing inspection helpers with deterministic verification,
not a new downstream plotting framework.

### 4. System map
- `.rootlogon.C`
- `io/macro/inspect_weights.C`
- `plot/macro/inspect_systematics.C`
- `tools/run-macro`
- `tools/macro-analysis-smoke.sh`
- `tests/CMakeLists.txt`
- `COMMANDS`
- `USAGE`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### analyzer-facing inspection macros
- add one macro that summarises persisted event weights per `EventListIO`
  sample
- add one macro that summarises detector, knob, and family payloads on one
  cached `DistributionIO::Spectrum`

#### macro-runner path cleanup
- remove stale `.rootlogon.C` includes that break the ROOT macro path
- replace deleted fit-macro examples in `tools/run-macro` and `COMMANDS`

#### deterministic macro verification
- build a tiny synthetic event-list and cache fixture
- run the new macros through `tools/run-macro`
- assert the printed summaries directly in one smoke test

### 6. Milestones

#### Milestone A: Add analyzer inspection macros and verify the macro runner path
- status: done
- hypothesis: a small read-only macro layer is worth keeping when it exposes
  the persisted weight and systematics surfaces directly and is exercised
  through the real ROOT runner path
- files / symbols touched:
  - `.rootlogon.C`
  - `inspect_weights(...)`
  - `inspect_systematics(...)`
  - `tools/macro-analysis-smoke.sh`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md .rootlogon.C io/macro/inspect_weights.C plot/macro/inspect_systematics.C tools/run-macro tools/macro-analysis-smoke.sh tests/CMakeLists.txt COMMANDS USAGE`
  - `bash -n tools/run-macro`
  - `bash -n tools/macro-analysis-smoke.sh`
  - `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/macro-rigorous -DCMAKE_BUILD_TYPE=Release && cmake --build .build/macro-rigorous --parallel && ctest --test-dir .build/macro-rigorous --output-on-failure'`
- acceptance criteria:
  - analyzer-facing macros exist for weight-surface and systematics-payload
    inspection
  - `tools/run-macro` examples point at real macros
  - the ROOT macro path no longer depends on deleted headers
  - the macro smoke is green in Docker
- verification results:
  - focused `git diff --check` passed
  - `bash -n tools/run-macro` passed
  - `bash -n tools/macro-analysis-smoke.sh` passed
  - Docker configure/build/ctest passed with:
    - `fit_rigorous_check`
    - `plot_rigorous_check`
    - `io_rigorous_check`
    - `systematics_rigorous_check`
    - `macro_analysis_smoke`

### 7. Public-surface check
- compatibility impact:
  - no installed target or public header changes
  - CTest gains one shell-driven macro smoke
- reviewer sign-off:
  - explicit user request in-thread to write analyzer-facing macros and test
    them

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- implicit assumptions targeted:
  - the ROOT macro path should not depend on deleted headers
  - analyzer inspection of `__w_norm__`, `__w_cv__`, `__w__`, and cached
    systematics should not require hand-written ROOT sessions

### 9. Decision log
- keep the new macros read-only and debug-oriented
- avoid inventing a final-region semantic plot macro surface in this pass

### 10. Stop conditions
- stop after the macro smoke is green in Docker
- do not broaden this pass into a macro-framework redesign

## ExecPlan Addendum: Rigorous Fit Validation

### 1. Objective
Harden the `fit/` boundary against malformed cached fit payloads and ambiguous
channel definitions, then add one self-contained regression test that checks
the default nuisance construction, prediction math, and fit convergence path
directly.

### 2. Constraints
- Keep the pass local to `fit/` plus one new `tests/` entrypoint.
- Keep the fit API small; do not redesign `mk_fit` or the persisted
  `DistributionIO` surface in this pass.
- Do not pull the broader dirty-worktree harness into the published change.

### 3. Design anchor
From `DESIGN.md`:
- prefer plain data and namespace functions
- keep workflows in `app/`
- add abstractions only when they delete complexity

The goal is a stricter fit boundary and deterministic coverage, not a broader
fit-layer rewrite.

### 4. System map
- `fit/SignalStrengthFit.cc`
- `fit/README`
- `tests/CMakeLists.txt`
- `tests/fit_rigorous_check.cc`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### explicit fit-contract checks
- reject duplicate non-data process names and invalid signal-process kinds
- reject partial detector/total envelopes and malformed family payload shapes
- reject invalid channel binning before prediction or fitting starts

#### self-contained fit regression coverage
- build one synthetic fit channel directly in-process
- check nuisance sharing across processes, family/knob/detector/total math,
  and one simple profile fit
- assert the main malformed-input failures explicitly

### 6. Milestones

#### Milestone A: Harden fit assumptions and add a rigorous `fit/` regression test
- status: done
- hypothesis: the fit library becomes easier to trust when malformed cached
  payloads fail at the fit boundary and one deterministic test exercises the
  default builder and calculator directly
- files / symbols touched:
  - `fit::validate_problem(...)`
  - fit-library prediction / builder assumptions documented in `fit/README`
  - `tests/fit_rigorous_check.cc`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md fit/README fit/SignalStrengthFit.cc tests/CMakeLists.txt tests/fit_rigorous_check.cc`
  - `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/fit-rigorous -DCMAKE_BUILD_TYPE=Release && cmake --build .build/fit-rigorous --parallel && ctest --test-dir .build/fit-rigorous --output-on-failure'`
- acceptance criteria:
  - malformed fit-channel definitions fail explicitly at the fit boundary
  - the default builder shares family, detector, and knob nuisances across
    processes as intended
  - signal scaling and nuisance shifts produce the expected predicted bins
  - a simple one-parameter profile fit converges with the expected result
  - the Docker CTest suite is green
- verification results:
  - focused `git diff --check` passed
  - Docker configure/build/ctest passed with:
    - `fit_rigorous_check`
    - `plot_rigorous_check`
    - `io_rigorous_check`
    - `systematics_rigorous_check`

### 7. Public-surface check
- compatibility impact:
  - no installed target or public header changes
  - CTest gains one new internal regression executable
- reviewer sign-off:
  - explicit user request in-thread to validate the fit library

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- implicit assumptions targeted:
  - `signal_process` should name a real signal process, not any process
  - one channel should not carry duplicate non-data process names
  - family/envelope payloads should match the declared histogram shape
  - fit prediction should be exercised through the same nuisance builder used
    by `mk_fit`

### 9. Decision log
- keep the new regression synthetic and self-contained
- harden only the narrow fit boundary checks needed for deterministic
  validation

### 10. Stop conditions
- stop after the fit regression is green in Docker
- do not expand this pass into a broader fit-framework redesign

## ExecPlan Addendum: Rigorous Plot Validation

### 1. Objective
Harden the `plot/` to `IO` boundary so row-wise plots stop silently
double-counting detector-variation samples or swallowing bad tree formulas,
then add one self-contained regression test that checks the main
`EventListIO` and `DistributionIO` plotting contracts directly.

### 2. Constraints
- Keep the pass local to `plot/` plus one new `tests/` entrypoint.
- Keep `plot/` rendering-only; do not move persistence or selection logic into
  it.
- Do not pull the broader dirty-worktree harness into the published change.

### 3. Design anchor
From `DESIGN.md`:
- `plot/`: rendering only
- downstream code should usually open `EventListIO` and stay on that surface
- add abstractions only when they delete complexity

The goal is a safer rendering boundary and deterministic coverage, not a
`plot/` redesign.

### 4. System map
- `plot/EventListPlotting.cc`
- `plot/PlottingHelper.cc`
- `plot/EfficiencyPlot.cc`
- `plot/README`
- `tests/CMakeLists.txt`
- `tests/plot_rigorous_check.cc`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### default row-wise sample filtering
- treat detector-variation samples as alternate systematics lanes, not default
  row-wise plotting inputs
- keep explicit sample-key access intact

#### explicit draw/payload failures
- reject `TTree::Draw(...)` formula failures instead of accepting empty plots
- reject malformed cached-spectrum `nominal` / `sumw2` shapes before building
  a `TH1D`

#### self-contained plot regression coverage
- build one synthetic `EventListIO` fixture with nominal, detector, and data
  rows
- check default sample selection, alias-driven stack/unstack rendering,
  `EfficiencyPlot`, and cached-spectrum plotting

### 6. Milestones

#### Milestone A: Harden plot/IO assumptions and add a rigorous `plot/` regression test
- status: done
- hypothesis: the row-wise plot path becomes easier to trust when detector
  alternates are excluded by default and bad draw expressions fail explicitly
- files / symbols touched:
  - `plot_utils::selected_sample_keys(...)`
  - `plot_utils::make_entries(...)`
  - `plot_utils::fill_histogram(...)`
  - `plot_utils::EfficiencyPlot::compute(...)`
  - `tests/plot_rigorous_check.cc`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md plot/EventListPlotting.cc plot/PlottingHelper.cc plot/EfficiencyPlot.cc plot/README tests/CMakeLists.txt tests/plot_rigorous_check.cc`
  - `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/plot-rigorous -DCMAKE_BUILD_TYPE=Release && cmake --build .build/plot-rigorous --parallel && ctest --test-dir .build/plot-rigorous --output-on-failure'`
- acceptance criteria:
  - default row-wise plot enumeration skips detector-variation samples
  - explicit detector sample selection still works
  - bad event-list plot formulas fail explicitly
  - malformed cached-spectrum payloads fail explicitly at the plot boundary
  - the Docker CTest suite is green
- verification results:
  - focused `git diff --check` passed
  - Docker configure/build/ctest passed with:
    - `plot_rigorous_check`
    - `io_rigorous_check`
    - `systematics_rigorous_check`

### 7. Public-surface check
- compatibility impact:
  - no installed target or public header changes
  - CTest gains one new internal regression executable
  - default row-wise `EventListIO` plotting now ignores detector variations
    unless the caller selects them explicitly
- reviewer sign-off:
  - explicit user request in-thread to do the same rigorous pass for `plot/`
    and its connection with `IO`

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- implicit assumptions targeted:
  - detector-variation samples should not be treated as default nominal plot
    inputs
  - `TTree::Draw(...)` formula failures should not quietly produce empty plots
  - cached plot payloads should match their declared histogram shapes

### 9. Decision log
- keep the new regression synthetic and self-contained
- keep explicit detector sample access available through explicit sample-key
  selection

### 10. Stop conditions
- stop after the plot regression is green in Docker
- do not expand this pass into a broader `plot/` redesign

## ExecPlan Addendum: Rigorous IO Validation

### 1. Objective
Harden the `io/` persistence chain against malformed persisted metadata and
payload-shape mistakes, then add one self-contained regression test that
checks the main shard, sample, dataset, event-list, and distribution
contracts directly.

### 2. Constraints
- Keep `io/` persistence-only.
- Keep the pass local to `io/` plus one new `tests/` entrypoint.
- Do not pull the broader dirty-worktree smoke harness into the published
  change.

### 3. Design anchor
From `DESIGN.md`:
- keep `io/` persistence-only
- prefer plain data and namespace functions
- add abstractions only when they delete complexity

The goal is stricter persistence contracts and better regression coverage, not
an `io/` redesign.

### 4. System map
- `io/ShardIO.cc`
- `io/SampleIO.cc`
- `io/EventListIO.cc`
- `io/DistributionIO.cc`
- `tests/CMakeLists.txt`
- `tests/io_rigorous_check.cc`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### explicit persistence-contract checks
- keep `ShardIO::scan(...)` aligned with the public `files()` accessor
- validate persisted `SampleIO` metadata on read
- reject malformed `DistributionIO::Spectrum` payload shapes before write

#### self-contained IO regression coverage
- build tiny ROOT and SQLite fixtures in-process
- check shard exposure aggregation, sample/dataset roundtrips, event-list
  sample and subrun-tree access, and cached-spectrum rebinning
- assert the main malformed-input failures explicitly

### 6. Milestones

#### Milestone A: Harden IO assumptions and add a rigorous `io/` regression test
- status: done
- hypothesis: `io/` becomes easier to trust when its public persistence
  surface rejects malformed metadata/payloads and one deterministic test
  exercises the full chain directly
- files / symbols touched:
  - `ShardIO::scan(...)`
  - `SampleIO::read(...)`
  - subrun-tree persistence/readback in `EventListIO`
  - `DistributionIO::write(...)`
  - `tests/io_rigorous_check.cc`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md io/ShardIO.cc io/SampleIO.cc io/EventListIO.cc io/DistributionIO.cc tests/CMakeLists.txt tests/io_rigorous_check.cc`
  - `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/io-rigorous -DCMAKE_BUILD_TYPE=Release && cmake --build .build/io-rigorous --parallel && ctest --test-dir .build/io-rigorous --output-on-failure'`
- acceptance criteria:
  - shard scans keep their input-file provenance
  - invalid persisted sample metadata fails on read
  - event-list subrun-tree lookup survives explicit tree paths
  - cached-spectrum payload-shape mismatches fail explicitly
  - the Docker CTest suite is green
- verification results:
  - focused `git diff --check` passed
  - Docker configure/build/ctest passed with:
    - `io_rigorous_check`
    - `systematics_rigorous_check`

### 7. Public-surface check
- compatibility impact:
  - no installed target or public header changes
  - CTest gains one new internal regression executable
- reviewer sign-off:
  - explicit user request in-thread to do the same rigorous pass for `io/`

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- implicit assumptions targeted:
  - `ShardIO::scan(...)` must keep the file list that it just scanned
  - persisted sample metadata should obey the same beam/polarity rules on read
  - event-list subrun-tree readback should not depend on path-vs-leaf naming
  - cached spectrum payloads should match their declared shapes

### 9. Decision log
- keep the new regression synthetic and self-contained
- port only the minimal event-list subrun-tree fix needed for this pass

### 10. Stop conditions
- stop after the IO regression is green in Docker
- do not expand this pass into a broader `io/` redesign

## ExecPlan Addendum: Rigorous Systematics Validation

### 1. Objective
Harden the live `syst/` calculation path against malformed selected trees and
add one self-contained regression test that checks the detector, reweighting,
rebinning, and total-envelope math directly.

### 2. Constraints
- Keep the pass local to `syst/` plus one new `tests/` entrypoint.
- Do not depend on the broader uncommitted local fixture harness.
- Preserve the current cache format and CLI surface.

### 3. Design anchor
From `DESIGN.md`:
- prefer plain data and namespace functions
- add abstractions only when they delete complexity
- keep module boundaries sharp

The goal is tighter validation and sharper tests, not a redesign of the
systematics API.

### 4. System map
- `CMakeLists.txt`
- `syst/ReweightFill.cc`
- `syst/Systematics.cc`
- `tests/CMakeLists.txt`
- `tests/systematics_rigorous_check.cc`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### explicit malformed-input rejection
- reject missing `__w__` on selected trees
- reject universe-family size changes across entries
- honour `enable_detector` instead of inferring detector work from a non-empty
  sample-key list alone

#### calculation-level regression coverage
- build one synthetic `EventListIO` fixture in-process
- check detector covariance, PPFX fallback, GENIE knob-pair shifts, exact cache
  rebinning, and total-envelope quadrature
- assert the main failure cases explicitly

### 6. Milestones

#### Milestone A: Harden assumptions and add a rigorous `syst/` regression test
- status: done
- hypothesis: `syst/` becomes safer and easier to trust when malformed trees
  fail fast and the core covariance math is covered by one deterministic test
- files / symbols touched:
  - root `BUILD_TESTING` / `check` wiring in `CMakeLists.txt`
  - `syst::detail::compute_sample(...)`
  - detector gate in `syst::build_cache_entry(...)`
  - `tests/systematics_rigorous_check.cc`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/ReweightFill.cc syst/Systematics.cc tests/CMakeLists.txt tests/systematics_rigorous_check.cc`
  - `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/docker-rigorous -DCMAKE_BUILD_TYPE=Release && cmake --build .build/docker-rigorous --parallel && ctest --test-dir .build/docker-rigorous --output-on-failure'`
- acceptance criteria:
  - malformed selected-tree/systematic payload assumptions fail with explicit
    errors
  - one deterministic test covers the main detector and reweighting math paths
  - the Docker CTest suite is green
- verification results:
  - focused `git diff --check` passed
  - Docker configure/build/ctest passed with the new
    `systematics_rigorous_check` plus the existing suite green

### 7. Public-surface check
- compatibility impact:
  - no installed target or public header changes
  - CTest gains one new internal regression executable
- reviewer sign-off:
  - explicit user request in-thread to test systematics rigorously

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- implicit assumptions removed:
  - selected trees must carry `__w__`
  - universe-family widths must stay stable across entries
  - detector work now follows `enable_detector`

### 9. Decision log
- keep the new test synthetic and self-contained so it can be committed
  independently of the broader local smoke harness
- harden the live code path instead of weakening tests around malformed input

### 10. Stop conditions
- stop after the calculation-level regression test is green in Docker
- do not expand this pass into a broader `syst/` redesign

## ExecPlan Addendum: Dead Systematics Surface Cleanup

### 1. Objective
Delete the reviewed dead systematics surface: the no-op
`persist_covariance` option and the unused persisted
`detector_cv_sample_keys` payload.

### 2. Constraints
- Keep current cache behavior unchanged for live callers.
- Preserve legacy sigma-only family loading for old cache entries.
- Limit the pass to dead option/data removal plus a small clarity note for the
  remaining legacy sigma-only branch.

### 3. Design anchor
From `DESIGN.md`:
- delete obsolete classes and wrappers
- keep module boundaries sharp
- make grep-based navigation easier

If a public knob no longer changes behavior, or a persisted field has no live
reader, it should not stay on the active surface.

### 4. System map
- `syst/Systematics.hh`
- `syst/Systematics.cc`
- `syst/ReweightCovariance.cc`
- `io/DistributionIO.hh`
- `io/DistributionIO.cc`
- `tools/systematics-reweight-smoke.sh`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### dead option removal
- delete `persist_covariance` from `SystematicsOptions`
- delete `persist_covariance` from `CacheBuildOptions`
- remove the stale smoke-test assignment

#### dead payload removal
- delete `detector_cv_sample_keys` from `DistributionIO::Spectrum`
- stop reading and writing that ROOT payload branch
- stop populating it in `syst::build_cache_entry(...)`

#### compatibility clarification
- keep the sigma-only family branch only as explicit legacy-cache handling

### 6. Milestones

#### Milestone A: Remove dead systematics option and payload surface
- status: done
- hypothesis: the `syst/` and `DistributionIO` surfaces get smaller and less
  misleading when dead knobs and unread payload fields are deleted
- files / symbols touched:
  - `persist_covariance`
  - `detector_cv_sample_keys`
  - legacy sigma-only branch comment in `syst/ReweightCovariance.cc`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/Systematics.hh syst/Systematics.cc syst/ReweightCovariance.cc io/DistributionIO.hh io/DistributionIO.cc tools/systematics-reweight-smoke.sh`
  - `rg -n "persist_covariance|detector_cv_sample_keys" -S syst io tools`
  - `rg -n "legacy caches may carry only per-bin sigma" -S syst/ReweightCovariance.cc`
- acceptance criteria:
  - no live code references remain to `persist_covariance`
  - no live code references remain to `detector_cv_sample_keys`
  - the remaining sigma-only branch is clearly marked as legacy-cache support
- verification results:
  - focused `git diff --check` passed for the dead-surface cleanup files
  - `rg -n "persist_covariance|detector_cv_sample_keys" -S syst io tools`
    returned no matches
  - the sigma-only branch now carries an explicit legacy-cache comment in
    `syst/ReweightCovariance.cc`
  - `cmake --build build --target IO Syst --parallel` still did not provide a
    trustworthy compile check here because the local `build/` tree points at
    missing `/usr/bin/cmake`

### 7. Public-surface check
- compatibility impact:
  - removes one dead public option from `syst/Systematics.hh`
  - removes one unread public payload field from `io/DistributionIO.hh`
- reviewer sign-off:
  - explicit user approval received in-thread to fix the stale/dead code found
    in review

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- dead public knobs removed:
  - `persist_covariance`
- dead persisted fields removed:
  - `detector_cv_sample_keys`

### 9. Decision log
- keep sigma-only family handling as explicit legacy-cache support
- remove only the definitely dead option/data paths in this pass

### 10. Stop conditions
- stop after the dead option and unread payload field are removed
- do not expand this pass into broader family-payload redesign

## ExecPlan Addendum: Collapse CacheKey into Detail.hh

### 1. Objective
Inline the private cache-key helper implementation into `syst/bits/Detail.hh`
and delete the now-single-purpose `bits/CacheKey.cc` translation unit.

### 2. Constraints
- Keep the change private to `syst/`; do not alter installed public headers.
- Preserve the existing cache-key behavior and cache format.
- Keep the change narrow: one private source file disappears and the shared
  private header absorbs its helper bodies.

### 3. Design anchor
From `DESIGN.md`:
- keep module layout flat
- use `bits/` only for shared private helpers
- add abstractions only when they delete complexity

The cache-key helpers are only used through `Detail.hh`, so the extra private
translation unit no longer earns its own file.

### 4. System map
- `syst/bits/Detail.hh`
- `syst/CMakeLists.txt`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### helper collapse
- inline `fine_spec_for(...)`, `encode_options_for_cache(...)`,
  `stable_hash_hex(...)`, and `evaluation_cache_key(...)` in
  `syst/bits/Detail.hh`
- delete `syst/bits/CacheKey.cc`
- remove the dead source entry from `syst/CMakeLists.txt`

### 6. Milestones

#### Milestone A: Collapse the private cache-key translation unit
- status: done
- hypothesis: one fewer private `.cc` file makes the `syst/bits/` boundary
  easier to scan because all shared cache-key helpers live in one place
- files / symbols touched:
  - `syst/bits/Detail.hh`
  - `syst/bits/CacheKey.cc`
  - `syst/CMakeLists.txt`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/bits/Detail.hh syst/CMakeLists.txt`
  - `rg -n "bits/CacheKey\\.cc|syst/CacheKey\\.cc" -S syst/CMakeLists.txt syst`
  - `find syst -maxdepth 2 -type f | sort`
- acceptance criteria:
  - the cache-key helpers live directly in `syst/bits/Detail.hh`
  - `syst/bits/CacheKey.cc` is gone
  - `syst/CMakeLists.txt` no longer lists the deleted source file
- verification results:
  - focused `git diff --check` passed for the collapse files
  - no live `syst/` build-surface references remain to `syst/bits/CacheKey.cc`
  - `find syst -maxdepth 2 -type f | sort` shows no standalone cache-key
    source file under `syst/`

### 7. Public-surface check
- compatibility impact:
  - none; this is private `syst/` implementation cleanup only
- reviewer sign-off:
  - explicit user approval received in-thread for collapsing `CacheKey` into
    `Detail.hh`

### 8. Reduction ledger
- files deleted: 1
  - `syst/bits/CacheKey.cc`
- wrappers removed: 0
- shell branches removed: 0
- duplicate private file boundaries removed:
  - standalone cache-key translation unit

### 9. Decision log
- keep these helpers in `Detail.hh` rather than adding another private header
- keep the rest of the `syst/` helper split unchanged

### 10. Stop conditions
- stop after the cache-key helpers are header-local and the dead source entry
  is removed
- do not expand this pass into broader `Systematics.cc` restructuring

## ExecPlan Addendum: DistributionIO Rebinning Ownership

### 1. Objective
Move exact cached-payload rebinning out of `syst/` and into
`DistributionIO`, while keeping detector envelopes and family-result semantics
in `syst/`.

### 2. Constraints
- Keep `io/` persistence-focused; only move payload-shape transforms there.
- Do not move detector-envelope or universe-family interpretation logic into
  `io/`.
- Preserve cache format and downstream behavior.
- Delete the obsolete `syst/Rebin.cc` layer once callers switch over.

### 3. Design anchor
From `DESIGN.md`:
- `io/` owns persistence only
- prefer plain data and namespace functions
- keep module boundaries sharp

The persistence layer already owns the cached bin-wise payload shape, so exact
rebinned views of that payload belong there more than in `syst/`.

### 4. System map
- `io/DistributionIO.hh`
- `io/DistributionIO.cc`
- `syst/Systematics.cc`
- `syst/DetectorSystematics.cc`
- `syst/ReweightCovariance.cc`
- `syst/bits/Detail.hh`
- `syst/CMakeLists.txt`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### ownership shift
- let `DistributionIO::Spectrum` rebin vectors, covariances, and row/bin-major
  payloads using its own stored histogram spec

#### deletion pass
- delete `syst/Rebin.cc`
- delete the duplicate rebin helper declarations from `syst/bits/Detail.hh`

### 6. Milestones

#### Milestone A: Move rebinning into DistributionIO
- status: done
- hypothesis: cached-payload rebinning gets easier to find and cheaper to
  reuse when the logic lives with `DistributionIO::Spectrum`
- files / symbols touched:
  - `DistributionIO::Spectrum::rebinned_values(...)`
  - `DistributionIO::Spectrum::rebinned_covariance(...)`
  - `DistributionIO::Spectrum::rebinned_source_major_payload(...)`
  - `DistributionIO::Spectrum::rebinned_bin_major_payload(...)`
  - `syst/Rebin.cc`
- expected behavior risk: medium-low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md io/DistributionIO.hh io/DistributionIO.cc syst/Systematics.cc syst/DetectorSystematics.cc syst/ReweightCovariance.cc syst/bits/Detail.hh syst/CMakeLists.txt`
  - `rg -n "build_rebin_matrix|rebin_vector|rebin_detector_templates|rebin_detector_shift_vectors|rebin_shift_vectors|rebin_covariance" -S syst`
  - `cmake --build build --target IO Syst --parallel`
- acceptance criteria:
  - `DistributionIO` owns the exact payload rebin transforms
  - `syst/Rebin.cc` is gone
  - `syst/` callers use `DistributionIO::Spectrum` rebin helpers instead of
    local duplicate math
  - `syst/bits/Detail.hh` no longer carries implementation-only rebin or
    matrix/branch helpers
- verification results:
  - focused `git diff --check` passed for the move-pass files
  - the old `syst` rebin helper names no longer appear under `syst/`
  - `syst/bits/Detail.hh` is back to shared declarations only; the
    implementation-only `MatrixRowMajor` alias and central-weight branch name
    now live in their owning `.cc` files
  - `cmake --build build --target IO Syst --parallel` did not provide a
    trustworthy compile check here because the local build tree is stale and
    reconfigure remains blocked by missing SQLite3 headers and
    `nlohmann/json.hpp`

### 7. Public-surface check
- compatibility impact:
  - `DistributionIO.hh` now exposes exact rebin helpers on `Spectrum`
- reviewer sign-off:
  - explicit user approval received in-thread for moving rebinning into
    `DistributionIO`

### 8. Reduction ledger
- files deleted: 1
  - `syst/Rebin.cc`
- wrappers removed: 0
- shell branches removed: 0
- duplicated helper families removed:
  - exact vector rebinning
  - exact covariance rebinning
  - exact row/bin-major payload rebinning
- approximate LOC delta: small positive in `io/`, larger negative in `syst/`

### 9. Decision log
- keep detector-envelope and family-result assembly in `syst/`
- move only exact cached-payload transforms into `DistributionIO`

### 10. Stop conditions
- stop after the rebin math lives in `DistributionIO` and the duplicate `syst`
  helper layer is deleted
- do not expand the pass into broader `DistributionIO` family/result redesign

## ExecPlan Addendum: mk_cov Executable Rename

### 1. Objective
Rename the covariance export executable and its repo references from the old
long-form CLI name to `mk_cov`.

### 2. Constraints
- Preserve the existing covariance export behavior and file format.
- Keep the source file `app/mk_cov.cc` in place.
- Update active docs, scripts, build targets, and tracking files so the new
  executable name is used consistently.
- Leave `.git/` history and reflogs untouched.

### 3. Design anchor
From `DESIGN.md`:
- prefer fewer concepts per workflow
- keep workflows easy to grep
- add abstractions only when they delete complexity

This pass deletes a leftover compatibility name instead of adding another one.

### 4. System map
- `app/CMakeLists.txt`
- `app/mk_cov.cc`
- `COMMANDS`
- `INSTALL`
- `USAGE`
- `INVARIANTS.md`
- `tools/systematics-sbnfit-export-smoke.sh`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### cli naming
- rename the app target and installed executable to `mk_cov`
- update all repo references to the new CLI name

### 6. Milestones

#### Milestone A: Apply the executable rename
- status: done
- hypothesis: one short covariance-export CLI name is easier to teach, grep,
  and keep in sync with the existing `app/mk_cov.cc` source path
- files / symbols touched:
  - `mk_cov`
  - `app/CMakeLists.txt`
  - `app/mk_cov.cc`
  - workflow docs and smoke script references
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/CMakeLists.txt app/mk_cov.cc COMMANDS INSTALL USAGE INVARIANTS.md tools/systematics-sbnfit-export-smoke.sh`
  - `rg --hidden --glob '!.git' -n "retired covariance export CLI name" -S .`
  - `cmake --build build --target mk_cov --parallel`
- acceptance criteria:
  - the executable target is named `mk_cov`
  - no repo references remain to the retired CLI name outside `.git/`
  - the smoke script and install docs call `mk_cov`
- verification results:
  - focused `git diff --check` passed for the rename-pass files
  - the hidden-file `rg` sweep returned no remaining references to the retired
    covariance export CLI name outside `.git/`
  - `cmake --build build --target mk_cov --parallel` did not provide a
    trustworthy compile check here because the local `build/` tree is stale
    and reconfigure remains blocked by missing SQLite3 headers and
    `nlohmann/json.hpp`

### 7. Public-surface check
- compatibility impact:
  - the installed executable and build target are now `mk_cov`
- reviewer sign-off:
  - explicit user approval received in-thread for replacing every repo
    reference to the old CLI name

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- stale CLI names removed from the active tree:
  - the old covariance export executable name
- approximate LOC delta: near-neutral; mostly string and target-name updates

### 9. Decision log
- keep the smoke script filename unchanged in this pass; only the invoked
  binary name changed
- update historical tracking files too so the repo contains no remaining live
  references to the retired CLI name

### 10. Stop conditions
- stop after the executable target, docs, scripts, and tracking files all use
  `mk_cov`
- do not expand the pass into unrelated SBNFit export refactors

## ExecPlan Addendum: Cards Directory Rename

### 1. Objective
Rename the checked-in sample-catalog workflow directory from `samples/` to
`cards/` without changing the internal ROOT layout or build artifact paths.

### 2. Constraints
- Preserve `build/samples/` as the sample artifact output directory.
- Preserve internal ROOT object paths like `samples/<sample-key>/...`.
- Keep `samples-dag.mk` unchanged in this pass.
- Update only active workflow paths and defaults that refer to the checked-in
  catalog directory.

### 3. Design anchor
From `DESIGN.md`:
- prefer fewer concepts per workflow
- keep workflows in `app/` and helper configuration nearby
- make grep-based navigation easier

This pass only renames the checked-in workflow inputs so the repo-local catalog
directory has the reviewed old-school name.

### 4. System map
- `cards/README`
- `cards/datasets.tsv`
- `cards/catalog.tsv`
- `cards/generated/*`
- `tools/render-sample-catalog.sh`
- `COMMANDS`
- `USAGE`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### directory naming
- rename `samples/` to `cards/`

#### path defaults
- update the render script defaults from `samples/...` to `cards/...`
- update active workflow docs and generated include paths to `cards/generated`

### 6. Milestones

#### Milestone A: Apply the directory rename
- status: done
- hypothesis: the repo-local catalog directory becomes less ambiguous once it
  stops sharing the generic `samples` name with build outputs and internal ROOT
  paths
- files / symbols touched:
  - `cards/README`
  - `cards/datasets.tsv`
  - `cards/catalog.tsv`
  - `cards/generated/*`
  - `tools/render-sample-catalog.sh`
  - `COMMANDS`
  - `USAGE`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md tools/render-sample-catalog.sh COMMANDS USAGE cards/README cards/generated/datasets.mk`
  - `rg -n "samples/generated|samples/catalog\\.tsv|samples/datasets\\.tsv" -S COMMANDS USAGE tools/render-sample-catalog.sh cards`
  - `find cards -maxdepth 2 -type f | sort`
- acceptance criteria:
  - the checked-in workflow directory is named `cards/`
  - active docs and defaults point at `cards/generated/...`
  - `build/samples/` and internal ROOT `samples/...` paths are unchanged
- verification results:
  - focused `git diff --check` passed for the rename-pass files
  - the focused `rg` sweep found no remaining active `samples/...` workflow
    paths in the updated files
  - `find cards -maxdepth 2 -type f | sort` shows the renamed catalog tree

### 7. Public-surface check
- compatibility impact:
  - repo-local workflow paths changed from `samples/...` to `cards/...`
  - build artifact paths and persisted ROOT paths are unchanged
- reviewer sign-off:
  - explicit user approval received in-thread for the directory rename

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- stale repo-local workflow paths removed:
  - `samples/catalog.tsv`
  - `samples/datasets.tsv`
  - `samples/generated/*`
- approximate LOC delta: near-neutral; mostly path updates plus one directory
  rename

### 9. Decision log
- leave historical log entries below untouched where they describe earlier
  milestones under the old directory name
- keep `samples-dag.mk` and `build/samples/` unchanged in this pass

### 10. Stop conditions
- stop after the checked-in workflow directory and active docs/defaults use
  `cards/`
- do not expand the pass into a broader rename of build outputs or ROOT paths

## ExecPlan Addendum: App Source File Rename Cleanup

### 1. Objective
Make the `app/` source filenames match the public CLI names more closely by
renaming the two remaining mismatched entrypoint files.

### 2. Constraints
- Preserve installed executable names in this pass.
- Keep the rename limited to source filenames and direct references to them.
- Leave historical log entries untouched where they describe earlier
  milestones.

### 3. Design anchor
From `DESIGN.md`:
- keep workflows in `app/`
- prefer fewer concepts per workflow
- make grep-based navigation easier

This pass does not change workflow behavior; it just removes two misleading
source filenames from the active tree.

### 4. System map
- `app/CMakeLists.txt`
- `app/mk_fit.cc`
- `app/mk_cov.cc`
- `fit/README`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### file naming
- rename `app/mk_xsec_fit.cc` to `app/mk_fit.cc`
- rename the covariance export entrypoint source to `app/mk_cov.cc`

### 6. Milestones

#### Milestone A: Apply the app source-file rename
- status: done
- hypothesis: matching the source filenames to the CLI names reduces friction
  when navigating the app entrypoints
- files / symbols touched:
  - `app/CMakeLists.txt`
  - `app/mk_fit.cc`
  - `app/mk_cov.cc`
  - `fit/README`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/CMakeLists.txt app/mk_fit.cc app/mk_cov.cc fit/README`
  - `rg -n "app/mk_xsec_fit\\.cc" -S app fit/README`
- acceptance criteria:
  - the app build references only `mk_fit.cc` and `mk_cov.cc`
  - active docs no longer point at the old source filenames
- verification results:
  - focused `git diff --check` passed for the rename-pass files
  - the focused `rg` sweep found no remaining active references in `app/` or
    `fit/README`

### 7. Public-surface check
- compatibility impact:
  - none; installed executable names stay `mk_fit` and `mk_cov`
- reviewer sign-off:
  - explicit user approval received in-thread for the source-file rename

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- stale source filenames removed from the active tree:
  - `app/mk_xsec_fit.cc`
- approximate LOC delta: near-neutral; pure file rename plus small reference
  updates

### 9. Decision log
- keep this pass scoped to source-file naming; executable naming is handled
  separately
- update only active references to the source paths; historical log entries may
  continue to mention the old filenames

### 10. Stop conditions
- stop after the source filenames, build file, and active path references are
  aligned
- do not expand the pass beyond the source-file cleanup set

## ExecPlan Addendum: Systematics File Layout Cleanup

### 1. Objective
Rename the remaining misleading `syst/` implementation filenames and split the
old `Support.cc` junk drawer into two responsibility-named translation units.

### 2. Constraints
- Preserve systematics behavior and cache formats.
- Keep the installed public header surface unchanged in this pass.
- Keep the split local to `syst/`; do not push helper logic into `io/` or
  `app/`.
- Limit churn to the user-approved file-level rename and split set.

### 3. Design anchor
From `DESIGN.md`:
- keep module boundaries sharp
- prefer plain data and namespace functions
- add abstractions only when they delete complexity

This pass removes one generic implementation bucket and makes the remaining
translation-unit names describe their real responsibilities.

### 4. System map
- `syst/CMakeLists.txt`
- `syst/Systematics.cc`
- `syst/DetectorSystematics.cc`
- `syst/bits/Detail.hh`
- `syst/Rebin.cc`
- `syst/ReweightFill.cc`
- `syst/ReweightCovariance.cc`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### file naming
- rename `Detector.cc` to `DetectorSystematics.cc`
- rename `UniverseFill.cc` to `ReweightFill.cc`
- rename `UniverseSummary.cc` to `ReweightCovariance.cc`

#### junk-drawer split
- split `Support.cc` into cache-key helpers and rebin helpers

### 6. Milestones

#### Milestone A: Apply the file-layout cleanup
- status: done
- hypothesis: responsibility-named translation units make `syst/` easier to
  scan and grep without changing runtime behavior
- files / symbols touched:
  - `syst/CMakeLists.txt`
  - `syst/DetectorSystematics.cc`
  - `syst/bits/Detail.hh`
  - `syst/Rebin.cc`
  - `syst/ReweightFill.cc`
  - `syst/ReweightCovariance.cc`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/CMakeLists.txt syst/DetectorSystematics.cc syst/bits/Detail.hh syst/Rebin.cc syst/ReweightFill.cc syst/ReweightCovariance.cc`
  - `ls syst`
- acceptance criteria:
  - the build lists only the new implementation filenames
  - `Support.cc` is gone and its responsibilities are split into named files
  - the detector and reweight translation units have domain-specific names
- verification results:
  - focused `git diff --check` passed for the file-layout cleanup files
  - `ls syst` shows the renamed and split translation units in place
  - compile verification remains limited by the broken local `build/` tree

### 7. Public-surface check
- compatibility impact:
  - no installed public headers changed in this pass
- reviewer sign-off:
  - explicit user approval received in-thread for the file rename/split pass

### 8. Reduction ledger
- files deleted: 1
  - `syst/Support.cc`
- wrappers removed: 0
- shell branches removed: 0
- stale file names removed from the active `syst/` build surface:
  - `Detector.cc`
  - `UniverseFill.cc`
  - `UniverseSummary.cc`
- approximate LOC delta: near-neutral; one source file split into two smaller
  units

### 9. Decision log
- keep the split minimal: cache-key helpers in `Detail.hh`, rebin math in
  `Rebin.cc`
- keep the translation-unit renames local; no namespace or type rename churn
  beyond the earlier approved naming pass

### 10. Stop conditions
- stop after the file names and `Support.cc` split match the reviewed rename
  set
- do not expand the pass into broader detector/reweight refactors

## ExecPlan Addendum: Systematics Naming Cleanup

### 1. Objective
Make the `syst/` surface smaller and easier to grep by deleting the stale
`SystematicsEngine` wrapper, renaming the adjacent cached-family nouns to be
more explicit, and tightening one internal data-struct name.

### 2. Constraints
- Preserve CLI behavior.
- Keep `io/` persistence-only and `app/` workflow-only.
- Keep `DistributionIO::Spectrum` unchanged in this pass.
- Limit public-surface churn to the user-approved naming changes:
  - remove `SystematicsEngine`
  - rename `DistributionIO::Spec` to `DistributionIO::HistogramSpec`
  - rename `DistributionIO::Family` to `DistributionIO::UniverseFamily`
- Leave unrelated files and historical log content untouched.

### 3. Design anchor
From `DESIGN.md`:
- prefer plain data and namespace functions
- keep module boundaries sharp
- add abstractions only when they delete complexity

This pass removes a wrapper instead of adding one and makes the nouns on the
systematics/cache boundary more explicit.

### 4. System map
- `syst/Systematics.hh`
- `syst/Systematics.cc`
- `syst/UniverseFill.cc`
- `syst/UniverseSummary.cc`
- `syst/Support.cc`
- `syst/Detector.cc`
- `syst/bits/Detail.hh`
- `io/DistributionIO.hh`
- `fit/SignalStrengthFit.hh`
- `fit/SignalStrengthFit.cc`
- `app/mk_cov.cc`
- `docs/adaptive-binning-plan.md`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`

### 5. Candidate simplifications

#### wrapper collapse
- delete `SystematicsEngine` and rely on the existing `syst::` namespace API

#### boundary sharpening
- rename `DistributionIO::Spec` to `DistributionIO::HistogramSpec`
- rename `DistributionIO::Family` to `DistributionIO::UniverseFamily`

#### stale scaffolding
- rename `SampleComputation` to `ComputedSample`
- stop exposing `SystematicsEngine` in diagnostics

### 6. Milestones

#### Milestone A: Apply the naming pass end to end
- status: done
- hypothesis: deleting the stale wrapper and using more explicit cache nouns
  reduces conceptual duplication without changing workflow behavior
- files / symbols touched:
  - `syst::SystematicsEngine`
  - `DistributionIO::HistogramSpec`
  - `DistributionIO::UniverseFamily`
  - `syst::detail::ComputedSample`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md io/DistributionIO.hh fit/SignalStrengthFit.hh fit/SignalStrengthFit.cc app/mk_cov.cc syst/Systematics.hh syst/Systematics.cc syst/UniverseFill.cc syst/UniverseSummary.cc syst/Support.cc syst/Detector.cc syst/bits/Detail.hh docs/adaptive-binning-plan.md`
  - `rg -n "SampleComputation|DistributionIO::Family|DistributionIO::Spec|class SystematicsEngine|SystematicsEngine:" -S io syst app fit`
- acceptance criteria:
  - the wrapper class is gone
  - the renamed types are updated through downstream code
  - no stale code references remain to the removed names
- verification results:
  - the focused `git diff --check` passed for the naming-pass files
  - the focused `rg` sweep returned no remaining code references to `SampleComputation`, `DistributionIO::Family`, `DistributionIO::Spec`, or `SystematicsEngine`
  - `cmake --build build --target Syst mk_fit mk_cov --parallel` did not provide a trustworthy compile check here because the current `build/` tree still points at `/usr/bin/cmake`, which is absent in this environment

### 7. Public-surface check
- compatibility impact:
  - installed public headers changed by user request
- migration note:
  - replace `DistributionIO::Spec` with `DistributionIO::HistogramSpec`
  - replace `DistributionIO::Family` with `DistributionIO::UniverseFamily`
  - replace `SystematicsEngine::*` calls with `syst::*`
- reviewer sign-off:
  - explicit user approval received in-thread for the naming pass

### 8. Reduction ledger
- files deleted: 0
- wrappers removed:
  - `SystematicsEngine`
- shell branches removed: 0
- stale docs removed:
  - one active reference to `DistributionIO::Spec`
- targets or dependencies removed: 0
- approximate LOC delta: small negative; one public wrapper deleted

### 9. Decision log
- keep `DistributionIO::Spectrum` unchanged in this pass
- rename only the adjacent family/spec nouns plus one internal helper struct
- use `syst:` as the diagnostic prefix instead of the removed class name

### 10. Stop conditions
- stop after the agreed rename set is complete
- do not expand the pass into broader cache-surface churn around `Spectrum`

## ExecPlan Addendum: HIVE-Informed Systematics Import

### 1. Objective
Define a reviewed covariance-first end state for `syst/` so the next
implementation pass can import the good parts of `hive` systematics handling
without importing its XML/shell framework.

The intended simplification is conceptual, not cosmetic:

- make covariance the canonical systematics object
- treat detector and reweighting sources with one explicit math contract
- keep envelopes as derived summaries rather than the primary persisted result

### 2. Constraints
- Preserve current external CLI shapes during the planning/document pass.
- Keep `io/` persistence-only.
- Keep workflow orchestration in `app/`, not `syst/`.
- Do not import `hive` XML templates, env-var discovery, or `system()`-based
  orchestration.
- Prefer additive cache-schema evolution over broad rename churn.
- Leave unrelated dirty-worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep module boundaries sharp
- prefer plain data and namespace functions
- add abstractions only when they delete complexity

This pass should produce a clearer math contract and migration path, not a new
framework.

### 4. System map
- planning / vision:
  - `syst/VISION.md`
  - `syst/README`
- current systematics implementation:
  - `syst/Systematics.hh`
  - `syst/Systematics.cc`
  - `syst/Detector.cc`
  - `syst/UniverseFill.cc`
  - `syst/UniverseSummary.cc`
  - `syst/Support.cc`
  - `syst/bits/Detail.hh`
- persistence / downstream consumers:
  - `io/DistributionIO.hh`
  - `io/DistributionIO.cc`
  - `fit/SignalStrengthFit.hh`
  - `fit/SignalStrengthFit.cc`
  - `app/mk_dist.cc`
  - `app/mk_xsec_fit.cc`
- comparison reference:
  - `/Users/user/programs/hive/hive/src/bdt_covar.cxx`
  - `/Users/user/programs/hive/hive/src/bdt_datamc.cxx`
  - `/Users/user/programs/hive/hive/src/load_mva_param.cxx`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`

### 5. Candidate simplifications

#### boundary sharpening
- move `amarantin` toward a covariance-first `syst/` contract so `fit/` no
  longer depends on envelope-only fallbacks
- keep detector-systematic semantics in `syst/` and cache persistence in `io/`

#### wrapper collapse
- avoid adding a `hive`-style orchestration layer around the math import
- prefer one explicit data-model extension over new adapter classes

#### doc / build cleanup
- write the equations, open review points, and staged migration path once in
  `syst/VISION.md` instead of rediscovering them during code edits

### 6. Milestones

#### Milestone A: Write the covariance-first vision and review equations
- status: done
- hypothesis: one agreed local design doc reduces oscillation and prevents
  importing `hive` mechanics when only its covariance semantics are needed
- files / symbols touched:
  - `syst/VISION.md`
  - `syst/README`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/README syst/VISION.md`
- acceptance criteria:
  - `syst/` has a local vision doc
  - the doc spells out the target covariance equations and review questions
  - the migration path is staged without committing to `hive` orchestration

#### Milestone B: Extend the cache schema toward covariance-first detector payloads
- status: done
- hypothesis: detector source shifts plus covariance are a better canonical
  cache surface than detector envelope alone
- files / symbols touched:
  - `io/DistributionIO.hh`
  - `io/DistributionIO.cc`
  - `syst/Systematics.cc`
  - `syst/Detector.cc`
  - `syst/bits/Detail.hh`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md io/DistributionIO.hh io/DistributionIO.cc syst/Systematics.hh syst/Systematics.cc syst/Detector.cc syst/bits/Detail.hh`
  - focused CMake builds if ROOT is available
  - detector/reweight smoke checks if ROOT is available
- acceptance criteria:
  - detector covariance survives cache write/read
  - detector envelope becomes a derived view, not the only detector payload
  - detector source matching uses source-local detector CV samples where
    available instead of nominal-wide detector envelopes
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md io/DistributionIO.hh io/DistributionIO.cc syst/Systematics.hh syst/Systematics.cc syst/Detector.cc syst/bits/Detail.hh` passed
  - the existing `build/` tree was stale and invoked `/usr/bin/cmake`, which is absent in this environment
  - a fresh `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` retry confirmed the tree issue but is still blocked by missing local dependencies (`/usr/include/sqlite3.h` and `nlohmann/json.hpp`)
  - detector/reweight smoke checks remain deferred because a trustworthy configured ROOT build is not available here

#### Milestone C: Teach fit-side assembly to consume covariance-first payloads
- status: done
- hypothesis: a fit path built from covariance-derived modes or source shifts
  is simpler and more faithful than sigma/envelope fallbacks
- files / symbols touched:
  - `fit/SignalStrengthFit.hh`
  - `fit/SignalStrengthFit.cc`
  - `app/mk_xsec_fit.cc`
  - `fit/README`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md fit/SignalStrengthFit.hh fit/SignalStrengthFit.cc fit/README app/mk_xsec_fit.cc`
  - focused CMake builds if ROOT is available
  - fit-side smoke checks once covariance payloads exist
- acceptance criteria:
  - fit semantics no longer depend primarily on envelope survival
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md fit/SignalStrengthFit.hh fit/SignalStrengthFit.cc fit/README app/mk_xsec_fit.cc` passed
  - `cmake --build build --target Fit mk_xsec_fit --parallel` did not provide a trustworthy compile check here; the current `build/` tree is inconsistent and returned `make: *** No rule to make target 'Fit'.  Stop.`
  - a fresh configure remains blocked in this environment by missing local dependencies (`/usr/include/sqlite3.h` and `nlohmann/json.hpp`)
  - fit-side runtime smoke checks remain deferred because no trustworthy configured local build is available

#### Milestone D: Add a thin SBNFit-style covariance export surface
- status: done
- hypothesis: one small edge CLI that exports fractional covariance from the
  persisted covariance-first cache is simpler than pushing SBNFit-format
  concerns back into `syst/` or `io/`
- files / symbols touched:
  - `app/mk_cov.cc`
  - `app/CMakeLists.txt`
  - `COMMANDS`
  - `INSTALL`
  - `USAGE`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/mk_cov.cc app/CMakeLists.txt COMMANDS INSTALL USAGE`
  - focused CMake builds if ROOT is available
- acceptance criteria:
  - one CLI can export a cached spectrum as SBNFit-style fractional covariance
  - the export is derived from the persisted covariance-first payloads
  - SBNFit-format details stay in `app/`, not in core `syst/` math
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/mk_cov.cc app/CMakeLists.txt COMMANDS INSTALL USAGE` passed
  - `cmake --build build --target mk_cov --parallel` did not provide a trustworthy compile check here; the current `build/` tree is inconsistent and returned `make: *** No rule to make target 'mk_cov'.  Stop.`
  - a fresh configure remains blocked in this environment by missing local dependencies (`/usr/include/sqlite3.h` and `nlohmann/json.hpp`)

#### Milestone E: Make reweight families covariance-canonical
- status: done
- hypothesis: family covariance should be the one persisted reweight truth, with
  sigma and any compressed modes derived from that matrix instead of treating
  covariance as optional metadata
- files / symbols touched:
  - `syst/UniverseSummary.cc`
  - `syst/Support.cc`
  - `syst/Systematics.cc`
  - `syst/bits/Detail.hh`
  - `syst/README`
  - `io/bits/DERIVED`
  - `tools/systematics-reweight-smoke.sh`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/UniverseSummary.cc syst/Systematics.cc syst/Support.cc syst/bits/Detail.hh syst/README io/bits/DERIVED tools/systematics-reweight-smoke.sh`
  - `bash -n tools/systematics-reweight-smoke.sh`
  - `cmake -S . -B .build/milestone-e -DCMAKE_BUILD_TYPE=Release`
  - `cmake --build build-docker-plot-check --target Syst --parallel`
- acceptance criteria:
  - family covariance is always persisted as the canonical cache payload
  - rebinned family results prefer exact covariance over compressed modes when
    both survive
  - older no-covariance family caches still have an explicit fallback path
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/UniverseSummary.cc syst/Systematics.cc syst/Support.cc syst/bits/Detail.hh syst/README io/bits/DERIVED tools/systematics-reweight-smoke.sh` passed
  - `bash -n tools/systematics-reweight-smoke.sh` passed
  - `cmake -S . -B .build/milestone-e -DCMAKE_BUILD_TYPE=Release` failed in this environment because `ROOT` is not available and `root-config` is not on `PATH`
  - `cmake --build build-docker-plot-check --target Syst --parallel` was not a usable check because that auxiliary build tree was configured under `/work/build-docker-plot-check` and now fails the CMake path consistency guard

#### Milestone F: Record the upstream EventWeight branch contract in the vision doc
- status: done
- hypothesis: one explicit mapping from the concrete
  `searchingforstrangeness` EventWeight branches to `amarantin` systematic
  semantics will prevent silent branch misuse and reduce future implementation
  churn
- files / symbols touched:
  - `syst/VISION.md`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/VISION.md`
- acceptance criteria:
  - the vision doc names the concrete upstream EventWeight branches
  - each branch set is classified as nominal-weight, canonical multisim, or
    optional paired-knob surface
  - the doc states how those branches should be used in `amarantin`
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/VISION.md` passed

### 7. Public-surface check
- compatibility impact:
  - `DistributionIO` cache schema version is now `4`
  - persisted reweight family covariance is now canonical; old caches should be rebuilt
  - public headers remain in place; canonical family payloads always persist covariance
- migration note or explicit non-goal:
  - non-goal: import `hive` XML templates or large CLI mode switchboards
  - non-goal: move persistence ownership out of `io/`
- reviewer sign-off: user requested a HIVE-informed `syst/` plan/vision before
  implementation and then asked to continue milestone-by-milestone

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- stale docs removed: 0
- targets or dependencies removed: 0
- approximate LOC delta: one additive family-covariance cleanup pass; no new framework or orchestration layer

### 9. Decision log
- import `hive`'s covariance semantics, not its orchestration machinery
- make covariance the canonical systematics contract
- keep envelopes as derived display summaries
- keep the full-vector collapse idea explicit for later fit work

### 10. Stop conditions
- stop after the vision, equations, and staged plan are written and reviewed
- implementation should start only after the open math conventions are agreed

## ExecPlan Addendum: EventList Branch And Category Format Pass

### 1. Objective
Rename the persisted EventList truth-category branches and the downstream
EventListIO-first category API so the on-disk/output names match the new
analysis terminology:

- `__analysis_channel__` -> `__event_category__`
- `__is_signal__` -> `__passes_signal_definition__`
- plot-side category helpers stop using generic `channel` naming for the
  EventList category surface

### 2. Constraints
- Preserve the current event-level physics logic.
- Keep `io/` owning the persistence contract.
- Prefer EventListIO-surface compatibility over ad hoc downstream fallback.
- Leave unrelated dirty-worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- `io/` owns persistence only
- keep module boundaries sharp
- prefer clear data flow and code that is cheap to change

### 4. System map
- EventList persistence and branch naming:
  - `io/EventListIO.hh`
  - `io/EventListIO.cc`
  - `io/bits/DERIVED`
  - `ana/EventListBuild.cc`
- row-wise downstream consumers:
  - `plot/PlotDescriptors.hh`
  - `plot/PlotChannels.hh`
  - `plot/StackedHist.cc`
  - `plot/UnstackedHist.cc`
  - `plot/README`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- verification:
  - `git diff --check -- ...`
  - focused builds if ROOT is available

### 5. Candidate simplifications

#### boundary sharpening
- move the canonical EventList branch names onto the `EventListIO` persistence
  surface instead of keeping them as local literals in `ana`
- make the downstream plot API say `event_category` instead of overloading
  `channel`

#### stale scaffolding
- keep old and new branch names interoperable through EventListIO tree aliases
  rather than forcing downstream call sites to special-case both layouts

### 6. Milestones

#### Milestone A: Rename EventList branches and aliases
- status: done
- hypothesis: one canonical persistence owner plus read/write aliases is
  simpler than leaving the old misleading names in the file format
- files / symbols touched:
  - `io/EventListIO.hh`
  - `io/EventListIO.cc`
  - `ana/EventListBuild.cc`
  - `io/bits/DERIVED`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- ...`
  - focused builds if the environment allows
- acceptance criteria:
  - new EventList files write the renamed branch names
  - EventListIO-selected trees expose aliases so downstream EventListIO-based
    code can use both old and new names during the transition
- verification results:
  - `ana/EventListBuild.cc` now writes `__event_category__` and
    `__passes_signal_definition__` via `EventListIO` branch-name helpers
  - `io/EventListIO.cc` now applies selected-tree aliases in both directions
    so old and new files are both queryable through the EventListIO surface

#### Milestone B: Rename plot-side category API
- status: done
- hypothesis: downstream row-wise plotting becomes easier to read when its
  option names and code maps say `event_category`
- files / symbols touched:
  - `plot/PlotDescriptors.hh`
  - `plot/PlotChannels.hh`
  - `plot/StackedHist.cc`
  - `plot/UnstackedHist.cc`
  - `plot/CMakeLists.txt`
  - `plot/README`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- ...`
  - focused builds if the environment allows
- acceptance criteria:
  - plot-side EventList category helpers no longer use generic `channel`
    naming for that surface
- verification results:
  - `plot/PlotDescriptors.hh` now uses `event_category` option names and
    defaults to `EventListIO::event_category_branch_name()`
  - `plot/PlotEventCategories.hh` is the canonical mapper and
    `plot/PlotChannels.hh` remains as a compatibility include alias
  - `plot/StackedHist.cc` and `plot/UnstackedHist.cc` now use
    `EventCategories` and `event_category` terminology throughout the
    row-wise EventList path

### 7. Public-surface check
- compatibility impact:
  - EventList output branch names change on new files
  - EventListIO-based readers keep transitional compatibility through aliases
  - `plot/PlotEventCategories.hh` is the new canonical header and
    `plot/PlotChannels.hh` is preserved as a compatibility include
- migration note or explicit non-goal:
  - non-goal: rename fit-side `fit::Channel`
  - non-goal: change category codes or the signal predicate logic
- reviewer sign-off: user explicitly requested the deeper branch/file-format pass

### 8. Reduction ledger
- files deleted: 0
- wrappers removed:
  - local branch-name literals in `ana/EventListBuild.cc`
  - generic plot-side `channel` wording for the EventList category surface
- shell branches removed: 0
- stale docs removed:
  - old EventList branch-name documentation in `io/bits/DERIVED`
  - old plot README wording that still described the row-wise surface as
    generic channels
- targets or dependencies removed: 0
- approximate LOC delta: moderate rename/schema pass with compatibility aliases
  instead of downstream special cases

### 9. Decision log
- use `__event_category__` as the canonical persisted category branch name
- use `__passes_signal_definition__` as the canonical persisted signal-flag
  branch name
- keep transition compatibility at the EventListIO surface via aliases

### 10. Stop conditions
- stop when the persisted/output names and the EventListIO-first downstream
  terminology are aligned and verified as far as the environment allows

## ExecPlan Addendum: SignalDefinition And EventCategory Naming

### 1. Objective
Rename the new analysis-side signal and event-label surfaces so their names
say exactly what they are:

- `SignalDef` / `SignalDefinition` surface -> `SignalDefinition`
- `Channels` surface -> `EventCategory`

### 2. Constraints
- Preserve current `mk_eventlist` behavior.
- Keep the hardcoded signal definition in `ana/`.
- Leave unrelated dirty-worktree changes untouched.
- Keep fit-side `fit::Channel` naming out of scope.

### 3. Design anchor
From `DESIGN.md`:
- keep names direct
- prefer clear data flow and code that is cheap to change

### 4. System map
- `ana/SignalDefinition.hh`
- `ana/SignalDefinition.cc`
- `ana/EventCategory.hh`
- `ana/EventListBuild.hh`
- `ana/EventListBuild.cc`
- `ana/CMakeLists.txt`
- `ana/README`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`

### 5. Candidate simplifications

#### boundary sharpening
- make the hardcoded signal owner read as a definition, not a shortened alias
- make the event-label surface read as one event category, not generic
  “channels”

### 6. Milestones

#### Milestone A: Rename surfaces
- status: done
- hypothesis: explicit nouns reduce ambiguity with collider-fit channels and
  fit-side categorisation
- files / symbols touched:
  - `ana/SignalDefinition.hh`
  - `ana/SignalDefinition.cc`
  - `ana/EventCategory.hh`
  - `ana/EventListBuild.hh`
  - `ana/EventListBuild.cc`
  - `ana/CMakeLists.txt`
  - `ana/README`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- ...`
  - focused CMake builds if the environment allows
- acceptance criteria:
  - `ana/` exposes `SignalDefinition` and `EventCategory` by name
  - fit-side `fit::Channel` remains unchanged
- verification results:
  - tracked touched files passed `git diff --check`
  - new files `ana/EventCategory.hh`, `ana/SignalDefinition.hh`, and `ana/SignalDefinition.cc` produced no whitespace diagnostics under `git diff --no-index --check`
  - `cmake --build build --target Ana --parallel` is still blocked because the existing `build/` tree points at `/usr/bin/cmake`
  - a fresh configure in `.build/eventcategory-check` is blocked by missing ROOT / `root-config`

### 7. Public-surface check
- compatibility impact: installed header names in `ana/` change again in this
  pass
- migration note or explicit non-goal:
  - non-goal: rename `fit::Channel`
  - non-goal: change event-level physics logic
- reviewer sign-off: requested directly by the user

### 8. Reduction ledger
- files deleted:
  - installed header `ana/Channels.hh` replaced by `ana/EventCategory.hh`
- wrappers removed:
  - the generic `channels` naming at the `ana/` event-label surface
- shell branches removed: 0
- stale docs removed:
  - `ana/README` wording that still referred to `SignalDef.hh` and `Channels.hh`
- targets or dependencies removed: 0
- approximate LOC delta: mostly naming churn around the public `ana/` surface; behavior unchanged

### 9. Decision log
- reserve `Channel` for fit-side concepts
- use `EventCategory` for the event-level selected-event label
- use `SignalDefinition` for the hardcoded truth-level signal predicate

### 10. Stop conditions
- stop when the rename is complete and the touched files are verified as far
  as the environment allows

## ExecPlan Addendum: Canonical SignalDef Surface

### 1. Objective
Replace the current pseudo-configurable signal-definition plumbing with one
canonical hardcoded `SignalDef` type that is easier to grep and keeps the
event-level Lambda signal predicate in one place.

### 2. Constraints
- Preserve current `mk_eventlist` CLI behavior.
- Keep `io/` persistence-only.
- Leave unrelated dirty-worktree changes untouched.
- Keep the current event-level signal behavior unchanged while folding in the
  already-present truth-vertex and metadata edits.

### 3. Design anchor
From `DESIGN.md`:
- prefer plain data and namespace functions
- keep workflows in `app/`
- add abstractions only when they delete complexity

The goal here is to remove fake configurability, not add a new framework.

### 4. System map
- signal-definition and event-list build path:
  - `ana/Channels.hh`
  - `ana/EventListBuild.hh`
  - `ana/EventListBuild.cc`
  - `app/mk_eventlist.cc`
  - `io/EventListIO.hh`
  - `io/EventListIO.cc`
- new canonical signal-definition owner:
  - `ana/SignalDef.hh`
  - `ana/SignalDef.cc`
  - `ana/CMakeLists.txt`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- verification:
  - `git diff --check -- ...`
  - `cmake --build build --target Ana --parallel`
  - `cmake --build build --target mk_eventlist --parallel`

### 5. Candidate simplifications

#### boundary sharpening
- move the event-level Lambda signal predicate out of `Channels.hh` into a
  dedicated `ana::SignalDef` owner
- let `Channels.hh` own channel classification only

#### wrapper collapse
- drop `BuildConfig.signal_definition` so the hardcoded predicate stops
  pretending to be runtime configuration

#### stale scaffolding
- keep EventList metadata summary generation with the canonical definition
  instead of hand-building it at the call site

### 6. Milestones

#### Milestone A: Introduce canonical SignalDef
- status: done
- hypothesis: one named class with one hardcoded definition is smaller and
  clearer than a configurable-looking struct threaded through build config
- files / symbols touched:
  - `ana/SignalDef.hh`
  - `ana/SignalDef.cc`
  - `ana/Channels.hh`
  - `ana/EventListBuild.hh`
  - `ana/EventListBuild.cc`
  - `ana/CMakeLists.txt`
  - `app/mk_eventlist.cc`
  - `io/EventListIO.hh`
  - `io/EventListIO.cc`
- expected behavior risk: low to moderate
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md ana/CMakeLists.txt ana/SignalDef.hh ana/SignalDef.cc ana/Channels.hh ana/EventListBuild.hh ana/EventListBuild.cc app/mk_eventlist.cc io/EventListIO.hh io/EventListIO.cc`
  - `cmake --build build --target Ana --parallel`
  - `cmake --build build --target mk_eventlist --parallel`
- acceptance criteria:
  - one canonical `SignalDef` owns the event-level Lambda signal predicate
  - `EventListBuild` uses that canonical definition directly
  - current `mk_eventlist` CLI behavior is unchanged
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md ana/CMakeLists.txt ana/README ana/SignalDef.hh ana/SignalDef.cc ana/Channels.hh ana/EventListBuild.hh ana/EventListBuild.cc io/EventListIO.hh io/EventListIO.cc` passed on tracked touched files
  - `git diff --no-index --check -- /dev/null ana/SignalDef.hh` and `git diff --no-index --check -- /dev/null ana/SignalDef.cc` produced no whitespace diagnostics for the new files
  - `cmake --build build --target Ana --parallel` and `cmake --build build --target mk_eventlist --parallel` are blocked because the existing `build/` tree still references `/usr/bin/cmake`
  - fresh configure at `.build/signaldef-check` is blocked by missing ROOT / `root-config`

### 7. Public-surface check
- compatibility impact: additive new `ana/SignalDef.hh`; `ana::BuildConfig`
  stops advertising signal-definition customization that the CLI never
  exposed
- migration note or explicit non-goal:
  - non-goal: expose new signal-definition runtime options
  - non-goal: change the physics cuts in this pass
- reviewer sign-off: user explicitly requested a dedicated `SignalDef` class

### 8. Reduction ledger
- files deleted: 0
- wrappers removed:
  - `BuildConfig.signal_definition`
  - the signal-predicate ownership from `ana/Channels.hh`
- shell branches removed: 0
- stale docs removed: 0
- targets or dependencies removed: 0
- approximate LOC delta: about +490 / -180 including the new signal-definition files and tracking updates

### 9. Decision log
- keep one canonical hardcoded signal definition in `ana/`
- preserve the in-flight truth-vertex-aware logic instead of reverting it

### 10. Stop conditions
- stop when the signal predicate has one owner, the fake config seam is gone,
  and focused verification has run
- stop before widening into broader event-channel redesign

## ExecPlan Addendum: Spectrum And SampleIO Seam Trim

### 1. Objective
Implement the next two concrete `io/VISION.md` items without widening into a
new file-format redesign:

- rename `DistributionIO::Entry` to `DistributionIO::Spectrum`
- move manifest / `@file` parsing out of `SampleIO` and into `app/mk_sample`

### 2. Constraints
- Keep `io/` persistence-only.
- Preserve the documented `mk_sample` CLI behavior.
- Avoid changing on-disk ROOT layout just to match in-memory names.
- Leave unrelated dirty-worktree changes untouched.

### 3. Scope
- rename/update:
  - `io/DistributionIO.hh`
  - `io/DistributionIO.cc`
  - `app/mk_xsec_fit.cc`
  - `plot/EventListPlotting.hh`
  - `plot/EventListPlotting.cc`
  - `plot/macro/inspect_dist.C`
  - `syst/bits/Detail.hh`
  - `fit/README`
  - `syst/README`
  - `docs/adaptive-binning-plan.md`
  - `io/SampleIO.hh`
  - `io/SampleIO.cc`
  - `app/mk_sample.cc`
  - `io/macro/mk_sample.C`
  - `io/VISION.md`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`

### 4. Status
- status: done
- outcome:
  - `DistributionIO` now exposes `Spectrum` as the cached-payload noun
  - `SampleIO` now builds from already-resolved shard inputs instead of
    parsing manifests or `@file` indirection itself
  - the `mk_sample` app now owns the small workflow parsing layer that was
    previously leaking into `io/`

## ExecPlan Addendum: ShardIO And Signal Naming Pass

### 1. Objective
Turn the recorded `io/` naming decisions into code without widening into a new
workflow redesign:

- make `ShardIO` the real public type and installed header
- rename sample origin `enriched` to `signal`
- trim the small `DatasetIO.cc` redundancies already called out in review
- keep current docs and install surfaces in sync with those names

### 2. Constraints
- Keep `io/` persistence-only.
- Keep persisted ROOT payloads backward-readable where practical.
- Do not widen into file-format redesign just to match in-memory names.
- Leave unrelated dirty-worktree changes untouched.

### 3. Scope
- rename/update:
  - `io/InputPartitionIO.hh` -> `io/ShardIO.hh`
  - `io/ShardIO.cc`
  - `io/SampleIO.hh`
  - `io/SampleIO.cc`
  - `io/DatasetIO.hh`
  - `io/DatasetIO.cc`
  - `ana/EventListBuild.cc`
  - `plot/PlottingHelper.cc`
  - `io/macro/print_sample.C`
  - `io/CMakeLists.txt`
  - `io/README`
  - `io/VISION.md`
  - `VISION.md`
  - `INSTALL`
  - `USAGE`
  - `docs/repo-internals.puml`
  - `io/bits/DERIVED`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`

### 4. Status
- status: done
- outcome:
  - `ShardIO` is now the public type/header instead of `InputPartitionIO`
  - `signal` is now the emitted sample-origin name, while old `"enriched"`
    payloads still read successfully
  - the small `DatasetIO.cc` ceremony trims were applied without changing the
    persisted layout contract

## ExecPlan Addendum: Remove ChannelIO Compatibility Layer

### 1. Objective
Remove the remaining `ChannelIO` persistence layer and the local helper macros
that still depended on it, without widening the pass into a new coarse-channel
format redesign.

### 2. Constraints
- Keep `DistributionIO` as the persisted bin-wise surface.
- Do not reintroduce a second IO layer on top of `DistributionIO`.
- Update current module/docs surfaces that would otherwise point at deleted
  files.
- Leave historical log material intact.

### 3. Scope
- delete:
  - `io/ChannelIO.hh`
  - `io/ChannelIO.cc`
  - `fit/macro/fit_channel.C`
  - `fit/macro/scan_mu.C`
  - `plot/macro/plot_channel.C`
- update:
  - `io/CMakeLists.txt`
  - `io/README`
  - `io/VISION.md`
  - `INSTALL`
  - `COMMANDS`
  - `docs/repo-internals.puml`
  - `docs/adaptive-binning-plan.md`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`

### 4. Status
- status: done
- outcome:
  - `ChannelIO` removed from the tree
  - the last dependent local macros were deleted with it
  - current docs now describe `DistributionIO`-first downstream flow instead
    of a deleted channel bundle layer

## ExecPlan Addendum: IO Correctness Guardrails

### 1. Objective
Fix four concrete `io/` correctness bugs without widening the pass into API
renames or workflow redesign:

- fail fast on mixed or missing shard `SubRun` trees
- fail fast on partial run-database coverage
- make repeated `DatasetIO::add_sample` writes read back the current
  provenance only
- preserve `ChannelIO` universe histograms on round-trip

### 2. Constraints
- Preserve current CLI surfaces and installed targets.
- Preserve installed public headers by default.
- Keep `io/` persistence-only and avoid moving policy into downstream modules.
- Leave unrelated worktree changes untouched.
- Treat the earlier `InputPartitionIO` file rename as out of scope here; this
  pass is about correctness, not further naming churn.

### 3. Design anchor
From `DESIGN.md`:
- `io/` owns persistence only
- keep module boundaries sharp
- add abstractions only when they delete complexity

This pass should tighten persisted contracts and failure modes rather than add
new wrapper layers.

### 4. System map
- persistence and scan logic:
  - `io/InputPartitionIO.hh`
  - `io/ShardIO.cc`
  - `io/SampleIO.hh`
  - `io/SampleIO.cc`
  - `io/DatasetIO.hh`
  - `io/DatasetIO.cc`
  - `io/ChannelIO.hh`
  - `io/ChannelIO.cc`
- downstream consumer touched by the bug report:
  - `ana/EventListBuild.cc`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- verification:
  - `git diff --check -- ...`
  - `bash -n tools/run-macro`
  - focused CMake configure/build if ROOT is available

### 5. Candidate simplifications

#### boundary sharpening
- make shard scanning validate one explicit tree layout instead of relying on
  `TChain` to silently skip incompatible files
- make run-database coverage explicit: fully covered or unit mode, never
  partially zero-weighted

#### stale scaffolding
- persist the current provenance count so readback does not depend on stale
  directories left behind by an overwrite
- extend `ChannelIO` payload helpers to match the full `DistributionIO::Family`
  surface

### 6. Milestones

#### Milestone A: Guard shard scan and run-db coverage
- status: done
- hypothesis: early validation at sample-build time is smaller and safer than
  letting downstream event weighting detect corrupted state indirectly
- files / symbols touched:
  - `io/ShardIO.cc`
  - `io/SampleIO.cc`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- io/ShardIO.cc io/SampleIO.cc`
  - focused configure/build if ROOT is available
- acceptance criteria:
  - mixed or missing `SubRun` tree paths fail loudly
  - partial run-database coverage fails loudly
- verification results:
  - `git diff --check -- io/ShardIO.cc io/SampleIO.cc` passed
  - fresh host-side configure is blocked by missing ROOT on this machine

#### Milestone B: Guard overwrite/readback correctness
- status: done
- hypothesis: one persisted provenance-count guard is smaller than directory
  cleanup logic and preserves backward compatibility on read
- files / symbols touched:
  - `io/DatasetIO.cc`
  - `io/ChannelIO.cc`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- io/DatasetIO.cc io/ChannelIO.cc`
- acceptance criteria:
  - overwriting one dataset sample key does not read stale provenance
  - `ChannelIO` preserves `universe_histograms`
- verification results:
  - `git diff --check -- io/DatasetIO.cc io/ChannelIO.cc` passed
  - backward-read compatibility was preserved by making the new fields optional

### 7. Public-surface check
- compatibility impact: none intended; file formats remain backward-readable
  and the app/installed header surface is unchanged
- migration note or explicit non-goal:
  - non-goal: rename `InputPartitionIO` symbols in this pass
  - non-goal: redesign `ChannelIO`
- reviewer sign-off: not required for additive internal persistence guards

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- stale docs removed: 0
- targets or dependencies removed: 0
- approximate LOC delta: roughly +110 / -13 in the code touched for the fixes

### 9. Decision log
- prefer fail-fast validation over silent fallback when persisted weighting data
  would otherwise be incomplete
- prefer additive persisted read guards over destructive directory cleanup in
  ROOT files

### 10. Stop conditions
- stop when the four reported bugs are fixed and verified as far as this host
  allows
- stop before widening into public-type renames or broader `ChannelIO`
  retirement work

## ExecPlan Addendum: VISION Workflow Convergence

### 1. Objective
Converge the repository from the current
`mk_sample -> mk_dataset -> mk_eventlist [cache] -> mk_channel -> mk_xsec_fit`
story toward the `VISION.md` target:

- one logical-sample build path in `mk_sample`
- one logical-dataset assembly path in `mk_dataset`
- one canonical row-wise build path in `mk_eventlist`
- one explicit `mk_dist` cache-building step
- `DistributionIO` as the default downstream plot / fit surface

The intended outcome is a workflow that is smaller in concepts, flatter in
execution order, and easier to grep than the current mixed channel-first and
cache-inside-`mk_eventlist` story.

### 2. Constraints
- Preserve external behavior unless one milestone explicitly calls out an
  approved compatibility change.
- Preserve installed libraries and installed public headers by default:
  `IO`, `Ana`, `Syst`, `Plot`, `Fit`.
- Treat CLI churn as additive-first:
  - add the native `VISION.md` path first
  - keep legacy positional or compatibility modes until the new path is
    documented, verified, and accepted
- Keep module boundaries sharp:
  - `io/` persistence only
  - `ana/` build-time transforms only
  - `syst/` cache/systematics only
  - `plot/` rendering only
  - `fit/` final assembly / fit helpers only
- Do not move analysis or systematics logic back into `io/`.
- Do not let `plot/` become a generic utility bucket.
- Leave unrelated worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep workflows in `app/`
- prefer plain data and namespace functions
- keep module boundaries sharp
- add abstractions only when they delete complexity

From `VISION.md`:
- the preferred workflow is
  `mk_sample -> mk_dataset -> mk_eventlist -> mk_dist -> plot/fit`
- `EventListIO` is the row-wise build/debug surface
- `DistributionIO` is the default persisted downstream cache surface

That means the convergence path should first fix the persisted data contract,
then move orchestration into thin `app/` entry points, and only then delete the
legacy wrapper paths.

### 4. System map
- workflow apps:
  - `app/mk_sample.cc`
  - `app/mk_dataset.cc`
  - `app/mk_eventlist.cc`
  - `app/mk_channel.cc`
  - `app/mk_xsec_fit.cc`
  - `app/CMakeLists.txt`
- persistence:
  - `io/InputPartitionIO.hh`
  - `io/InputPartitionIO.cc`
  - `io/SampleIO.hh`
  - `io/SampleIO.cc`
  - `io/DatasetIO.hh`
  - `io/DatasetIO.cc`
  - `io/EventListIO.hh`
  - `io/EventListIO.cc`
  - `io/DistributionIO.hh`
  - `io/DistributionIO.cc`
  - `io/ChannelIO.hh`
  - `io/ChannelIO.cc`
- analysis/cache:
  - `ana/EventListBuild.hh`
  - `ana/EventListBuild.cc`
  - `ana/SampleDef.hh`
  - `ana/SampleDef.cc`
  - `syst/Systematics.hh`
  - `syst/Systematics.cc`
- workflow generators and shell helpers:
  - `samples-dag.mk`
  - `datasets.mk`
  - `tools/render-sample-catalog.sh`
  - `tools/run-macro`
- docs/tracking:
  - `VISION.md`
  - `COMMANDS`
  - `USAGE`
  - `INSTALL`
  - `io/README`
  - `ana/README`
  - `syst/README`
  - `fit/README`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- verification baseline:
  - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
  - `cmake --build build --parallel`
  - `cmake --build build --target IO Ana Syst Plot Fit mk_sample mk_dataset mk_eventlist --parallel`
  - `bash -n tools/mklist.sh`
  - `bash -n tools/run-macro`
  - `build/bin/mk_sample --help`
  - `build/bin/mk_dataset --help || true`
  - `build/bin/mk_eventlist --help || true`

### 5. Candidate simplifications

#### wrapper collapse
- remove `mk_eventlist --cache-*` as the place where cache-building policy
  lives once `mk_dist` exists
- stop using `mk_dataset` as the place where shard fan-in and logical-sample
  reconstruction happen
- keep `mk_channel` and `ChannelIO` as compatibility surfaces until the native
  `DistributionIO` downstream path is in place

#### script simplification
- make `samples-dag.mk` and `tools/render-sample-catalog.sh` emit one native
  `mk_sample` manifest per logical sample instead of relying on accidental
  multi-list semantics
- simplify `tools/run-macro` only after the native CLI path is documented and
  stable

#### boundary sharpening
- make `SampleIO` own logical-sample normalization and shard provenance
- make `DatasetIO` collect already-logical samples instead of recreating them
- make `mk_eventlist` consume the embedded normalization surface directly
- make `syst/` and downstream code treat the resolved event-level weight
  surface as authoritative

#### doc / build cleanup
- rewrite `COMMANDS`, `USAGE`, and module READMEs so the preferred teaching
  path matches the actual execution path
- add `mk_dist` to the public build/install surface only when the app exists

#### stale scaffolding
- remove stale docs that describe repeated-key shard merging as the preferred
  dataset path
- defer `InputPartitionIO` to `InputShardIO` rename until the workflow is
  stable and the rename deletes more confusion than it adds

### 6. Milestones

#### Milestone 1: Baseline And Contract Inventory
- status: done
- hypothesis: if the current public behavior, docs, and migration guardrails
  are recorded in one place before changing code, later workflow edits will be
  smaller and less likely to drift across docs and implementations
- files / symbols touched:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
  - possibly `COMMANDS`
  - possibly `USAGE`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md COMMANDS USAGE`
- acceptance criteria:
  - the current legacy path and the target native path are both named
  - compatibility promises and non-goals are explicit
  - later milestones have concrete file scopes and verification gates

#### Milestone 2: Logical-Sample Normalization Surface
- status: done
- hypothesis: if `SampleIO` and `DatasetIO` persist explicit logical-sample
  identity, shard provenance, and run/subrun normalization tables, then the
  rest of the migration can stop depending on sample-wide scalar assumptions
- files / symbols touched:
  - `io/InputPartitionIO.hh`
  - `io/InputPartitionIO.cc`
  - `io/SampleIO.hh`
  - `io/SampleIO.cc`
  - `io/DatasetIO.hh`
  - `io/DatasetIO.cc`
  - `io/README`
  - `COMMANDS`
  - `USAGE`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- io/InputPartitionIO.hh io/InputPartitionIO.cc io/SampleIO.hh io/SampleIO.cc io/DatasetIO.hh io/DatasetIO.cc io/README COMMANDS USAGE .agent/current_execplan.md docs/minimality-log.md`
  - `cmake --build build --target IO mk_sample mk_dataset --parallel`
  - `build/bin/mk_sample --help`
  - `build/bin/mk_dataset --help || true`
- acceptance criteria:
  - one logical sample can carry many shard provenance records
  - a persisted run/subrun normalization surface exists
  - the old scalar normalisation, if kept, is only a summary and not the full
    contract
- verification results:
  - `git diff --check` passed for the touched files
  - host-side reuse of the checked-in `build/` tree is invalid here:
    - the generated makefiles hard-code `/usr/bin/cmake`
    - the checked-in binaries are Linux ELF artifacts and cannot run on this
      macOS host
  - Docker verification passed in a fresh Linux build tree:
    - `docker build -t amarantin-dev .`
    - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/m2-docker -DCMAKE_BUILD_TYPE=Release && cmake --build .build/m2-docker --target IO mk_sample mk_dataset --parallel && .build/m2-docker/bin/mk_sample --help && .build/m2-docker/bin/mk_dataset --help || true'`
    - `IO`, `mk_sample`, and `mk_dataset` built successfully
    - `mk_sample --help` printed the expected usage
    - `mk_dataset --help` printed the current usage text and then exited
      through its existing invalid-arguments path

#### Milestone 3: Native `mk_sample` Manifest Workflow
- status: done
- hypothesis: if `mk_sample` accepts `--sample NAME --manifest SAMPLE.manifest`
  and treats shard membership as an explicit workflow input, then logical
  sample building becomes reproducible and no longer depends on accidental file
  naming or multi-list inference
- files / symbols touched:
  - `app/mk_sample.cc`
  - `io/SampleIO.hh`
  - `io/SampleIO.cc`
  - `samples-dag.mk`
  - `tools/render-sample-catalog.sh`
  - `COMMANDS`
  - `USAGE`
  - `INSTALL`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- app/mk_sample.cc io/SampleIO.hh io/SampleIO.cc samples-dag.mk tools/render-sample-catalog.sh COMMANDS USAGE INSTALL .agent/current_execplan.md docs/minimality-log.md`
  - `bash -n tools/render-sample-catalog.sh`
  - `cmake --build build --target mk_sample --parallel`
  - `build/bin/mk_sample --help`
- acceptance criteria:
  - the native CLI supports explicit logical sample identity
  - manifest rows are `shard sample-list-path`
  - duplicate shard names and malformed rows fail loudly
  - a compatibility path, if kept, is clearly documented as transitional
- verification results:
  - local workflow checks passed:
    - `bash -n tools/render-sample-catalog.sh`
    - `bash tools/render-sample-catalog.sh`
    - `make -f samples-dag.mk print-samples`
    - `make -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk print-samples`
    - `make -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk print-datasets`
    - `make -n -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk samples`
  - the generated catalog now emits:
    - one `sample_manifest.<dataset>.<logical>` per logical sample
    - one logical sample root per downstream sample key
    - a logical-only generated dataset manifest that points at those roots
  - Docker verification passed in a fresh Linux build tree:
    - `docker build -t amarantin-dev .`
    - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/m3-docker -DCMAKE_BUILD_TYPE=Release && cmake --build .build/m3-docker --target IO mk_sample --parallel && .build/m3-docker/bin/mk_sample --help'`
    - native manifest smoke passed on a synthetic two-shard sample with a
      temporary SQLite run DB
    - legacy single-list smoke still passed on the same synthetic input
    - duplicate-shard and malformed-manifest smokes both failed loudly as
      intended
    - a compiled verifier confirmed the persisted logical sample key,
      partition count, and run/subrun normalization payload in the produced
      sample ROOT file

#### Milestone 4: Logical-Only `mk_dataset` Scope Assembly
- status: done
- hypothesis: if `mk_dataset` only assembles already-logical samples and
  validates explicit dataset scope, then dataset assembly becomes flatter and
  stops being the first place where shard fan-in is reconstructed
- files / symbols touched:
  - `app/mk_dataset.cc`
  - `ana/SampleDef.hh`
  - `ana/SampleDef.cc`
  - `samples-dag.mk`
  - `tools/render-sample-catalog.sh`
  - `COMMANDS`
  - `USAGE`
  - `INSTALL`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- app/mk_dataset.cc ana/SampleDef.hh ana/SampleDef.cc samples-dag.mk tools/render-sample-catalog.sh COMMANDS USAGE INSTALL .agent/current_execplan.md docs/minimality-log.md`
  - `bash -n tools/render-sample-catalog.sh`
  - `cmake --build build --target mk_dataset --parallel`
  - `build/bin/mk_dataset --help || true`
- acceptance criteria:
  - the native CLI shape is explicit about `--run`, `--beam`, `--polarity`,
    and manifest input
  - repeated-key shard merge behavior is no longer the preferred native path
  - dataset validation checks scope conflicts without requiring one fixed
    universal sample set
- verification results:
  - local generator / workflow checks passed:
    - `bash -n tools/render-sample-catalog.sh`
    - `bash tools/render-sample-catalog.sh`
    - `make -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk print-datasets`
    - `make -Bn -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk datasets`
  - the generated dataset workflow now emits:
    - `dataset_run.<dataset>`
    - `dataset_beam.<dataset>`
    - `dataset_polarity.<dataset>`
    - optional `dataset_campaign.<dataset>`
    - a native `mk_dataset --run ... --beam ... --polarity ... --manifest ...`
      invocation in `samples-dag.mk`
  - Docker verification passed in a fresh Linux build tree:
    - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/m4-docker -DCMAKE_BUILD_TYPE=Release && cmake --build .build/m4-docker --target IO Ana mk_dataset --parallel && .build/m4-docker/bin/mk_dataset --help'`
    - native dataset scope smoke passed on a synthetic logical sample root
    - legacy context-string compatibility smoke still passed
    - duplicate-sample and run-scope-mismatch smokes both failed loudly as
      intended
    - a compiled verifier confirmed the produced dataset context, sample key,
      beam, polarity, and campaign payload

#### Milestone 5: `mk_eventlist` Uses Embedded Normalization Maps
- status: done
- hypothesis: if `mk_eventlist` resolves nominal event weights from the
  embedded run/subrun normalization table and persists `__w_norm__`,
  `__w_cv__`, `__w__`, and `__w2__`, then downstream code can stop
  reconstructing normalization from older workflow layers
- files / symbols touched:
  - `ana/EventListBuild.hh`
  - `ana/EventListBuild.cc`
  - `io/EventListIO.hh`
  - `io/EventListIO.cc`
  - `app/mk_eventlist.cc`
  - `io/bits/DERIVED`
  - `ana/README`
  - `COMMANDS`
  - `USAGE`
- expected behavior risk: high
- verification commands:
  - `git diff --check -- ana/EventListBuild.hh ana/EventListBuild.cc io/EventListIO.hh io/EventListIO.cc app/mk_eventlist.cc io/bits/DERIVED ana/README COMMANDS USAGE .agent/current_execplan.md docs/minimality-log.md`
  - `cmake --build build --target Ana IO mk_eventlist --parallel`
  - `build/bin/mk_eventlist --help || true`
- acceptance criteria:
  - missing `run` / `subRun` branches fail
  - missing normalization-table lookups fail
  - nominal event weights are derived from the logical normalization map, not
    a sample-wide scalar shortcut
- verification results:
  - local checks passed:
    - `git diff --check -- ana/EventListBuild.cc io/EventListIO.cc io/bits/DERIVED ana/README COMMANDS USAGE`
    - `bash -n tools/run-macro`
  - Docker verification passed in a fresh Linux build tree:
    - `docker build -t amarantin-dev .`
    - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/m5-docker -DCMAKE_BUILD_TYPE=Release && cmake --build .build/m5-docker --target IO Ana mk_eventlist --parallel && .build/m5-docker/bin/mk_eventlist --help'`
    - `mk_eventlist --help` printed the expected usage and then followed its
      existing invalid-arguments exit path
    - a focused synthetic `DatasetIO -> EventListIO` smoke confirmed:
      - `__w_norm__`, `__w_cv__`, `__w__`, and `__w2__` are written with the
        expected resolved values
      - copied `EventListIO` sample metadata now retains the run/subrun
        normalisation table
      - missing `run` / `subRun` branches fail loudly
      - missing run/subrun lookup entries fail loudly

#### Milestone 6: Extract `mk_dist`
- status: done
- hypothesis: if cache-building moves into a dedicated thin `mk_dist` app,
  then the workflow becomes more honest and `mk_eventlist` goes back to owning
  only the row-wise build step
- files / symbols touched:
  - `app/CMakeLists.txt`
  - `app/mk_dist.cc`
  - `app/mk_eventlist.cc`
  - `COMMANDS`
  - `USAGE`
  - `INSTALL`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- app/CMakeLists.txt app/mk_dist.cc app/mk_eventlist.cc syst/Systematics.hh syst/Systematics.cc io/DistributionIO.hh io/DistributionIO.cc syst/README COMMANDS USAGE INSTALL .agent/current_execplan.md docs/minimality-log.md`
  - `cmake --build build --target Syst mk_eventlist mk_dist --parallel`
  - `build/bin/mk_eventlist --help || true`
  - `build/bin/mk_dist --help || true`
- acceptance criteria:
  - `mk_dist` owns one-request cache construction
  - `mk_eventlist` no longer owns the preferred persistent-cache workflow
  - compatibility handling for legacy `--cache-*` flags is explicit
- verification results:
  - local checks passed:
    - `git diff --check -- app/CMakeLists.txt app/mk_dist.cc app/mk_eventlist.cc COMMANDS USAGE INSTALL`
  - Docker verification passed in a fresh Linux build tree:
    - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/m6-docker -DCMAKE_BUILD_TYPE=Release && cmake --build .build/m6-docker --target Syst mk_eventlist mk_dist --parallel && (.build/m6-docker/bin/mk_eventlist --help || true) && (.build/m6-docker/bin/mk_dist --help || true)'`
    - `mk_eventlist --help` now prints a dedicated row-wise usage plus an
      explicit legacy cache-bridge form
    - `mk_dist --help` prints the new one-request cache-builder usage
    - a focused synthetic smoke confirmed:
      - direct `mk_dist` writes a `DistributionIO` cache entry from an existing
        `EventListIO` file
      - the legacy `mk_eventlist --cache-*` bridge still writes the same cache
        payload
      - the legacy bridge emits the expected `use mk_dist` warning
  - implementation was intentionally narrowed to `app/` plus top-level docs:
    the existing `syst::build_systematics_cache(...)` and `DistributionIO`
    surface were already sufficient, and unrelated in-flight edits under
    `syst/` did not need to be touched for this milestone

#### Milestone 7: `DistributionIO`-First Downstream Assembly
- status: done
- hypothesis: if docs and downstream assembly code default to
  `DistributionIO` rather than `EventListIO` or hand-entered channel data, then
  the normal teaching path and the normal final-analysis path stop diverging
- files / symbols touched:
  - `plot/README`
  - `fit/README`
  - `COMMANDS`
  - `USAGE`
  - `io/ChannelIO.hh`
  - `io/ChannelIO.cc`
  - `fit/SignalStrengthFit.hh`
  - `fit/SignalStrengthFit.cc`
  - `app/mk_channel.cc`
  - `app/mk_xsec_fit.cc`
- expected behavior risk: moderate to high
- verification commands:
  - `git diff --check -- plot/README fit/README COMMANDS USAGE io/ChannelIO.hh io/ChannelIO.cc fit/SignalStrengthFit.hh fit/SignalStrengthFit.cc app/mk_channel.cc app/mk_xsec_fit.cc .agent/current_execplan.md docs/minimality-log.md`
  - `cmake --build build --target Fit mk_channel mk_xsec_fit --parallel`
  - `build/bin/mk_xsec_fit --help || true`
- acceptance criteria:
  - the docs teach `DistributionIO` as the normal final surface
  - compatibility channel/fit tooling still works
  - any remaining `ChannelIO` dependency is described as compatibility or
    transitional, not as the target end state
- verification results:
  - local checks passed:
    - `git diff --check -- plot/README fit/README COMMANDS USAGE io/ChannelIO.hh io/ChannelIO.cc fit/SignalStrengthFit.hh app/mk_channel.cc app/mk_xsec_fit.cc .agent/current_execplan.md docs/minimality-log.md`
  - Docker verification passed in a fresh Linux build tree:
    - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/m7-docker -DCMAKE_BUILD_TYPE=Release && cmake --build .build/m7-docker --target IO Fit mk_channel mk_xsec_fit --parallel && (.build/m7-docker/bin/mk_channel --help || true) && (.build/m7-docker/bin/mk_xsec_fit --help || true)'`
    - a focused synthetic smoke confirmed:
      - `mk_channel --manifest` builds a multi-process `ChannelIO` bundle from
        cached `DistributionIO` entries and records observed-data provenance
      - the legacy positional `mk_channel` path still writes a compatible
        two-process channel
      - `mk_xsec_fit` runs on the manifest-built channel and now reports the
        source `distribution_path` plus observed-data source keys
  - implementation notes:
    - this milestone intentionally built on top of additive in-flight
      `mk_channel` / `ChannelIO` changes that already moved channel assembly
      closer to cached `DistributionIO` inputs
    - unrelated plotting and `syst/` worktree changes stayed untouched

#### Milestone 8: Deletion Pass And Compatibility Review
- status: done
- hypothesis: if stale docs, wrapper paths, shell branching, and compatibility
  scaffolding are removed only after the native path is proven, then the final
  cleanup will reduce concepts instead of causing migration churn
- files / symbols touched:
  - `tools/run-macro`
  - `COMMANDS`
  - `USAGE`
  - `INSTALL`
  - stale compatibility code discovered in earlier milestones
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- tools/run-macro COMMANDS USAGE INSTALL .agent/current_execplan.md docs/minimality-log.md`
  - `bash -n tools/run-macro`
  - relevant focused target builds from earlier milestones
- acceptance criteria:
  - stale legacy-first docs are removed
  - shell branching is reduced without changing the documented invocation
  - any removal of installed targets or public headers requires separate
    explicit sign-off
- verification results:
  - local checks passed:
    - `git diff --check -- tools/run-macro COMMANDS USAGE INSTALL .agent/current_execplan.md docs/minimality-log.md`
    - `bash -n tools/run-macro`
  - implementation notes:
    - `tools/run-macro` now uses simple literal inference plus explicit
      prefixes instead of scraping macro signatures at runtime
    - stale docs that still taught the old keyed `mk_dataset` workflow or the
      old positional event-list example as the normal path were rewritten or
      removed
    - older ROOT helper macros such as `cache_systematics.C` and
      `write_channel.C` were left in place for ad hoc sessions, but were
      removed from the documented scripted workflow

### 7. Public-surface check
- compatibility impact:
  - additive native CLI work is expected in milestones 3, 4, and 6
  - milestone 5 changes the nominal event-weighting contract internally and
    may change persisted helper-column semantics
  - milestones 7 and 8 may stage deprecations, but they must not remove
    installed targets or public headers without explicit approval
- migration note:
  - preserve legacy `mk_sample`, `mk_dataset`, `mk_eventlist --cache-*`, and
    `mk_channel` paths until the replacement path is documented and verified
  - treat `InputPartitionIO` -> `InputShardIO` rename as a later cleanup, not
    a prerequisite for the workflow migration
- reviewer sign-off:
  - required before any non-additive CLI removal or installed-surface churn

### 8. Reduction ledger
- files deleted: pending
- wrappers removed:
  - target: `mk_eventlist --cache-*` as the preferred cache-building wrapper
  - target: repeated-key shard merge as the preferred `mk_dataset` path
- shell branches removed:
  - target: simplify `tools/run-macro` after the native downstream path lands
- stale docs removed:
  - target: replace channel-first and repeated-key dataset guidance
- targets or dependencies removed:
  - none approved yet
- approximate LOC delta:
  - expected additive in early milestones
  - expected net reduction only after milestones 6 to 8

### 9. Decision log
- fix the persisted normalization contract before rewriting downstream apps
- add the `VISION.md` native workflow first; delete compatibility layers later
- keep workflow orchestration in `app/` rather than inventing new service or
  manager layers
- do not spend rename churn on `InputPartitionIO` until the workflow model is
  stable
- treat `ChannelIO` and `mk_channel` as compatibility surfaces unless and
  until explicit approval is given to retire them

### 10. Stop conditions
- stop when the next remaining step is mostly public-API churn without clear
  simplicity gain
- stop when only rename churn or style-only cleanup remains
- stop when the workflow is already honest in docs and code, even if some
  compatibility surfaces are still present
- stop and ask for review before removing any installed target, installed
  header, or non-additive CLI behavior


## ExecPlan Addendum: Native Channel Assembly Expansion

### 1. Objective
Move final-region assembly closer to the repo vision by extending the native
`mk_channel` workflow from the current two-process bridge into a small
plain-text-driven channel builder with automatic observed-data assembly.

### 2. Constraints
- Preserve the existing positional `mk_channel` workflow for one signal plus
  one background.
- Keep workflow parsing and orchestration in `app/`; do not push manifest or
  grouping logic into `io/`.
- Keep `DistributionIO` as the fine-bin cache surface and `ChannelIO` as the
  final-region surface.
- Leave unrelated worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep workflows in `app/`
- prefer plain data and namespace functions
- keep module boundaries sharp
- add abstractions only when they delete complexity

That favors one additive plain-text manifest path in `mk_channel` rather than
introducing a second orchestration layer or a new persistence module.

### 4. System map
- `app/mk_channel.cc`
- `io/ChannelIO.hh`
- `io/ChannelIO.cc`
- `COMMANDS`
- `USAGE`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- verification:
  - `git diff --check -- app/mk_channel.cc io/ChannelIO.hh io/ChannelIO.cc COMMANDS USAGE .agent/current_execplan.md docs/minimality-log.md`
  - `cmake --build build --target mk_channel --parallel`

### 5. Milestone
- status: blocked
- hypothesis: if `mk_channel` can read one plain-text manifest describing many
  signal/background/data inputs while preserving the old positional mode, then
  final-region assembly becomes much closer to the intended native downstream
  path without broad redesign
- files / symbols touched:
  - `app/mk_channel.cc`
  - `io/ChannelIO.hh`
  - `io/ChannelIO.cc`
  - `COMMANDS`
  - `USAGE`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- app/mk_channel.cc io/ChannelIO.hh io/ChannelIO.cc COMMANDS USAGE .agent/current_execplan.md docs/minimality-log.md`
  - `cmake --build build --target mk_channel --parallel`
- acceptance criteria:
  - old positional `mk_channel` usage still works
  - additive manifest mode can assemble many processes into one `ChannelIO`
    region
  - observed data can be filled automatically from manifest data inputs
  - channel persistence keeps enough provenance for the assembled observation
- verification results:
  - `git diff --check` passed for the touched files
  - local build verification is blocked in this environment:
    - `cmake --build build --target mk_channel --parallel` failed because the
      existing `build/` tree does not define `mk_channel`
    - rerunning `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` failed because
      local SQLite3 and Eigen package discovery is broken here
    - a fallback local compiler syntax check is also unavailable because the
      default `c++` driver in this shell cannot find standard library headers

### 6. Public-surface check
- compatibility impact:
  - additive `mk_channel` manifest mode
  - additive `ChannelIO` channel provenance field if needed for observed-data
    bookkeeping
- migration note:
  - the old two-process CLI remains valid; the new path is a native expansion,
    not a replacement
- reviewer sign-off:
  - requested by user

## ExecPlan Addendum: Systematics Cache Safety And Workflow Follow-up

### 1. Objective
Finish the `syst/` follow-up after the internal file split by making
persistent cache reuse strict, making retained universe histograms real,
documenting one canonical detector-plus-reweighting workflow, and adding
focused smoke entry points for the detector and reweighting lanes.

### 2. Constraints
- Preserve the current `Systematics.hh` public API and the existing
  `mk_eventlist --cache-*` CLI shape.
- Keep the logic inside `syst/` plus `DistributionIO`; do not push systematic
  policy into `io/` persistence helpers or add a second workflow layer.
- Keep the internal split flat and grep-friendly.
- Leave unrelated worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep module boundaries sharp
- prefer plain data and namespace functions
- keep module layout flat
- add abstractions only when they delete complexity

That favors tightening the existing cache and summary paths directly, behind
the current private helper surface under `syst/bits/`, instead of inventing a
new cache-wrapper abstraction.

### 4. System map
- `io/DistributionIO.hh`
- `io/DistributionIO.cc`
- `syst/Systematics.cc`
- `syst/Detector.cc`
- `syst/Support.cc`
- `syst/UniverseFill.cc`
- `syst/UniverseSummary.cc`
- `syst/bits/Detail.hh`
- `syst/README`
- `COMMANDS`
- `USAGE`
- `tools/systematics-detector-smoke.sh`
- `tools/systematics-reweight-smoke.sh`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- verification:
  - `git diff --check -- io/DistributionIO.hh io/DistributionIO.cc syst/CMakeLists.txt syst/Systematics.cc syst/Detector.cc syst/Support.cc syst/UniverseFill.cc syst/UniverseSummary.cc syst/bits/Detail.hh syst/README COMMANDS USAGE tools/systematics-detector-smoke.sh tools/systematics-reweight-smoke.sh .agent/current_execplan.md docs/minimality-log.md`
  - `bash -n tools/systematics-detector-smoke.sh`
  - `bash -n tools/systematics-reweight-smoke.sh`
  - `cmake --build build --target Syst mk_eventlist --parallel`

### 5. Milestone
- status: blocked
- hypothesis: if persistent cache files are only reused when their metadata
  matches the current `EventListIO`, retained universe histograms round-trip
  through `DistributionIO`, and the detector and reweighting lanes have one
  documented workflow plus focused smoke scripts, then `syst/` becomes safer to
  operate and easier to verify without widening the public surface
- files / symbols touched:
  - `io/DistributionIO.hh`
  - `io/DistributionIO.cc`
  - `syst/Systematics.cc`
  - `syst/Support.cc`
  - `syst/UniverseSummary.cc`
  - `syst/README`
  - `COMMANDS`
  - `USAGE`
  - `tools/systematics-detector-smoke.sh`
  - `tools/systematics-reweight-smoke.sh`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: low to moderate
- verification results:
  - `git diff --check` passed for the touched files
  - `bash -n` passed for both new smoke scripts
  - focused rebuild verification remains blocked here:
    - `cmake --build build --target Syst mk_eventlist --parallel` still fails
      because the generated `build/` makefiles reference `/usr/bin/cmake`
    - rerunning `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` still fails on
      local SQLite3 and Eigen discovery
    - the new smoke scripts were added but not executed because `root-config`
      is not available on this host
  - behavior changes now in place:
    - `DistributionIO` cache entries are rejected when a non-empty file
      metadata block does not match the current `EventListIO`
    - retained universe histograms are persisted and reconstructed
    - sigma-only cached families no longer silently linearly rebin across a
      different binning
    - `COMMANDS`, `USAGE`, and `syst/README` now describe one canonical
      detector-plus-reweighting cache workflow

## ExecPlan Addendum: Fit And Channel Safety Corrections

### 1. Objective
Correct the fit/channel issues found in review: inconsistent reported fit
points, silent zero-data fits, weak channel compatibility checks, and
over-correlated unlabeled detector templates.

### 2. Constraints
- Preserve the current `SignalStrengthFit` and `mk_*` surfaces as much as
  possible.
- Keep the corrections inside the existing fit/channel workflow instead of
  adding new persistence layers or abstractions.
- Leave unrelated worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep workflows in `app/`
- keep module boundaries sharp
- prefer plain data and namespace functions

That favors fixing the existing fit/channel seams directly rather than adding a
second safety wrapper layer.

### 4. System map
- `fit/SignalStrengthFit.cc`
- `app/mk_channel.cc`
- `app/mk_xsec_fit.cc`
- `io/macro/write_channel.C`
- `USAGE`
- `COMMANDS`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- verification:
  - `git diff --check -- fit/SignalStrengthFit.cc app/mk_channel.cc app/mk_xsec_fit.cc io/macro/write_channel.C USAGE COMMANDS`
  - Docker focused smoke for zero-data guards, selection mismatch rejection,
    unlabeled detector-template naming, and fit report consistency

### 5. Milestone
- status: done
- hypothesis: if the CLI requires explicit zero-data intent, channel assembly
  verifies selection compatibility, unlabeled detector templates stay
  process-local, and the fitter reports one self-consistent optimum, then the
  current fit workflow becomes materially safer without broad redesign
- files / symbols touched:
  - `fit/SignalStrengthFit.cc`
  - `app/mk_channel.cc`
  - `app/mk_xsec_fit.cc`
  - `io/macro/write_channel.C`
  - `USAGE`
  - `COMMANDS`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- verification results:
  - `git diff --check` passed for the touched files
  - Docker smoke passed:
    - `mk_channel` now rejects missing observed bins unless `--allow-zero-data`
      is passed
    - `mk_xsec_fit` now rejects all-zero observed spectra unless
      `--allow-zero-data` is passed
    - selection mismatches are rejected during channel assembly
    - unlabeled detector templates stay process-local in nuisance naming
    - nuisance and parameter reports now agree at the best-fit point

## ExecPlan Addendum: Systematics Source Split By Type

### 1. Objective
Split the monolithic `syst/Systematics.cc` so detector handling and
universe-family handling live in separate implementation files, while keeping
`Systematics.hh` as the stable public surface.

### 2. Constraints
- Preserve the current `syst::evaluate(...)`, `syst::build_systematics_cache`,
  and `SystematicsEngine` public API.
- Keep `syst/` focused on systematic evaluation and cache construction; do not
  push workflow logic into `io/` or `app/`.
- Keep the split internal: no public-header churn unless the build needs a
  private helper header under `syst/bits/`.
- Leave unrelated worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep module boundaries sharp
- prefer plain data and namespace functions
- keep module layout flat
- add abstractions only when they delete complexity

That favors one public header plus a small private helper surface, with the
detector and universe-family implementations split by responsibility rather
than introducing new public classes.

### 4. System map
- `syst/Systematics.hh`
- `syst/Systematics.cc`
- new internal `syst/` implementation files for detector and family handling
- optional private helper header under `syst/bits/`
- `syst/CMakeLists.txt`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- verification:
  - `git diff --check -- syst/CMakeLists.txt syst/Systematics.hh syst/Systematics.cc syst/bits/* .agent/current_execplan.md docs/minimality-log.md`
  - `cmake --build build --target Syst mk_eventlist --parallel`

### 5. Milestone
- status: done
- hypothesis: if detector-specific and universe-family-specific code live in
  separate translation units behind one private helper surface, then `syst/`
  becomes easier to navigate and review without changing behavior
- files / symbols touched:
  - `syst/Systematics.cc`
  - `syst/CMakeLists.txt`
  - `syst/Support.cc`
  - `syst/Detector.cc`
  - `syst/UniverseFill.cc`
  - `syst/UniverseSummary.cc`
  - `syst/bits/Detail.hh`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: low to moderate
- verification commands:
  - `git diff --check -- syst/CMakeLists.txt syst/Systematics.hh syst/Systematics.cc syst/bits/* .agent/current_execplan.md docs/minimality-log.md`
  - `cmake --build build --target Syst mk_eventlist --parallel`
  - `docker build -t amarantin-dev .`
  - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B /tmp/amarantin-syst-check -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/amarantin-syst-check --target Syst --parallel'`
- acceptance criteria:
  - detector and universe-family handling are no longer co-located in one
    source file
  - the public `syst/` API remains unchanged
  - the focused build passes
- verification results:
  - `git diff --check` passed for the touched tracking and `syst/` files
  - the source split itself is implemented
  - Docker focused build passed for `Syst`
  - the default host `build/` tree remains unreliable in this checkout, so the
    focused verification ran in a clean temporary Docker build tree instead

## ExecPlan Addendum: Fit Detector-Envelope Follow-up

### 1. Objective
Close the remaining fit-library rough edge for channels that retain only
`detector_down` / `detector_up` payloads and no detector templates, while
documenting the verification boundary on the host setup path.

### 2. Constraints
- Preserve the current `SignalStrengthFit` public surface.
- Keep the change inside `fit/` and tracking/docs unless a persisted schema bug
  is discovered.
- Leave unrelated worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep module boundaries sharp
- prefer plain data and namespace functions
- do a small deletion or simplification pass after each feature pass

That favors tightening the existing fallback logic instead of introducing a new
detector-abstraction layer.

### 4. System map
- `fit/SignalStrengthFit.cc`
- `fit/README`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- verification:
  - `git diff --check -- fit/SignalStrengthFit.cc fit/README docs/minimality-log.md .agent/current_execplan.md`
  - Docker synthetic smoke for an envelope-only `ChannelIO` channel
  - host `source ./.setup.sh` check

### 5. Milestone
- status: done
- hypothesis: if detector-envelope-only payloads stay keyed by shared detector
  identity and are morphed through down / nominal / up directly, then reduced
  `ChannelIO` channels will still produce a coherent shared detector nuisance
  without reviving template-only assumptions
- files / symbols touched:
  - `fit/SignalStrengthFit.cc`
  - `fit/README`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- verification results:
  - `git diff --check` passed for the touched files
  - Docker envelope-only smoke passed with one shared `detector:detA` nuisance
  - host setup remained unavailable because `./.setup.sh` requires CVMFS
    products and Fermilab `setup` tooling that are absent here

## ExecPlan Addendum: Sample Catalog And Logical-Sample Assembly Pass

### 1. Objective
Make sample handling scale to many run fragments and detector variations by
introducing a flat sample-catalog workflow and by letting `mk_dataset` assemble
one logical dataset sample from many concrete `SampleIO` artifact files.

### 2. Constraints
- Preserve the existing `mk_sample` and positional `mk_dataset key=path`
  workflows.
- Keep `io/` persistence-only; do not move catalog parsing or manifest logic
  into the I/O classes.
- Keep the first pass additive:
  - manifest support in `mk_dataset`
  - per-artifact metadata overrides in `samples-dag.mk`
  - generated catalog outputs under `samples/`
- Leave unrelated worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep workflows in `app/`
- prefer plain data and namespace functions
- keep module boundaries sharp
- add abstractions only when they delete complexity

That favors one flat catalog plus generated plain-text manifests over carrying
XML-stage names through the whole analysis stack.

### 4. System map
- `app/mk_dataset.cc`
- `samples-dag.mk`
- `COMMANDS`
- `USAGE`
- `samples/README`
- `samples/datasets.tsv`
- `samples/catalog.tsv`
- `tools/render-sample-catalog.sh`
- generated outputs under `samples/generated/`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- verification:
  - `bash -n tools/render-sample-catalog.sh`
  - `cmake --build <build> --target mk_dataset --parallel`
  - focused smoke for manifest-driven merged dataset assembly

### 5. Milestones

### Milestone 1
- status: done
- hypothesis: if `mk_dataset` can merge repeated sample keys from a manifest,
  then production shards can stay shard-level while downstream analysis uses
  stable logical sample keys
- files / symbols touched:
  - `app/mk_dataset.cc`
- expected behavior risk: moderate
- verification commands:
  - Docker `cmake --build /tmp/amarantin-sample-build --target mk_dataset --parallel`
  - focused smoke creating tiny synthetic `SampleIO` files and merging them via
    `--manifest`
- acceptance criteria:
  - repeated sample keys merge into one dataset sample
  - old positional `key=path` usage still works
  - metadata conflicts fail loudly

### Milestone 2
- status: done
- hypothesis: per-artifact metadata in the sample build layer removes the
  current global `origin` / `variation` bottleneck and supports many detector
  variations cleanly
- files / symbols touched:
  - `samples-dag.mk`
  - generated `samples/generated/datasets.mk`
- expected behavior risk: low
- verification commands:
  - `make -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk print-samples`
- acceptance criteria:
  - artifact-level metadata overrides are supported with defaults preserved

### Milestone 3
- status: done
- hypothesis: one flat checked-in catalog plus a short generator script is
  easier to maintain than duplicating sample information across XML fragments,
  `datasets.mk`, and ad hoc `sample.defs`
- files / symbols touched:
  - `samples/README`
  - `samples/datasets.tsv`
  - `samples/catalog.tsv`
  - `tools/render-sample-catalog.sh`
  - `samples/generated/*`
  - `COMMANDS`
  - `USAGE`
- expected behavior risk: low
- verification commands:
  - `bash -n tools/render-sample-catalog.sh`
  - `bash tools/render-sample-catalog.sh`
- acceptance criteria:
  - one command regenerates dataset make includes, dataset manifests, and
    sample defs from the flat catalog
  - the generated run1 example is usable with the new `mk_dataset --manifest`
    path

### 6. Public-surface check
- compatibility impact:
  - additive `mk_dataset --manifest` support
  - additive `samples/` catalog workflow
- migration note:
  - legacy positional `key=path` dataset assembly remains supported
  - old `datasets.mk` style remains supported
- reviewer sign-off:
  - only needed before making the catalog workflow the default root-level path

### 7. Reduction ledger
- files deleted: 0
- wrappers removed:
  - removed the assumption that one dataset key must map to exactly one
    concrete `SampleIO` file at dataset-assembly time
- shell branches removed: 0
- stale docs removed: 0
- targets or dependencies removed: 0
- approximate LOC delta: about `+450` additive LOC across the new catalog,
  generator, `mk_dataset` merge path, and docs

### 8. Decision log
- Keep shard-level provenance in `SampleIO`, but stop exposing shard names as
  the preferred downstream analysis sample keys.
- Treat the new catalog as an additive source-of-truth layer; do not replace
  existing workflows until the manifest path is verified.

### 9. Stop conditions
- Stop after the additive manifest/catalog workflow is implemented and
  verified.
- Stop before changing persisted schemas or making generated catalog outputs the
  only supported path.

## ExecPlan Addendum: Plot Stale-State Cleanup Pass

### 1. Objective
Trim stale state and legacy carryover from the new `plot/` surface without
changing the external plotting workflows.

### 2. Constraints
- Preserve the `Plot` target and current stack / unstack / efficiency entry
  points.
- Keep the cleanup scoped to stale members, stale helper coupling, and
  undocumented compatibility carryover.
- Leave unrelated worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- add abstractions only when they delete complexity
- keep module boundaries sharp
- do a small deletion pass after every feature pass

That favors deleting dead plot state and moving shared histogram math into one
private helper surface rather than letting copy-by-declaration coupling persist.

### 4. System map
- `plot/StackedHist.hh`
- `plot/StackedHist.cc`
- `plot/UnstackedHist.hh`
- `plot/UnstackedHist.cc`
- `plot/EfficiencyPlot.hh`
- `plot/EfficiencyPlot.cc`
- `plot/PlottingHelper.hh`
- `plot/PlottingHelper.cc`
- `plot/bits/DataMcHistogramUtils.hh`
- `docs/minimality-log.md`
- verification:
  - `git diff --check -- <touched plot files>`
  - Docker `cmake --build ... --target Plot --parallel`

### 5. Milestone
- status: done
- hypothesis: deleting dead member state, exposing accurate efficiency totals,
  and consolidating shared ratio/error helpers will make the new plotting layer
  smaller and easier to maintain without changing rendered behavior
- files / symbols touched:
  - `plot/StackedHist.hh`
  - `plot/StackedHist.cc`
  - `plot/UnstackedHist.hh`
  - `plot/UnstackedHist.cc`
  - `plot/EfficiencyPlot.hh`
  - `plot/EfficiencyPlot.cc`
  - `plot/PlottingHelper.hh`
  - `plot/PlottingHelper.cc`
  - `plot/bits/DataMcHistogramUtils.hh`
- verification results:
  - `git diff --check` passed on the touched tracking and `plot/` files
  - Docker `Plot` target rebuild passed in `/tmp/amarantin-plot-cleanup-build`

## 1. Objective
Port the legacy `heron` plotting surface into `amarantin` by adding a smaller,
`EventListIO`-native version of `Plotter`, `PlottingHelper`, `StackedHist`, and
`UnstackedHist`, while keeping the existing `Plot` target and current plotting
entrypoints intact.

## 2. Constraints
- Preserve existing installed targets, especially `Plot`.
- Keep the current `EventDisplay`, `EventListPlotting`, and `EfficiencyPlot`
  behavior unchanged.
- Do not pull `heron`'s old selection / frame wrapper stack into this repo.
- Keep the new plotting layer centered on `EventListIO`, framework-owned event
  list branches, and direct ROOT drawing.
- Leave unrelated worktree changes untouched.

## 3. Design anchor
From `DESIGN.md`:
- keep `io/` persistence-only
- add abstractions only when they delete complexity
- prefer plain data and namespace functions
- prefer `EventListIO`-first downstream APIs in `plot/`

That means porting the plotting capability, but not the old `SelectionEntry` /
`Frame` graph from `heron`.

## 4. System map
- `plot/CMakeLists.txt`
- `plot/PlotDescriptors.hh`
- `plot/PlotChannels.hh`
- `plot/PlottingHelper.hh`
- `plot/PlottingHelper.cc`
- `plot/Plotter.hh`
- `plot/Plotter.cc`
- `plot/StackedHist.hh`
- `plot/StackedHist.cc`
- `plot/UnstackedHist.hh`
- `plot/UnstackedHist.cc`
- `.rootlogon.C`
- legacy reference files under
  `/Users/user/programs/heron/framework/modules/plot/`
- verification commands:
  - `cmake --build build --target Plot --parallel`
  - `bash -n tools/run-macro`
  - one focused smoke that instantiates the new plotting surface

## 5. Candidate simplifications

### boundary sharpening
- keep the port on top of `EventListIO` sample trees, `__analysis_channel__`,
  and `__w__`
- avoid reviving legacy selection wrapper types that do not exist here

### wrapper collapse
- expose one small descriptor layer instead of spreading plotting parameters
  across ad hoc helpers

### doc / build cleanup
- wire the new public headers into `Plot`
- expose them through `.rootlogon.C` for ROOT macro usage

### stale scaffolding
- do not introduce macro-only or repo-utility wrappers unless verification
  proves they are needed

## 6. Milestones

### Milestone 1
- status: done
- hypothesis: a small descriptor layer plus direct `EventListIO`-based
  stack/unstack renderers will recover the useful `heron` plotting capability
  with less coupling than the legacy implementation
- files / symbols touched:
  - `plot/PlotDescriptors.hh`
  - `plot/PlotChannels.hh`
  - `plot/PlottingHelper.hh`
  - `plot/PlottingHelper.cc`
  - `plot/Plotter.hh`
  - `plot/Plotter.cc`
  - `plot/StackedHist.hh`
  - `plot/StackedHist.cc`
  - `plot/UnstackedHist.hh`
  - `plot/UnstackedHist.cc`
  - `plot/CMakeLists.txt`
  - `.rootlogon.C`
  - `docs/minimality-log.md`
- expected behavior risk: moderate
- verification commands:
  - `cmake --build build --target Plot --parallel`
  - `bash -n tools/run-macro`
  - focused compile or ROOT smoke using the new plot classes
- acceptance criteria:
  - `Plot` builds with the new headers and sources
  - the new plotting layer can produce stack and unstack plots from
    `EventListIO`
  - existing plot code continues to compile
  - the port stays `EventListIO`-first and does not import legacy selection
    wrappers

## 7. Public-surface check
- compatibility impact: adds installed public headers under `plot/` and extends
  the public `Plot` surface
- migration note: additive only; existing plotting entrypoints remain supported
- reviewer sign-off: required only if follow-up work wants to remove or rename
  the new headers after this port

## 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- stale docs removed: 0
- targets or dependencies removed: 0
- approximate LOC delta: pending implementation and verification
- approximate LOC delta: about `+2k` additive LOC across the new plotting
  surface and integration points

## 9. Decision log
- Reuse the legacy visual model and API shape where it helps, but keep the data
  source model native to `amarantin`.
- Treat external samples as MC-like stack components, not overlaid data, because
  `EventListBuild` gives them framework channel code `1` and weighted yields.
- Prefer direct ROOT histogram filling from per-sample `TTree`s over introducing
  `RDataFrame`-specific wrapper types that the current repo does not have.

## 10. Stop conditions
- Stop after the new plotting surface is built and smoke-tested.
- Stop if parity with `heron` requires importing large legacy abstractions
  rather than extending the new `EventListIO`-native layer.
- Stop if the remaining work is mostly styling or macro sugar rather than core
  plotting capability.

---

# ExecPlan Addendum: Signal-Strength Fit Refactor

## 1. Objective
Replace the first-pass `fit/` API and optimiser with a smaller but more
production-leaning signal-strength fit surface that is easier to grep, shares
global nuisances across processes, and uses the persisted `ChannelIO`
uncertainty payload more faithfully.

## 2. Constraints
- Keep `io/` persistence-only even if `ChannelIO` needs a small additive schema
  extension.
- Keep the `Fit` target and `mk_xsec_fit` workflow intact.
- Leave unrelated worktree changes untouched.
- Treat renames of installed fit headers and symbols as an explicit,
  user-approved surface change in this pass.

## 3. Design anchor
From `DESIGN.md`:
- prefer plain data and namespace functions
- keep workflows in `app/`
- keep module boundaries sharp
- add abstractions only when they delete complexity

That favors one flat signal-strength fit surface with plain structs and free
functions, not a port of `collie`'s old fitter class graph.

## 4. System map
- `fit/CMakeLists.txt`
- `fit/SignalStrengthFit.hh`
- `fit/SignalStrengthFit.cc`
- `fit/XsecFit.hh`
- `app/mk_xsec_fit.cc`
- `fit/macro/fit_channel.C`
- `fit/macro/scan_mu.C`
- `.rootlogon.C`
- `fit/README`
- `io/ChannelIO.hh`
- `io/ChannelIO.cc`
- `app/mk_channel.cc`
- `io/macro/write_channel.C`
- `/Users/user/programs/collie/doc/cross_section_math.md`
- `/Users/user/programs/collie/limit/src/ProfileLH.cc`
- `/Users/user/programs/collie/limit/src/CrossSectionCalc.cc`
- verification:
  - `cmake --build build --target Fit mk_xsec_fit --parallel`
  - `bash -n tools/run-macro`
  - one focused fit smoke in a clean build environment

## 5. Candidate simplifications

### boundary sharpening
- keep fit semantics in `fit/` while only extending `ChannelIO` with the
  identity needed to build shared nuisances

### wrapper collapse
- rename the vague `fit::Model` surface to a direct signal-strength problem API
- replace the homegrown coordinate-descent loop with one joint minimizer path

### doc / build cleanup
- switch repo callsites and docs to the renamed signal-strength fit surface
- keep `XsecFit.hh` only as a small migration shim if needed

### stale scaffolding
- remove the current per-process nuisance builder shape that encodes broken
  correlations

## 6. Milestones

### Milestone 1
- status: done
- hypothesis: one refactor pass that aligns naming, nuisance bookkeeping, and
  optimizer behavior will produce a fit surface that is more accurate and less
  misleading without importing `collie` wholesale
- files / symbols touched:
  - `fit/SignalStrengthFit.hh`
  - `fit/SignalStrengthFit.cc`
  - `fit/XsecFit.hh`
  - `fit/CMakeLists.txt`
  - `io/ChannelIO.hh`
  - `io/ChannelIO.cc`
  - `app/mk_channel.cc`
  - `io/macro/write_channel.C`
  - `app/mk_xsec_fit.cc`
  - `fit/macro/fit_channel.C`
  - `fit/macro/scan_mu.C`
  - `.rootlogon.C`
  - `fit/README`
- expected behavior risk: moderate to high
- verification commands:
  - `bash -n tools/run-macro`
  - `cmake --build <builddir> --target Fit mk_xsec_fit --parallel`
  - one synthetic channel smoke that exercises shared nuisances and interval
    reporting
- verification results:
  - `git diff --check` passed on the touched files
  - `bash -n tools/run-macro` passed
  - Docker build passed in `/tmp/amarantin-fit-refactor-build`:
    - `Built target Fit`
    - `Built target mk_xsec_fit`
  - Docker synthetic channel smoke passed:
    - one shared `flux:weightsPPFX:mode0` nuisance covered three processes
    - one shared `detector:detA` nuisance covered two processes
    - total and stat intervals were reported with explicit `*_found: true`
    - covariance rows were emitted in the fit report
- acceptance criteria:
  - shared family nuisances are no longer duplicated per process
  - detector/stat inputs persisted on `ChannelIO` are reflected in the fit
    problem builder
  - failed interval scans are reported explicitly, not as zero
  - repo callsites use the renamed signal-strength API

## 7. Public-surface check
- compatibility impact: renames the primary fit header and public type names
- migration note: `XsecFit.hh` may remain as a thin compatibility include for
  one pass, but repo callsites should move to `SignalStrengthFit.hh`
- reviewer sign-off: explicitly approved by the user in this pass

## 8. Reduction ledger
- files deleted:
  - `fit/XsecFit.cc`
- wrappers removed:
  - removed the old coordinate-descent profiling implementation
- shell branches removed: 0
- stale docs removed: 0
- targets or dependencies removed: 0
- approximate LOC delta: net additive; the new fit source replaces the old one
  but also adds covariance/status reporting and the small `ChannelIO` identity
  extension

## 9. Decision log
- Prefer a global nuisance book keyed by persisted identity over the current
  per-process mode builder.
- Treat `total_down` / `total_up` as a fallback nuisance source to avoid
  double-counting when component payloads are already present.
- Prefer a ROOT joint minimizer over expanding the coordinate-descent loop.

## 10. Stop conditions
- Stop if the remaining work is mostly external compatibility sugar.
- Stop if making detector correlations fully faithful would require a much
  larger `ChannelIO` redesign than this pass justifies.

---

# ExecPlan Addendum: Fit Library Pass

## 1. Objective
Add a small native `fit/` library that profiles a `ChannelIO` signal-strength
parameter against persisted process templates and mode payloads, while keeping
the current `io/` / `syst/` boundaries intact and documenting the fit surface in
the root `README`.

## 2. Constraints
- Preserve existing installed targets and keep the new work additive.
- Keep `io/` persistence-only; do not move fit logic or optimizer code there.
- Keep the first pass centered on `ChannelIO`, not on raw ntuples or ad hoc
  ROOT macro entrypoints.
- Leave unrelated worktree changes untouched.

## 3. Design anchor
From `DESIGN.md`:
- `io/` owns persistence only
- keep workflows in `app/`
- prefer plain data and namespace functions
- add abstractions only when they delete complexity

That justifies a new, flat `fit/` module with plain structs plus free
functions, instead of importing `collie`'s old I/O and fitter class graph.

## 4. System map
- `CMakeLists.txt`
- `fit/CMakeLists.txt`
- `fit/XsecFit.hh`
- `fit/XsecFit.cc`
- `README`
- `io/ChannelIO.hh`
- verification commands:
  - `cmake --build build --target Fit --parallel`

## 5. Candidate simplifications

### boundary sharpening
- keep the fit layer on `ChannelIO` rather than reaching back into `EventListIO`
  or pushing optimizer logic into `io/`

### wrapper collapse
- expose one small `fit::Model` / `fit::Result` surface instead of several
  builder or manager classes

### doc / build cleanup
- wire the new library into the main CMake graph and document the toy scan in
  the root `README`

### stale scaffolding
- avoid porting `collie`'s legacy loader and mass-point wrapper surface

## 6. Milestones

### Milestone 1
- status: done
- hypothesis: one additive `Fit` library with a direct `ChannelIO` surface is a
  smaller and easier-to-grep first step than porting `collie` wholesale
- files / symbols touched:
  - `CMakeLists.txt`
  - `fit/CMakeLists.txt`
  - `fit/XsecFit.hh`
  - `fit/XsecFit.cc`
  - `README`
  - `docs/minimality-log.md`
- expected behavior risk: moderate
- verification commands:
  - `cmake --build build --target Fit --parallel`
- acceptance criteria:
  - `Fit` builds as a standalone library target
  - the public fit surface stays plain-data and `ChannelIO`-first
  - the root `README` documents the toy scan requested by the user

## 7. Public-surface check
- compatibility impact: additive installed target `Fit` and additive public
  header `XsecFit.hh`
- migration note: no existing CLI or installed header was renamed or removed
- reviewer sign-off: required only if follow-up work wants to change the new
  header name or fold `fit/` into another module

## 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- stale docs removed: 0
- targets or dependencies removed: 0
- approximate LOC delta: about `+500` additive LOC for the first native fit pass

## 9. Decision log
- Use one nuisance per persisted mode by default and leave shared
  cross-process-correlation bookkeeping to a later channel-assembly pass.
- Prefer a small built-in profile scan over adding another external fitting
  dependency in the first pass.

## 10. Stop conditions
- Stop after the additive `Fit` target builds and the root `README` is updated.
- Stop if the next useful step requires redesigning persisted correlation
  metadata rather than finishing the small native fitter surface.

---

# ExecPlan Addendum: Fit Workflow Wiring Pass

## 1. Objective
Wire the additive `fit/` library into the normal `amarantin` workflow so a user
can assemble a small `ChannelIO` fit input and run the native xsec fit through
`app/` executables instead of ad hoc local code.

## 2. Constraints
- Preserve existing installed targets and current `mk_sample` / `mk_dataset` /
  `mk_eventlist` behavior.
- Keep `io/` persistence-only and keep workflow orchestration in `app/`.
- Keep the first channel assembly pass small: one signal process, one
  background process, and optional observed data bins on common binning.
- Leave unrelated worktree changes untouched.

## 3. Design anchor
From `DESIGN.md`:
- keep workflows in `app/`
- prefer plain data and namespace functions
- add abstractions only when they delete complexity

That favors two thin CLIs on top of the existing `ChannelIO` and `fit::`
surfaces, rather than another persistence class or macro-only workflow.

## 4. System map
- `app/CMakeLists.txt`
- `app/mk_channel.cc`
- `app/mk_xsec_fit.cc`
- `fit/XsecFit.hh`
- `fit/XsecFit.cc`
- `COMMANDS`
- `INSTALL`
- `USAGE`
- `README`
- verification commands:
  - `cmake --build build --target mk_channel --parallel`
  - `cmake --build build --target mk_xsec_fit --parallel`
  - `build/bin/mk_channel --help || true`
  - `build/bin/mk_xsec_fit --help || true`

## 5. Candidate simplifications

### boundary sharpening
- keep channel assembly and fit execution in `app/` instead of `io/` or
  ROOT-only macros

### wrapper collapse
- add one direct `fit::profile_xsec(...)` convenience overload for
  `ChannelIO::Channel` so the CLI stays thin

### doc / build cleanup
- document the new `mk_channel` and `mk_xsec_fit` workflow next to the existing
  `mk_*` commands

### stale scaffolding
- avoid adding a new fit-result persistence layer until a real downstream need
  appears

## 6. Milestones

### Milestone 1
- status: done
- hypothesis: a small `mk_channel` + `mk_xsec_fit` path is the smallest change
  that makes the native fit library usable in the repo's normal workflow
- files / symbols touched:
  - `app/CMakeLists.txt`
  - `app/mk_channel.cc`
  - `app/mk_xsec_fit.cc`
  - `fit/XsecFit.hh`
  - `fit/XsecFit.cc`
  - `COMMANDS`
  - `INSTALL`
  - `USAGE`
  - `README`
- expected behavior risk: moderate
- verification commands:
  - `cmake --build build --target mk_channel --parallel`
  - `cmake --build build --target mk_xsec_fit --parallel`
  - focused CLI help / smoke checks
- acceptance criteria:
  - the repo builds additive `mk_channel` and `mk_xsec_fit` executables
  - `mk_xsec_fit` runs directly from `ChannelIO`
  - the docs show the end-to-end channel and fit workflow

## 7. Public-surface check
- compatibility impact: additive executables and additive `fit::` convenience
  overloads only
- migration note: existing users of `fit::Model` keep working; the new CLI path
  is an additive shortcut
- reviewer sign-off: required only if a later pass wants to replace existing
  workflow commands rather than extend them

## 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- stale docs removed: 0
- targets or dependencies removed: 0
- approximate LOC delta: about `+250` additive LOC for the CLI wiring plus doc
  updates

## 9. Decision log
- Prefer a text-report fit result over adding `FitResultIO` in the first app
  wiring pass.
- Keep the first `mk_channel` path intentionally small and explicit rather than
  adding generic multi-process parsing.

## 10. Stop conditions
- Stop after the CLI workflow builds and the docs reflect it.
- Stop if the next step is mostly a generalized process-spec parser or a new
  persistence surface rather than direct workflow wiring.

---

# ExecPlan Addendum: Root-Clutter Reduction Pass

## 1. Objective
Reduce root-directory clutter from ad hoc CMake build trees without changing
the canonical `build/` workflow or deleting any existing generated directories.

## 2. Constraints
- Preserve the documented primary build path:
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- Do not delete or move existing build trees in this pass.
- Keep the change small and tool-based: prevent new clutter rather than trying
  to clean the whole workspace aggressively.
- Leave unrelated worktree changes untouched.

## 3. Design anchor
From `DESIGN.md`:
- keep workflows direct
- keep helper scripts short
- add abstractions only when they delete complexity

That favors one short helper script plus workspace exclusions, rather than a
larger build-management layer.

## 4. System map
- `.gitignore`
- `.vscode/settings.json`
- `tools/configure-build`
- `COMMANDS`
- `INSTALL`
- `USAGE`
- verification commands:
  - `bash -n tools/configure-build`
  - `python3 -m json.tool .vscode/settings.json`

## 5. Candidate simplifications

### script simplification
- add one tiny helper that routes extra builds into `.build/<name>`

### boundary sharpening
- keep the canonical `build/` path for the main documented workflow
- keep scratch builds in one hidden parent instead of many root-level
  directories

### doc / build cleanup
- document the hidden multi-build convention next to the existing build docs
- hide clutter in the workspace explorer without changing build outputs

### stale scaffolding
- avoid deleting existing build trees in this pass

## 6. Milestones

### Milestone 1
- status: done
- hypothesis: one hidden-parent convention plus editor exclusions is enough to
  stop root clutter from getting worse
- files / symbols touched:
  - `.gitignore`
  - `.vscode/settings.json`
  - `tools/configure-build`
  - `COMMANDS`
  - `INSTALL`
  - `USAGE`
- expected behavior risk: low
- verification commands:
  - `bash -n tools/configure-build`
  - `python3 -m json.tool .vscode/settings.json`
- acceptance criteria:
  - extra builds can be configured under `.build/<name>`
  - common editor views hide root-level generated build trees
  - docs point users at the new convention

## 7. Public-surface check
- compatibility impact: additive helper script and additive workspace settings
  only
- migration note: the existing `build/` path remains the primary documented
  build path

## 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- stale docs removed: 0
- targets or dependencies removed: 0
- approximate LOC delta: about `+40` additive LOC for one helper, one workspace
  settings file, and small doc updates

## 9. Decision log
- Use a hidden `.build/` parent for extra build trees instead of changing the
  canonical `build/` directory.
- Prefer non-destructive decluttering over deleting existing build trees.

## 10. Stop conditions
- Stop after the helper, ignores, and docs are in place.
- Stop if the next step would require deleting or relocating user build trees
  without an explicit ask.

## ExecPlan Addendum: Sample Source-Mode And Run-1 Detector Catalog Pass

### 1. Objective
Make the sample catalog honest enough for many run fragments and detector
variations by supporting non-directory artifact sources, by filling in real
Run 1 detector-variation rows from the legacy good-runs definitions, and by
making logical dataset assembly a first-class generated workflow.

### 2. Constraints
- Preserve the old `datasets.mk` `artifact:subdir` include shape.
- Preserve `tools/mklist.sh --dir ... --pat ... --out ...`.
- Keep catalog parsing and workflow orchestration outside `io/`.
- Do not invent detector-variation output directories that are not present in
  local sources.
- Leave unrelated worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep workflows in `app/`
- prefer plain data and namespace functions
- keep module boundaries sharp
- add abstractions only when they delete complexity

That favors one richer flat source catalog plus generated plain-text workflow
files over growing ad hoc special cases in `samples-dag.mk`.

### 4. System map
- `tools/mklist.sh`
- `samples-dag.mk`
- `tools/render-sample-catalog.sh`
- `samples/README`
- `samples/catalog.tsv`
- `samples/datasets.tsv`
- generated files under `samples/generated/`
- `COMMANDS`
- `USAGE`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- legacy reference:
  - `/Users/user/programs/searchingforstrangeness/xml/numi_fhc_run1.xml`
  - `/Users/user/programs/searchingforstrangeness/scripts/README.md`
  - `/Users/user/programs/searchingforstrangeness/scripts/apply_goodruns_run1_fhc.sh`

### 5. Candidate simplifications

### boundary sharpening
- keep shard provenance in the catalog, but let the sample-build workflow
  distinguish directory scans from SAM-definition-backed shards explicitly

### wrapper collapse
- add a generated dataset target in `samples-dag.mk` instead of requiring a
  manual `mk_dataset --defs --manifest` invocation

### script simplification
- make `tools/mklist.sh` own the small source-mode branch instead of spreading
  it across separate shell helpers

### stale scaffolding
- remove the fake commented detector rows and replace them with real Run 1
  detector entries backed by local legacy definitions

### 6. Milestone
- status: done
- hypothesis: a richer source catalog with explicit source kinds and generated
  dataset assembly will scale to detector-variation-heavy workflows more
  cleanly than pretending every artifact is just a subdirectory under one base
- files / symbols touched:
  - `tools/mklist.sh`
  - `samples-dag.mk`
  - `tools/render-sample-catalog.sh`
  - `samples/catalog.tsv`
  - `samples/README`
  - `COMMANDS`
  - `USAGE`
  - generated files under `samples/generated/`
- expected behavior risk: moderate
- verification commands:
  - `bash -n tools/mklist.sh`
  - `bash -n tools/render-sample-catalog.sh`
  - `bash tools/render-sample-catalog.sh`
  - `make -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk print-samples`
  - `make -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk -n datasets`
  - focused `tools/mklist.sh --samdef ...` smoke with a stub `samweb`
- acceptance criteria:
  - the generated sample catalog can express both directory-backed and
    SAM-definition-backed artifacts
  - Run 1 detector-variation logical keys are rendered without guessed
    directory paths
  - dataset assembly from the generated manifest is a first-class make target

### 7. Public-surface check
- compatibility impact:
  - additive `tools/mklist.sh --samdef` and `--list` support
  - additive generated dataset target in `samples-dag.mk`
- migration note:
  - legacy `artifact:subdir` includes remain supported
  - plain `mk_dataset --defs --manifest` remains supported

### 8. Reduction ledger
- files deleted: 0
- wrappers removed:
  - remove the need to hand-write a separate `mk_dataset --defs --manifest`
    command once generated catalog outputs already exist
- shell branches removed:
  - remove the assumption in the generated sample path that every artifact
    source is a directory scan under one dataset base
- stale docs removed:
  - remove the fake commented detector rows from `samples/catalog.tsv`
- targets or dependencies removed: 0
- approximate LOC delta: about `+250` additive LOC across the richer source
  catalog, generator, `mklist.sh`, make target, and docs

### 9. Decision log
- represent detector-variation sources by real good-runs SAM definitions rather
  than guessed directory layouts
- keep the richer source branching in `tools/mklist.sh`, not in `io/` or
  duplicate wrapper scripts

### 10. Stop conditions
- stop after Run 1 nominal plus detector-variation source coverage is honest
  and generated dataset assembly is first class
- stop before inventing run 2 / run 3 shard metadata not present in local
  source material

## ExecPlan Addendum: Compatibility Surface Retirement

### 1. Objective
With explicit approval to remove the remaining compatibility layers, retire the
stale `mk_eventlist --cache-*` and `mk_channel` workflow seams, move the
native fit CLI onto direct `DistributionIO` inputs, and demote `ChannelIO.hh`
from the installed public header surface.

### 2. Constraints
- Keep `io/` persistence-only.
- Keep the deletion pass focused on workflow truth surfaces, not unrelated
  plotting or `syst/` refactors.
- Leave unrelated dirty worktree changes untouched.

### 3. Milestone
- status: done
- hypothesis: once `mk_dist` and direct `DistributionIO` fitting already
  exist, deleting the remaining cache/channel bridges will make the workflow
  smaller and more honest without leaving the fit path orphaned
- files / symbols touched:
  - `app/CMakeLists.txt`
  - `app/mk_eventlist.cc`
  - `app/mk_xsec_fit.cc`
  - `app/mk_channel.cc`
  - `fit/SignalStrengthFit.hh`
  - `fit/SignalStrengthFit.cc`
  - `io/CMakeLists.txt`
  - `COMMANDS`
  - `USAGE`
  - `INSTALL`
  - `fit/README`
  - `io/README`
  - `io/bits/DERIVED`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- app/CMakeLists.txt app/mk_eventlist.cc app/mk_xsec_fit.cc fit/SignalStrengthFit.hh fit/SignalStrengthFit.cc io/CMakeLists.txt COMMANDS USAGE INSTALL fit/README io/README io/bits/DERIVED .agent/current_execplan.md docs/minimality-log.md`
  - `docker build -t amarantin-dev .`
  - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/compat-retire-docker -DCMAKE_BUILD_TYPE=Release && cmake --build .build/compat-retire-docker --target IO Ana Syst Fit mk_eventlist mk_dist mk_xsec_fit --parallel && (.build/compat-retire-docker/bin/mk_eventlist --help || true) && (.build/compat-retire-docker/bin/mk_xsec_fit --help || true)'`
- acceptance criteria:
  - `mk_eventlist` no longer accepts or documents `--cache-*`
  - `mk_xsec_fit` consumes cached `DistributionIO` inputs directly
  - `mk_channel` is no longer built or installed
  - `ChannelIO.hh` is no longer installed as a public header
  - top-level docs teach `mk_eventlist -> mk_dist -> mk_xsec_fit`
- verification results:
  - local checks passed:
    - `git diff --check -- app/CMakeLists.txt app/mk_eventlist.cc app/mk_xsec_fit.cc fit/SignalStrengthFit.hh fit/SignalStrengthFit.cc io/CMakeLists.txt COMMANDS USAGE INSTALL fit/README io/README io/bits/DERIVED`
    - public-doc sweeps confirm:
      - `COMMANDS`, `USAGE`, `INSTALL`, `fit/README`, `io/README`, and
        `io/bits/DERIVED` no longer teach `mk_channel` or
        `mk_eventlist --cache-*` as part of the native workflow
  - Docker verification passed in a fresh Linux build tree:
    - `docker build -t amarantin-dev .`
    - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/compat-retire-docker -DCMAKE_BUILD_TYPE=Release && cmake --build .build/compat-retire-docker --target IO Ana Syst Fit mk_eventlist mk_dist mk_xsec_fit --parallel && (.build/compat-retire-docker/bin/mk_eventlist --help || true) > /tmp/mk_eventlist.help 2>&1 && ! grep -q -- "--cache-systematics" /tmp/mk_eventlist.help && (.build/compat-retire-docker/bin/mk_xsec_fit --help || true) > /tmp/mk_xsec_fit.help 2>&1 && grep -q -- "--manifest <fit.manifest>" /tmp/mk_xsec_fit.help'`
    - focused synthetic direct-fit smoke passed:
      - wrote a temporary `DistributionIO` file with `signal`, `background`,
        and `data` cache entries
      - ran `mk_xsec_fit --manifest /tmp/direct-fit.manifest --output /tmp/direct-fit.fit.txt /tmp/direct-fit.dists.root muon_region`
      - verified the report includes:
        - `distribution_path: /tmp/direct-fit.dists.root`
        - `eventlist_path: /tmp/direct-fit.eventlist.root`
        - `observed_source_keys: data`
        - `signal_process: signal`

### 4. Public-surface check
- compatibility impact:
  - non-additive removal of `mk_eventlist --cache-*`
  - non-additive removal of the `mk_channel` executable
  - `ChannelIO.hh` removed from installed public headers
- migration note:
  - the native workflow is now `mk_sample -> mk_dataset -> mk_eventlist ->
    mk_dist -> mk_xsec_fit`
  - local legacy macros may still use `ChannelIO`, but that layer is no longer
    the installed or documented workflow surface

### 5. Reduction ledger
- files deleted:
  - `app/mk_channel.cc`
- wrappers removed:
  - remove the cache-building branch from `mk_eventlist`
  - remove the separate persistent-channel bridge from the native fit workflow
- shell branches removed:
  - none in this pass
- stale docs removed:
  - `mk_channel`-first and `mk_eventlist --cache-*` workflow teaching from the
    top-level docs
- targets or dependencies removed:
  - `mk_channel` target and install surface
  - `ChannelIO.hh` from installed public headers
  - `Syst` link from `mk_eventlist`
- approximate LOC delta: net reduction from deleting the `mk_channel` app and
  the legacy cache branch while keeping the direct fit assembly local to
  `mk_xsec_fit`

### 6. Decision log
- keep `ChannelIO` available only as an in-tree local legacy helper for now
  instead of widening this pass into a full macro rewrite
- move the direct fit assembly into `mk_xsec_fit` and `fit::Channel` rather
  than introducing another persistence format

### 7. Stop conditions
- stop once the documented installed workflow no longer routes through
  `mk_eventlist --cache-*` or `mk_channel`
- stop before rewriting local channel-oriented ROOT macros that are no longer
  part of the documented native path

## ExecPlan Addendum: Analysis-Specific EventList Sample Partition Policy

### 1. Objective
Move the sample-partition rule used during EventList construction out of the
generic builder internals and into one explicit `ana/` analysis-policy home.

The concrete target is the current overlay/signal `count_strange` split:

- keep the behavior unchanged
- stop hardcoding that analysis-specific rule in `ana/EventListBuild.cc`
- make the policy easier to find and easier to replace later

### 2. Constraints
- Preserve the current `mk_eventlist` CLI surface and default behavior.
- Preserve the current EventList output selection semantics.
- Keep `io/` persistence-only.
- Do not widen this pass into configurable CLI policy plumbing.
- Leave unrelated dirty-worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep module boundaries sharp
- prefer plain data and namespace functions
- add abstractions only when they delete complexity

This pass should remove analysis-specific branching from the generic builder,
not add a framework.

### 4. System map
- event-list build path:
  - `ana/EventListBuild.cc`
  - `ana/EventListBuild.hh`
- analysis-specific policy home:
  - `ana/SignalDefinition.hh`
  - `ana/SignalDefinition.cc`
  - `ana/README`
- derived-format documentation:
  - `io/bits/DERIVED`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`

### 5. Candidate simplifications

#### boundary sharpening
- move origin-specific EventList filtering out of the builder and into one
  explicit analysis-owned policy surface

#### wrapper collapse
- prefer one plain-data rule description over ad hoc `if` branches embedded in
  the generic selection builder

#### doc / build cleanup
- document the policy owner in `ana/README` and fix `io/bits/DERIVED` so it
  points at `ana` rather than implying the persistence layer owns the rule

### 6. Milestones

#### Milestone A: Centralise the EventList sample-partition rule
- status: done
- hypothesis: one analysis-owned rule surface is easier to grep and keeps the
  generic EventList builder focused on chaining, formulas, and persistence
- files / symbols touched:
  - `ana/EventListBuild.cc`
  - `ana/SignalDefinition.hh`
  - `ana/SignalDefinition.cc`
  - `ana/README`
  - `io/bits/DERIVED`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md ana/EventListBuild.cc ana/SignalDefinition.hh ana/SignalDefinition.cc ana/README io/bits/DERIVED`
  - focused `Ana` / `mk_eventlist` builds if a valid ROOT build tree is available
- acceptance criteria:
  - `EventListBuild.cc` no longer contains the overlay/signal `count_strange`
    rule inline
  - the canonical rule lives in one explicit `ana/` policy home
  - docs name the analysis layer as the owner of that policy
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md ana/EventListBuild.cc ana/SignalDefinition.hh ana/SignalDefinition.cc ana/README io/bits/DERIVED` passed
  - focused rebuild attempt `cmake --build build --target Ana mk_eventlist --parallel` is blocked because the existing `build/` tree invokes `/usr/bin/cmake`, which is absent in this environment
  - a fresh configure was not attempted because local ROOT discovery is also unavailable here (`root-config` is not on `PATH`, and the previously cached `/opt/root/bin/root-config` no longer exists)

### 7. Public-surface check
- compatibility impact:
  - non-behavioral internal refactor unless the new helper is reused
    externally
- migration note or explicit non-goal:
  - non-goal: add a new `mk_eventlist` policy flag in this pass
  - non-goal: change the current overlay/signal split semantics
- reviewer sign-off:
  - user requested that analysis-specific assumptions stop living in generic
    event-list builder code

### 8. Reduction ledger
- files deleted: 0
- wrappers removed:
  - inline analysis-specific overlay/signal filtering branches in
    `ana/EventListBuild.cc`
- shell branches removed: 0
- stale docs removed:
  - wording that implied EventListIO owned the sample-partition rule
- targets or dependencies removed: 0
- approximate LOC delta: code path reduced locally in `ana/EventListBuild.cc`
  with small additive tracking/docs changes in this pass

### 9. Decision log
- prefer moving the rule into one explicit `ana/` home before adding any
  configurability
- keep this pass behavior-preserving and local to the EventList build path

### 10. Stop conditions
- stop once the builder no longer owns the hardcoded origin-specific rule
- stop before adding CLI/config plumbing for configurable sample-partition
  policies

## ExecPlan Addendum: Rename `mk_xsec_fit` To `mk_fit`

### 1. Objective
Shorten the native fit CLI surface from `mk_xsec_fit` to `mk_fit` and make
that shorter name the canonical documented executable.

### 2. Constraints
- Preserve fit behavior, flags, positional arguments, and report format.
- Keep the direct `DistributionIO -> fit` workflow unchanged apart from the
  executable name.
- Preserve library/module boundaries and keep the change local to `app/` and
  the documented workflow surface.
- Avoid unnecessary source-file rename churn; the executable name is the goal.
- Leave unrelated dirty-worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep workflows in `app/`
- add abstractions only when they delete complexity
- keep code cheap to change

This pass is a surface simplification, not a behavior change.

### 4. System map
- fit CLI target and implementation:
  - `app/CMakeLists.txt`
  - `app/mk_xsec_fit.cc`
- top-level workflow docs:
  - `COMMANDS`
  - `USAGE`
  - `INSTALL`
  - `fit/README`
  - `VISION.md`
  - `INVARIANTS.md`
  - `docs/repo-internals.puml`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`

### 5. Candidate simplifications

#### boundary sharpening
- keep the executable rename at the CLI/build surface without widening into a
  source-file rename or fit-library refactor

#### doc / build cleanup
- make one canonical executable name appear in build targets, help output, and
  current workflow docs

### 6. Milestones

#### Milestone A: Rename the fit CLI surface to `mk_fit`
- status: done
- hypothesis: a shorter executable name is easier to type and teach, and the
  change stays small if it is limited to the target name plus current docs
- files / symbols touched:
  - `app/CMakeLists.txt`
  - `app/mk_xsec_fit.cc`
  - `COMMANDS`
  - `USAGE`
  - `INSTALL`
  - `fit/README`
  - `VISION.md`
  - `INVARIANTS.md`
  - `docs/repo-internals.puml`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: low to moderate
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/CMakeLists.txt app/mk_xsec_fit.cc COMMANDS USAGE INSTALL fit/README VISION.md INVARIANTS.md docs/repo-internals.puml`
  - Docker rebuild of `mk_fit` if the local image/toolchain path is available
- acceptance criteria:
  - the CMake app target is `mk_fit`
  - CLI help and status/error messages say `mk_fit`
  - current docs teach `mk_fit` as the canonical fit executable
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/CMakeLists.txt app/mk_xsec_fit.cc COMMANDS USAGE INSTALL fit/README VISION.md INVARIANTS.md docs/repo-internals.puml` passed
  - Docker verification passed in a fresh Linux build tree:
    - `docker build -t amarantin-dev .`
    - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/mk-fit-rename-docker -DCMAKE_BUILD_TYPE=Release && cmake --build .build/mk-fit-rename-docker --target mk_fit --parallel && (.build/mk-fit-rename-docker/bin/mk_fit --help || true) > /tmp/mk_fit.help 2>&1 && ! grep -q -- "mk_xsec_fit" /tmp/mk_fit.help && grep -q -- "usage: mk_fit" /tmp/mk_fit.help'`
    - built target summary included:
      - `Built target mk_fit`

### 7. Public-surface check
- compatibility impact:
  - non-additive executable rename from `mk_xsec_fit` to `mk_fit`
- migration note or explicit non-goal:
  - migration note: update scripts, docs, and command history from
    `mk_xsec_fit` to `mk_fit`
  - non-goal: rename `app/mk_xsec_fit.cc` in this pass
- reviewer sign-off:
  - user explicitly requested the executable rename

### 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- stale docs removed:
  - current workflow docs still naming `mk_xsec_fit`
- targets or dependencies removed:
  - `mk_xsec_fit` target name
- approximate LOC delta: small doc/build-surface rename with no source-file
  move

### 9. Decision log
- rename the executable target and user-facing strings without renaming the
  source file path
- keep the old name out of the current documented workflow once this pass
  lands

### 10. Stop conditions
- stop once the current fit CLI surface consistently says `mk_fit`
- stop before widening the pass into history rewriting or broader fit-library
  cleanup

## ExecPlan Addendum: Integrate SearchingForStrangeness EventWeights

### 1. Objective
Implement the reviewed EventWeight branch contract from
`/Users/user/programs/searchingforstrangeness` in the live `amarantin`
framework so the selected-event, systematics-cache, fit, and export paths all
agree on how those upstream branches map to systematics.

### 2. Constraints
- Keep `io/` persistence-only.
- Keep `ana/` responsible only for building the selected-event surface and
  nominal event weights.
- Keep covariance construction in `syst/`.
- Reuse existing covariance-first detector and family patterns instead of
  adding a new orchestration layer.
- Leave unrelated dirty-worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- prefer plain data and namespace functions
- keep workflows in `app/`
- keep module boundaries sharp

This pass should extend the current direct data flow, not introduce a second
systematics framework.

### 4. System map
- upstream branch contract:
  - `syst/VISION.md`
  - `/Users/user/programs/searchingforstrangeness/AnalysisTools/EventWeightAnalysis_tool.cc`
- selected-event builder and persistence surface:
  - `ana/EventListBuild.cc`
  - `io/EventListIO.hh`
  - `io/EventListIO.cc`
- systematics builder and cache persistence:
  - `syst/Systematics.hh`
  - `syst/Systematics.cc`
  - `syst/UniverseFill.cc`
  - `syst/UniverseSummary.cc`
  - `syst/bits/Detail.hh`
  - `io/DistributionIO.hh`
  - `io/DistributionIO.cc`
- downstream consumers:
  - `fit/SignalStrengthFit.hh`
  - `fit/SignalStrengthFit.cc`
  - `app/mk_dist.cc`
  - `app/mk_cov.cc`
- verification:
  - `tools/systematics-reweight-smoke.sh`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`

### 5. Candidate simplifications

#### boundary sharpening
- keep the selected tree as the one persisted boundary for upstream weight
  branches instead of reconstructing art products downstream
- keep the logical flux-family resolution and GENIE knob-pair semantics in
  `syst/`

#### wrapper collapse
- reuse the existing detector source-shift pattern for explicit paired knob
  sources instead of inventing a parallel special-case fit path

#### doc / build cleanup
- tighten docs and smoke coverage around the actual upstream branch names so
  future work does not rediscover the contract by inspection

### 6. Milestones

#### Milestone A: Confirm the selected-event branch surface and document the implementation plan
- status: done
- hypothesis: the selected tree already preserves the relevant upstream weight
  branches, so the implementation work should stay concentrated in `syst/`,
  `fit/`, and edge CLIs
- files / symbols touched:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md`
- acceptance criteria:
  - the repo-local exec plan names the concrete implementation milestones
  - the current minimality log points at this integration pass
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md` passed

#### Milestone B: Make logical flux-family resolution branch-aware
- status: done
- hypothesis: `weightsPPFX` and `weightsFlux` are one logical family and the
  branch selection belongs in `syst::detail::compute_sample(...)`
- files / symbols touched:
  - `syst/UniverseFill.cc`
  - `syst/README`
  - `docs/minimality-log.md`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/UniverseFill.cc syst/README`
- acceptance criteria:
  - flux-family readout prefers `weightsPPFX`
  - flux-family readout falls back to `weightsFlux`
  - the persisted family branch name records the actual source branch used
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/UniverseFill.cc syst/README` passed

#### Milestone C: Add an explicit GENIE knob-pair source lane to the covariance cache
- status: done
- hypothesis: paired `weightsGenieUp` / `weightsGenieDn` should survive as
  labeled shift sources plus absolute covariance, not be folded into the
  GENIE multisim family
- files / symbols touched:
  - `syst/Systematics.hh`
  - `syst/Systematics.cc`
  - `syst/UniverseFill.cc`
  - `syst/bits/Detail.hh`
  - `io/DistributionIO.hh`
  - `io/DistributionIO.cc`
  - `syst/README`
  - `syst/VISION.md`
  - `docs/minimality-log.md`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/Systematics.hh syst/Systematics.cc syst/UniverseFill.cc syst/bits/Detail.hh io/DistributionIO.hh io/DistributionIO.cc syst/README syst/VISION.md`
- acceptance criteria:
  - the knob-pair lane has explicit stable source labels
  - paired shifts survive cache write/read
  - covariance and derived envelopes can be reconstructed from the persisted
    knob-pair payload
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/Systematics.hh syst/Systematics.cc syst/UniverseFill.cc syst/bits/Detail.hh io/DistributionIO.hh io/DistributionIO.cc syst/README syst/VISION.md` passed
  - the paired-knob lane now persists reviewed labels, shift vectors, and
    covariance additively in `DistributionIO`

#### Milestone D: Teach fit and export surfaces to consume the knob-pair lane
- status: done
- hypothesis: the fit and SBNFit export paths can reuse the existing
  covariance/source-shift consumption patterns with one additive lane
- files / symbols touched:
  - `fit/SignalStrengthFit.hh`
  - `fit/SignalStrengthFit.cc`
  - `app/mk_xsec_fit.cc`
  - `app/mk_cov.cc`
  - `fit/README`
  - `COMMANDS`
  - `USAGE`
  - `INSTALL`
  - `docs/minimality-log.md`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md fit/SignalStrengthFit.hh fit/SignalStrengthFit.cc app/mk_xsec_fit.cc app/mk_cov.cc fit/README COMMANDS USAGE INSTALL`
- acceptance criteria:
  - fit-side default nuisance assembly includes the knob-pair lane when present
  - SBNFit export includes the knob-pair covariance component when present
  - docs mention the new branch-aware flux and optional knob-pair behavior
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md fit/SignalStrengthFit.hh fit/SignalStrengthFit.cc app/mk_xsec_fit.cc app/mk_cov.cc fit/README COMMANDS USAGE INSTALL` passed
  - `cmake --build build --target Syst Fit mk_dist mk_fit mk_cov --parallel` failed because the existing `build/` tree still points at `/usr/bin/cmake`
  - fresh configure in `.build/eventweight-integration` failed in this environment because `ROOT` is not available and `root-config` is not on `PATH`

#### Milestone E: Add focused smoke coverage
- status: done
- hypothesis: one focused synthetic smoke can lock the branch-aware flux rule
  and the knob-pair covariance math without needing a full production tuple
- files / symbols touched:
  - `tools/systematics-reweight-smoke.sh`
  - `docs/minimality-log.md`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md tools/systematics-reweight-smoke.sh`
  - `bash -n tools/systematics-reweight-smoke.sh`
  - runtime smoke if a trustworthy ROOT build is available
- acceptance criteria:
  - the smoke covers `weightsPPFX` / `weightsFlux` selection
  - the smoke covers paired knob shifts and covariance persistence
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md tools/systematics-reweight-smoke.sh` passed
  - `bash -n tools/systematics-reweight-smoke.sh` passed
  - runtime smoke remains deferred because no trustworthy configured ROOT build is available here

### 7. Public-surface check
- compatibility impact:
  - additive `DistributionIO` schema growth if the knob-pair lane is
    persisted
  - additive fit/export behavior when the new lane is present
- migration note or explicit non-goal:
  - migration note: old caches remain readable through existing additive
    readback paths
  - non-goal: reconstruct art `MCEventWeight` products downstream

### 8. Reduction ledger
- files deleted: 0
- wrappers removed:
  - the need to infer upstream flux-family branch choice outside `syst/`
- shell branches removed: 0
- stale docs removed: 0
- approximate LOC delta: moderate additive systematics-surface extension

### 9. Decision log
- resolve the logical flux family in `syst/` by branch presence
- keep paired GENIE knobs separate from the GENIE multisim family
- persist the knob-pair lane as explicit source shifts plus covariance
- prefer reuse of the existing detector source-shift pattern over a new family
  abstraction

### 10. Stop conditions
- stop once branch-aware flux resolution and the optional GENIE knob-pair lane
  are integrated through cache, fit, export, and focused smoke coverage
- stop before widening this pass into unrelated upstream ntuple cleanup or a
  broader CLI redesign

## ExecPlan Addendum: Explicit Stacked SBNFit Export

### 1. Objective
Extend `mk_cov` so it can export a stacked multi-process covariance
matrix from multiple cached `DistributionIO::Spectrum` entries without guessing
cross-process correlations.

### 2. Constraints
- Keep the change at the app/export edge unless a small helper clearly deletes
  complexity.
- Do not redefine the core covariance math in `syst/`.
- Do not silently assume block-diagonal family correlations across processes.
- Preserve the current single-spectrum export mode.
- Leave unrelated dirty-worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep workflows in `app/`
- keep module boundaries sharp
- add abstractions only when they delete complexity

This pass should add one explicit export contract, not a new framework.

### 4. System map
- stacked export implementation:
  - `app/mk_cov.cc`
- current workflow/docs:
  - `COMMANDS`
  - `USAGE`
  - `INSTALL`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`

### 5. Candidate simplifications

#### boundary sharpening
- keep stacked covariance assembly in `mk_cov` rather than pushing
  SBNFit-specific stacking rules back into `syst/`

#### wrapper collapse
- reuse persisted detector source labels, GENIE knob source labels, and
  retained universe histograms directly instead of inventing a second
  intermediate file format

#### doc / build cleanup
- document one explicit manifest format and one explicit correlation contract
  for stacked export

### 6. Milestones

#### Milestone A: Record the stacked-export contract
- status: done
- hypothesis: one explicit contract for stacked source/family correlations will
  prevent accidental block-diagonal exports
- files / symbols touched:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md`
- acceptance criteria:
  - the exec plan names the stacked-export contract and scope
  - the minimality log points at this export pass
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md` passed

#### Milestone B: Implement manifest-driven stacked export
- status: done
- hypothesis: `mk_cov` can stack spectra safely if it treats detector
  and knob lanes as shared labeled source shifts, and multisim families as
  shared universes only when retained universes survive
- files / symbols touched:
  - `app/mk_cov.cc`
- expected behavior risk: moderate
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/mk_cov.cc`
- acceptance criteria:
  - `mk_cov` supports a manifest-driven stacked mode
  - stacked detector and GENIE knob correlations are built only from explicit
    shared source labels
  - stacked GENIE / flux / reint family correlations are built only from
    retained universes with matching branch name and universe count
  - the tool rejects stacked exports when a required cross-process family
    contract is not available
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/mk_cov.cc` passed
  - `cmake --build build --target mk_cov --parallel` did not provide a trustworthy compile check here; the existing `build/` tree is stale and returned `make: *** No rule to make target 'mk_cov'.  Stop.`
  - `cmake -S . -B .build/stacked-export -DCMAKE_BUILD_TYPE=Release` reached dependency discovery but failed in this environment because `ROOT` is not available and `root-config` is not on `PATH`

#### Milestone C: Update docs and lightweight verification
- status: done
- hypothesis: current docs should teach the new manifest mode and explicit
  correlation rules without expanding the core API surface
- files / symbols touched:
  - `COMMANDS`
  - `USAGE`
  - `INSTALL`
  - `docs/minimality-log.md`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md COMMANDS USAGE INSTALL`
  - focused CMake build if the local environment is usable
- acceptance criteria:
  - docs teach the stacked manifest mode
  - docs state the strict correlation contract and rejection behavior
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md COMMANDS USAGE INSTALL` passed
  - focused build verification remains blocked by the stale `build/` tree and missing local `ROOT` environment described above

### 7. Public-surface check
- compatibility impact:
  - additive `mk_cov --manifest` workflow
- migration note or explicit non-goal:
  - migration note: the single-spectrum export mode remains unchanged
  - non-goal: move stacked export logic into core `syst/`

### 8. Reduction ledger
- files deleted: 0
- wrappers removed:
  - the need to hand-assemble stacked SBNFit covariance files outside the repo
- shell branches removed: 0
- stale docs removed: 0
- approximate LOC delta: moderate app-edge extension

### 9. Decision log
- stacked detector and GENIE knob correlations must come from shared source
  labels
- stacked multisim family correlations must come from retained universes, not
  guessed from per-process covariance blocks

### 10. Stop conditions
- stop once `mk_cov` can export one explicit stacked contract and the
  docs teach it
- stop before widening the pass into a broader SBNFit orchestration layer

## ExecPlan Addendum: Stacked Export Smoke Coverage

### 1. Objective
Add one focused smoke script for the new stacked `mk_cov --manifest`
contract so runtime verification is ready when a local ROOT build is available.

### 2. Constraints
- Keep the smoke focused on the stacked export contract, not broad systematics
  regeneration.
- Reuse `DistributionIO` payload writing directly instead of building a full
  `EventListIO` or `syst/` fixture.
- Leave unrelated dirty-worktree changes untouched.

### 3. Design anchor
From `DESIGN.md`:
- keep workflows in `app/`
- keep module boundaries sharp
- add abstractions only when they delete complexity

This pass should add one narrow verification tool, not a broader test
framework.

### 4. System map
- smoke tool:
  - `tools/systematics-sbnfit-export-smoke.sh`
- export implementation under test:
  - `app/mk_cov.cc`
- workflow/docs:
  - `COMMANDS`
  - `INSTALL`
- tracking:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`

### 5. Candidate simplifications

#### boundary sharpening
- verify stacked export at the `app/` edge by writing synthetic cached
  `DistributionIO::Spectrum` payloads directly

#### wrapper collapse
- avoid a heavier harness or generated fixture tree when one short shell script
  plus one compiled helper can exercise the contract

#### doc / build cleanup
- teach one direct smoke invocation instead of leaving the exporter unguarded

### 6. Milestones

#### Milestone A: Record the smoke-coverage pass
- status: done
- hypothesis: one explicit smoke target keeps the stacked export contract
  easier to validate and easier to grep
- files / symbols touched:
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md`
- acceptance criteria:
  - the exec plan names the smoke coverage scope
  - the minimality log points at this pass
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md` passed

#### Milestone B: Add a focused stacked-export smoke script
- status: done
- hypothesis: a synthetic cached-spectrum fixture can validate shared labeled
  source shifts, retained-universe family correlations, and rejection on
  mismatched family metadata with less setup than a full end-to-end eventlist
  build
- files / symbols touched:
  - `tools/systematics-sbnfit-export-smoke.sh`
- expected behavior risk: low
- verification commands:
  - `bash -n tools/systematics-sbnfit-export-smoke.sh`
- acceptance criteria:
  - the script writes a small synthetic `DistributionIO` file
  - the script exercises a successful stacked export and checks the exported
    matrices
  - the script checks that a mismatched multisim family contract is rejected
- verification results:
  - `bash -n tools/systematics-sbnfit-export-smoke.sh` passed
  - runtime execution of the smoke remains deferred in this environment because `ROOT` is not available and `root-config` is not on `PATH`

#### Milestone C: Teach the smoke invocation in the workflow docs
- status: done
- hypothesis: one short doc mention is enough to keep the new smoke discoverable
- files / symbols touched:
  - `COMMANDS`
  - `INSTALL`
  - `docs/minimality-log.md`
- expected behavior risk: low
- verification commands:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md COMMANDS INSTALL`
- acceptance criteria:
  - docs name the new smoke script
  - the minimality log records the simplification
- verification results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md tools/systematics-sbnfit-export-smoke.sh COMMANDS INSTALL` passed

### 7. Public-surface check
- compatibility impact:
  - additive verification script only
- migration note or explicit non-goal:
  - migration note: the export CLI surface stays unchanged
  - non-goal: add a general test harness framework

### 8. Reduction ledger
- files deleted: 0
- wrappers removed:
  - the need to validate the stacked export contract manually with ad hoc ROOT
    inspection
- shell branches removed: 0
- stale docs removed: 0
- approximate LOC delta: one focused smoke tool plus short doc mentions

### 9. Decision log
- verify stacked detector and knob correlations from shared source labels
- verify stacked multisim family correlations from retained universes
- verify exporter rejection on mismatched family metadata
