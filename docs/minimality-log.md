# Minimality Log

## Current milestone
- status: done
- subsystem: real `test.root` coverage expansion
- design rule from `DESIGN.md`: keep the fixture smoke small and direct while
  exercising the real persisted row-wise and cached-bin surfaces

## What changed
- started an optional `test.root` fixture smoke under `tests/`
- broadened the fixture checker so it validates `Snapshot`, row-wise plotting,
  covariance export, and fit outputs on the real fixture outputs
- broadened the fixture shell smoke so it drives the analyzer macros against
  those real outputs
- made the fixture path configurable with one CMake cache variable instead of
  only one hard-coded local filename
- added a second real-fixture `mk_eventlist --preset muon` pass so the
  selection-definition path is exercised on the same file
- taught `ana::build_event_list(...)` to accept `sub` as the event-tree
  subrun branch alias for explicit tree paths
- taught the reweight systematics path to ignore empty GENIE knob-pair
  placeholder branches instead of treating them as malformed populated payloads

## Why this is simpler
- one optional smoke now checks the real downstream fixture path instead of
  leaving the most analysis-facing outputs to synthetic coverage only
- the fixture remains opt-in through one path knob rather than becoming a
  required binary input for every configure
- the analyzer inspection surface is now checked on the same real outputs a
  user would inspect manually
- the explicit-tree fixture path works with the existing `sub` branch name
  instead of needing a special fixture rewrite
- empty knob placeholder branches now collapse to "absent" at the boundary,
  which matches the persisted cache contract and avoids fake zero-valued knob
  components

## Verification
- configure/build commands:
-  `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/testroot-rigorous -DCMAKE_BUILD_TYPE=Release -DAMARANTIN_TEST_ROOT_FIXTURE=/work/test.root && cmake --build .build/testroot-rigorous --parallel && ctest --test-dir .build/testroot-rigorous --output-on-failure'`
- target-only commands:
- shell checks:
-  `git diff --check -- syst/ReweightFill.cc tests/systematics_rigorous_check.cc tests/testroot_pipeline_check.cc tools/test-root-smoke.sh ana/EventListBuild.cc .agent/current_execplan.md docs/minimality-log.md tests/CMakeLists.txt COMMANDS INSTALL`
-  `bash -n tools/test-root-smoke.sh`
- smoke checks:
- results:
  - focused `git diff --check` passed
  - `bash -n tools/test-root-smoke.sh` passed
  - Docker `ctest` passed with:
    - `testroot_pipeline_smoke`
    - `fit_rigorous_check`
    - `plot_rigorous_check`
    - `io_rigorous_check`
    - `systematics_rigorous_check`
    - `macro_analysis_smoke`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - positive; one optional fixture checker plus a broader shell smoke

## Decisions
- keep `test.root` optional through a CMake path knob
- skip event-display fixture coverage because the needed detector-image
  branches are not part of this fixture
- keep the fixture smoke on the explicit `nuselection/...` tree paths
- treat empty `weightsGenieUp` / `weightsGenieDn` vectors as an absent knob
  lane, not as a malformed populated one

## Remaining hotspots
- detector-variation and stacked multi-sample paths still need other fixtures;
  one single-file `test.root` cannot cover them cleanly

## Current milestone
- status: done
- subsystem: analyzer-facing macro validation
- design rule from `DESIGN.md`: keep macros thin and make the persisted
  `EventListIO` and `DistributionIO` debug surfaces directly inspectable

## What changed
- added a read-only `inspect_weights` macro over the persisted event-weight
  surface
- added a read-only `inspect_systematics` macro over one cached spectrum's
  detector / knob / family payloads
- started a shell-driven macro smoke that runs both through `tools/run-macro`
- removed a stale deleted-header include from `.rootlogon.C`
- replaced stale deleted fit-macro examples in the macro-runner docs

## Why this is simpler
- analyzer debug sessions can inspect the main persisted weight and
  systematics surfaces without retyping ROOT snippets by hand
- the shared macro path is easier to trust when it no longer depends on a
  deleted header
- one smoke now exercises the real ROOT macro runner path end to end

## Verification
- configure/build commands:
-  `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/macro-rigorous -DCMAKE_BUILD_TYPE=Release && cmake --build .build/macro-rigorous --parallel && ctest --test-dir .build/macro-rigorous --output-on-failure'`
- target-only commands:
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md .rootlogon.C io/macro/inspect_weights.C plot/macro/inspect_systematics.C tools/run-macro tools/macro-analysis-smoke.sh tests/CMakeLists.txt COMMANDS USAGE`
-  `bash -n tools/run-macro`
-  `bash -n tools/macro-analysis-smoke.sh`
- smoke checks:
- results:
  - focused `git diff --check` passed
  - `bash -n tools/run-macro` passed
  - `bash -n tools/macro-analysis-smoke.sh` passed
  - Docker `ctest` passed with:
    - `fit_rigorous_check`
    - `plot_rigorous_check`
    - `io_rigorous_check`
    - `systematics_rigorous_check`
    - `macro_analysis_smoke`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - positive; two thin macros plus one shell smoke and small runner/doc
    cleanup

## Decisions
- keep the new macros read-only and inspection-oriented
- defer any semantic final-region plotting macro surface until the repo has a
  cleaner downstream query API

## Remaining hotspots
- the macro surface is still local convenience code; it should not grow into a
  second application workflow
- final-region analyzer macros still need a cleaner semantic query API before
  they can avoid raw sample/cache choices cleanly

## Current milestone
- status: done
- subsystem: rigorous `fit/` validation and boundary hardening
- design rule from `DESIGN.md`: keep the fit surface plain and make malformed
  channel/cache inputs fail where prediction and fitting begin

## What changed
- hardened `fit::validate_problem(...)` so duplicate non-data process names,
  invalid signal-process kinds, malformed family payloads, incomplete
  envelopes, and invalid channel binning fail explicitly
- started a self-contained `fit_rigorous_check` executable under `tests/`
- documented the unique non-data process / signal-process expectations in
  `fit/README`

## Why this is simpler
- the main fit assumptions now fail at the fit boundary instead of producing
  ambiguous nuisance construction or late runtime errors
- one deterministic test exercises the default nuisance builder and prediction
  math directly, without needing a broader fixture chain

## Verification
- configure/build commands:
-  `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/fit-rigorous -DCMAKE_BUILD_TYPE=Release && cmake --build .build/fit-rigorous --parallel && ctest --test-dir .build/fit-rigorous --output-on-failure'`
- target-only commands:
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md fit/README fit/SignalStrengthFit.cc tests/CMakeLists.txt tests/fit_rigorous_check.cc`
- smoke checks:
- results:
  - focused `git diff --check` passed
  - Docker `ctest` passed with:
    - `fit_rigorous_check`
    - `plot_rigorous_check`
    - `io_rigorous_check`
    - `systematics_rigorous_check`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - positive; one deterministic regression plus small fail-fast guards

## Decisions
- keep the new fit regression synthetic and self-contained
- harden only the boundary checks that make the persisted fit payload easier
  to trust

## Remaining hotspots
- the committed fit coverage is still synthetic; it does not yet exercise
  multi-channel coupling or covariance-driven downstream consumers beyond the
  one-channel `mk_fit` path

## Current milestone
- status: done
- subsystem: rigorous `plot/` validation and `IO` boundary hardening
- design rule from `DESIGN.md`: keep `plot/` rendering-only and make the
  persisted row/bin surfaces fail clearly instead of silently

## What changed
- hardened default `EventListIO` plot enumeration so detector-variation
  samples are not treated as nominal row-wise inputs
- hardened row-wise plot filling so bad `TTree::Draw(...)` formulas fail
  explicitly
- hardened cached-spectrum plotting so malformed `nominal` / `sumw2` payloads
  fail before a histogram is built
- started a self-contained `plot_rigorous_check` executable under `tests/`
- documented the default detector-variation skip in `plot/README`

## Why this is simpler
- row-wise plots now match the nominal/data view users usually intend instead
  of silently pulling detector alternates into the stack
- bad plot expressions stop at the rendering boundary instead of returning an
  empty image that looks valid
- one deterministic test covers the main `plot/` and `IO` contracts directly

## Verification
- configure/build commands:
- target-only commands:
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md plot/EventListPlotting.cc plot/PlottingHelper.cc plot/EfficiencyPlot.cc plot/README tests/CMakeLists.txt tests/plot_rigorous_check.cc`
- smoke checks:
-  `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/plot-rigorous -DCMAKE_BUILD_TYPE=Release && cmake --build .build/plot-rigorous --parallel && ctest --test-dir .build/plot-rigorous --output-on-failure'`
- results:
  - focused `git diff --check` passed
  - Docker `ctest` passed with:
    - `plot_rigorous_check`
    - `io_rigorous_check`
    - `systematics_rigorous_check`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - positive; one deterministic regression plus small fail-fast guards

## Decisions
- default row-wise `EventListIO` plotting should treat detector samples as
  opt-in
- explicit sample-key selection should remain able to target detector samples

## Remaining hotspots
- the committed plot coverage still does not exercise the event-display image
  path or cached-plot consumers outside the in-repo helpers

## Current milestone
- status: done
- subsystem: rigorous `io/` validation and persistence-contract hardening
- design rule from `DESIGN.md`: keep `io/` persistence-only and make the
  workflow chain easier to trust without adding wrapper ceremony

## What changed
- hardened `ShardIO::scan(...)` so the scanned file list matches the public
  provenance surface
- hardened `SampleIO::read(...)` so persisted beam/polarity metadata is
  validated on read
- hardened `DistributionIO::write(...)` so malformed payload shapes fail
  before they are persisted
- started a self-contained `io_rigorous_check` executable under `tests/`
- made `EventListIO` store/read subrun trees by leaf name while preserving the
  explicit metadata path

## Why this is simpler
- the main `io/` boundary assumptions now fail where the persistence surface is
  constructed instead of leaking silent inconsistencies downstream
- one deterministic test exercises the chain directly with tiny in-process
  ROOT and SQLite fixtures
- the event-list subrun-tree contract is easier to explain: metadata can keep
  the explicit path while the stored object stays a plain tree key

## Verification
- configure/build commands:
- target-only commands:
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md io/ShardIO.cc io/SampleIO.cc io/EventListIO.cc io/DistributionIO.cc tests/CMakeLists.txt tests/io_rigorous_check.cc`
- smoke checks:
-  `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/io-rigorous -DCMAKE_BUILD_TYPE=Release && cmake --build .build/io-rigorous --parallel && ctest --test-dir .build/io-rigorous --output-on-failure'`
- results:
  - focused `git diff --check` passed
  - Docker `ctest` passed with:
    - `io_rigorous_check`
    - `systematics_rigorous_check`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - positive; one deterministic regression plus small fail-fast guards

## Decisions
- keep the new regression self-contained and publishable on its own
- keep the event-list subrun-tree fix minimal and persistence-focused

## Remaining hotspots
- broader committed IO coverage still does not exercise external malformed
  dataset/event-list files built outside the in-repo writers

## Current milestone
- status: done
- subsystem: rigorous `syst/` validation and assumption hardening
- design rule from `DESIGN.md`: make the data flow easier to trust without
  adding wrapper ceremony

## What changed
- restored the root `BUILD_TESTING` / `check` wiring so committed tests are
  actually configured by CMake
- added one self-contained `systematics_rigorous_check` executable under
  `tests/`
- hardened `syst::detail::compute_sample(...)` so missing `__w__` and changing
  universe counts fail explicitly
- made detector work in `syst/Systematics.cc` obey `enable_detector`

## Why this is simpler
- the main `syst/` assumptions now fail at the boundary where they matter,
  instead of producing silent nonsense
- one deterministic test covers the core detector/reweight math directly,
  without needing a larger fixture harness
- the detector option surface now matches runtime behavior

## Verification
- configure/build commands:
-  `docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/docker-rigorous -DCMAKE_BUILD_TYPE=Release && cmake --build .build/docker-rigorous --parallel && ctest --test-dir .build/docker-rigorous --output-on-failure'`
- target-only commands:
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/ReweightFill.cc syst/Systematics.cc tests/CMakeLists.txt tests/systematics_rigorous_check.cc`
- smoke checks:
- results:
  - focused `git diff --check` passed
  - Docker `ctest` passed with:
    - `systematics_rigorous_check`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - positive; one new deterministic regression test plus small fail-fast guards

## Decisions
- keep the new regression test synthetic and self-contained
- reject malformed selected-tree/systematic payloads explicitly

## Remaining hotspots
- eigenmode-compression specifics and legacy sigma-only cache compatibility are
  still covered only indirectly

## Current milestone
- status: done
- subsystem: dead systematics surface cleanup
- design rule from `DESIGN.md`: delete obsolete options and unread payload
  fields once they stop paying for their complexity

## What changed
- deleted the dead `persist_covariance` option from the public `syst` option
  structs
- deleted the unread `detector_cv_sample_keys` field from
  `DistributionIO::Spectrum` and its ROOT payload read/write path
- kept the sigma-only family branch only as explicit legacy-cache support

## Why this is simpler
- the public `syst` options no longer advertise a knob that cannot change
  runtime behavior
- the persisted spectrum payload no longer carries detector CV-key data that no
  in-repo reader consumes
- the remaining sigma-only path is easier to interpret because it is clearly
  marked as legacy

## Verification
- configure/build commands:
- target-only commands:
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/Systematics.hh syst/Systematics.cc syst/ReweightCovariance.cc io/DistributionIO.hh io/DistributionIO.cc tools/systematics-reweight-smoke.sh`
-  `rg -n "persist_covariance|detector_cv_sample_keys" -S syst io tools`
-  `rg -n "legacy caches may carry only per-bin sigma" -S syst/ReweightCovariance.cc`
- smoke checks:
- results:
  - focused `git diff --check` passed for the cleanup files
  - `rg -n "persist_covariance|detector_cv_sample_keys" -S syst io tools`
    returned no matches
  - the sigma-only family branch is now explicitly marked as legacy-cache
    support
  - `cmake --build build --target IO Syst --parallel` still does not provide a
    trustworthy compile check here because the local `build/` tree points at
    missing `/usr/bin/cmake`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - small negative; dead option/data surface only

## Decisions
- keep the sigma-only family path as explicit legacy-cache handling
- remove only the definitely dead option/data paths in this pass

## Remaining hotspots
- trustworthy compile verification still requires a working local build tree

## Current milestone
- status: done
- subsystem: `syst/` private cache-key helper collapse
- design rule from `DESIGN.md`: keep `bits/` for shared private helpers and
  avoid extra implementation files that do not simplify the boundary

## What changed
- inlined the private cache-key helpers into `syst/bits/Detail.hh`
- deleted `syst/bits/CacheKey.cc`
- removed the dead source entry from `syst/CMakeLists.txt`

## Why this is simpler
- the shared private cache-key surface now lives in one place instead of being
  split between declarations and a tiny one-purpose `.cc`
- `syst/bits/` is easier to scan because there is one fewer private file
- the `Syst` target has one less implementation file to carry

## Verification
- configure/build commands:
- target-only commands:
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/bits/Detail.hh syst/CMakeLists.txt`
-  `rg -n "bits/CacheKey\\.cc|syst/CacheKey\\.cc" -S syst/CMakeLists.txt syst`
-  `find syst -maxdepth 2 -type f | sort`
- smoke checks:
- results:
  - focused `git diff --check` passed for the collapse files
  - the live `syst/` build surface no longer references `syst/bits/CacheKey.cc`
  - `find syst -maxdepth 2 -type f | sort` shows no standalone cache-key
    source file under `syst/`

## Reduction ledger
- files deleted: 1
  - `syst/bits/CacheKey.cc`
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - small negative; one private source file removed

## Decisions
- keep the cache-key helpers in `syst/bits/Detail.hh`
- keep this as a private implementation cleanup only

## Remaining hotspots
- compile verification remains limited by the broken local `build/` tree

## Current milestone
- status: done
- subsystem: `DistributionIO` owns exact cached-payload rebinning
- design rule from `DESIGN.md`: keep cached data-shape transforms close to the
  persistence surface and delete duplicate helper layers

## What changed
- moved exact vector, covariance, and row/bin-major payload rebinning into
  `DistributionIO::Spectrum`
- switched `syst/` cache-read paths to use those `DistributionIO` helpers
- deleted the duplicate `syst/Rebin.cc` layer and its stale declarations from
  `syst/bits/Detail.hh`
- pushed the implementation-only `MatrixRowMajor` alias and central-weight
  branch constant back into the `.cc` files that actually use them
- kept detector-envelope and family-result interpretation in `syst/`

## Why this is simpler
- exact rebinned views of cached payloads now live with the cached payload type
  itself
- `syst/` no longer carries a second rebin implementation for data it already
  reads from `DistributionIO`
- the module boundary is easier to explain: `DistributionIO` reshapes cached
  payloads, `syst/` interprets them
- `syst/bits/Detail.hh` is now a real shared-declarations header instead of a
  place for implementation leftovers

## Verification
- configure/build commands:
- target-only commands:
-  `cmake --build build --target IO Syst --parallel`
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md io/DistributionIO.hh io/DistributionIO.cc syst/Systematics.cc syst/DetectorSystematics.cc syst/ReweightCovariance.cc syst/bits/Detail.hh syst/CMakeLists.txt`
-  `rg -n "build_rebin_matrix|rebin_vector|rebin_detector_templates|rebin_detector_shift_vectors|rebin_shift_vectors|rebin_covariance" -S syst`
- smoke checks:
- results:
  - focused `git diff --check` passed for the move-pass files
  - the old `syst` rebin helper names no longer appear under `syst/`
  - `syst/bits/Detail.hh` no longer exports `Eigen` aliases or the central
    weight branch constant
  - `cmake --build build --target IO Syst --parallel` does not provide a
    trustworthy compile check here because the local build tree is stale and
    reconfigure remains blocked by missing SQLite3 headers and
    `nlohmann/json.hpp`

## Reduction ledger
- files deleted: 1
  - `syst/Rebin.cc`
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - small positive in `io/`, larger negative in `syst/`

## Decisions
- keep exact payload transforms in `DistributionIO`
- keep envelope and family-result semantics in `syst/`

## Remaining hotspots
- trustworthy compile verification still requires a configured local build tree
  with SQLite3 headers and `nlohmann/json.hpp`

## Current milestone
- status: done
- subsystem: covariance export executable rename
- design rule from `DESIGN.md`: keep workflow names short, direct, and easy to
  grep

## What changed
- renamed the covariance export executable and build target to `mk_cov`
- updated the CLI usage and diagnostics in `app/mk_cov.cc`
- updated active docs, install notes, invariants, and the stacked-export smoke
  script to call `mk_cov`
- updated tracking files so the repo no longer carries live references to the
  retired CLI name outside `.git/`

## Why this is simpler
- the CLI now matches the existing `app/mk_cov.cc` entrypoint directly
- `mk_cov` is shorter to type and easier to teach than the older long-form
  name
- the repo no longer has to carry two names for the same export tool

## Verification
- configure/build commands:
- target-only commands:
-  `cmake --build build --target mk_cov --parallel`
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/CMakeLists.txt app/mk_cov.cc COMMANDS INSTALL USAGE INVARIANTS.md tools/systematics-sbnfit-export-smoke.sh`
-  `rg --hidden --glob '!.git' -n "retired covariance export CLI name" -S .`
- smoke checks:
- results:
  - focused `git diff --check` passed for the rename-pass files
  - the hidden-file `rg` sweep returned no remaining references to the retired
    covariance export CLI name outside `.git/`
  - `cmake --build build --target mk_cov --parallel` does not provide a
    trustworthy compile check here because the local `build/` tree is stale and
    reconfigure remains blocked by missing SQLite3 headers and
    `nlohmann/json.hpp`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - near-neutral; target-name and string cleanup only

## Decisions
- keep the smoke script filename unchanged in this pass
- update tracking files too, so the repo contains no remaining live reference
  to the retired CLI name

## Remaining hotspots
- trustworthy compile verification still requires a configured local build tree
  with SQLite3 headers and `nlohmann/json.hpp`

## Current milestone
- status: done
- subsystem: checked-in sample catalog directory rename
- design rule from `DESIGN.md`: reduce naming ambiguity in workflow inputs and
  keep build artifacts distinct from checked-in catalog declarations

## What changed
- renamed the checked-in workflow directory from `samples/` to `cards/`
- updated `tools/render-sample-catalog.sh` defaults to read from `cards/` and
  write `cards/generated/`
- updated active workflow docs and generated include paths to use
  `cards/generated/...`
- left `build/samples/` and internal ROOT paths like `samples/<sample-key>/...`
  unchanged

## Why this is simpler
- the checked-in catalog directory no longer shares the same generic name as
  built sample ROOT outputs
- `cards/` reads as repo-local workflow input, not artifact storage
- the rename is narrow, so downstream persisted layout and build products stay
  stable

## Verification
- configure/build commands:
- target-only commands:
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md tools/render-sample-catalog.sh COMMANDS USAGE cards/README cards/generated/datasets.mk`
-  `rg -n "samples/generated|samples/catalog\\.tsv|samples/datasets\\.tsv" -S COMMANDS USAGE tools/render-sample-catalog.sh cards`
-  `find cards -maxdepth 2 -type f | sort`
- smoke checks:
- results:
  - focused `git diff --check` passed for the rename-pass files
  - the focused `rg` sweep found no remaining active workflow-path references
    in the updated files
  - `find cards -maxdepth 2 -type f | sort` shows the renamed catalog tree

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - near-neutral; mostly path updates plus one directory rename

## Decisions
- keep `samples-dag.mk` unchanged in this pass
- keep `build/samples/` and internal ROOT `samples/...` paths unchanged
- leave historical entries below untouched where they describe earlier
  milestones under the old name

## Remaining hotspots
- the repo still has historical references below to `samples/...` because those
  entries describe earlier milestones

## Current milestone
- status: done
- subsystem: `app/` source filename cleanup
- design rule from `DESIGN.md`: keep workflow entrypoints easy to grep and
  avoid extra naming drift between the CLI and the source tree

## What changed
- renamed the remaining mismatched app entrypoint sources:
  - `app/mk_xsec_fit.cc` -> `app/mk_fit.cc`
  - the covariance export entrypoint source now lives at `app/mk_cov.cc`
- updated `app/CMakeLists.txt` to build `mk_fit` from `mk_fit.cc` and
  `mk_cov` from `mk_cov.cc`
- updated the active source-path reference in `fit/README`

## Why this is simpler
- the `app/` tree now matches the public `mk_fit` name directly
- the fit and covariance entrypoints are easier to find without remembering old
  compatibility filenames
- the remaining longer compatibility name is now confined to the installed
  `mk_cov` executable, not duplicated in the source path as well

## Verification
- configure/build commands:
- target-only commands:
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/CMakeLists.txt app/mk_fit.cc app/mk_cov.cc fit/README`
-  `rg -n "app/mk_xsec_fit\\.cc" -S app fit/README`
- smoke checks:
- results:
  - focused `git diff --check` passed for the rename-pass files
  - the focused `rg` sweep found no remaining active references in `app/` or
    `fit/README`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - near-neutral; pure file rename plus small build/doc updates

## Decisions
- keep executable naming out of this source-file cleanup pass
- leave historical references below untouched where they describe earlier
  milestones

## Remaining hotspots
- executable naming was handled in a later dedicated pass

## Current milestone
- status: done
- subsystem: `syst/` file layout naming cleanup
- design rule from `DESIGN.md`: keep module boundaries sharp, delete generic
  helper buckets, and prefer names that match actual responsibilities

## What changed
- renamed the remaining misleading `syst/` implementation files:
  - `Detector.cc` -> `DetectorSystematics.cc`
  - `UniverseFill.cc` -> `ReweightFill.cc`
  - `UniverseSummary.cc` -> `ReweightCovariance.cc`
- split `Support.cc` into:
  - cache-key helpers, later collapsed into `Detail.hh`
  - `Rebin.cc`
- updated `syst/CMakeLists.txt` so the build surface matches the renamed and
  split translation units

## Why this is simpler
- `syst/` now reads like responsibilities instead of implementation phases
- the old `Support.cc` bucket no longer hides unrelated cache-key and rebinning
  logic behind one generic filename
- the build surface is easier to scan because detector, reweight filling,
  reweight covariance, cache keys, and rebinning are all named directly

## Verification
- configure/build commands:
- target-only commands:
-  `cmake --build build --target Syst mk_fit mk_cov --parallel`
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md syst/CMakeLists.txt syst/DetectorSystematics.cc syst/bits/Detail.hh syst/Rebin.cc syst/ReweightFill.cc syst/ReweightCovariance.cc`
-  `ls syst`
- smoke checks:
- results:
  - focused `git diff --check` passed for the file-layout cleanup files
  - `ls syst` shows the renamed and split translation units in place
  - `cmake --build build --target Syst mk_fit mk_cov --parallel` still
    does not provide a trustworthy compile check here because the current
    `build/` tree points at `/usr/bin/cmake`, which is absent in this
    environment

## Reduction ledger
- files deleted: 1
  - `syst/Support.cc`
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - near-neutral; one generic source file split into two smaller named files

## Decisions
- keep the split minimal and responsibility-based:
  - cache-key helpers live in `Detail.hh`
  - `Rebin.cc` owns rebin-matrix and vector-rebin helpers
- keep the rename local to `syst/` implementation files; public API names stay
  unchanged in this pass

## Remaining hotspots
- trustworthy compile verification still requires a working ROOT-enabled build
  tree
- historical log entries below still mention older filenames where they record
  past milestones

## Current milestone
- status: done
- subsystem: `syst/` naming cleanup
- design rule from `DESIGN.md`: prefer plain data and namespace functions, and
  delete abstractions that do not buy clarity

## What changed
- removed the stale public `SystematicsEngine` wrapper from `syst/`
- renamed adjacent cache nouns:
  - `DistributionIO::Spec` -> `DistributionIO::HistogramSpec`
  - `DistributionIO::Family` -> `DistributionIO::UniverseFamily`
- renamed the internal data result:
  - `SampleComputation` -> `ComputedSample`
- changed systematics diagnostics from `SystematicsEngine:` to `syst:`
- updated downstream `fit/`, export CLI, and one active docs reference to the
  new names

## Why this is simpler
- the `syst::` namespace was already the real public API; the wrapper class was
  pure duplication
- `HistogramSpec` and `UniverseFamily` are easier to grep and understand than
  `Spec` and `Family`
- `ComputedSample` reads like data returned from `compute_sample(...)`, not a
  process object

## Verification
- configure/build commands:
- target-only commands:
-  `cmake --build build --target Syst mk_fit mk_cov --parallel`
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md io/DistributionIO.hh fit/SignalStrengthFit.hh fit/SignalStrengthFit.cc app/mk_cov.cc syst/Systematics.hh syst/Systematics.cc syst/UniverseFill.cc syst/UniverseSummary.cc syst/Support.cc syst/Detector.cc syst/bits/Detail.hh docs/adaptive-binning-plan.md`
-  `rg -n "\\bSampleComputation\\b|\\bDistributionIO::Family\\b|\\bDistributionIO::Spec\\b|\\bclass SystematicsEngine\\b|\\bSystematicsEngine:" -S io syst app fit`
- smoke checks:
- results:
  - the focused `git diff --check` passed for the naming-pass files
  - the focused `rg` sweep returned no remaining code references to the removed names
  - `cmake --build build --target Syst mk_fit mk_cov --parallel` did not provide a trustworthy compile check here because the current `build/` tree still points at `/usr/bin/cmake`, which is absent in this environment

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - `SystematicsEngine`
- shell branches removed: 0
- docs/build artifacts removed:
  - one active stale reference to `DistributionIO::Spec`
- approximate LOC delta:
  - small negative; wrapper deleted and downstream uses updated

## Decisions
- keep `DistributionIO::Spectrum` unchanged in this pass
- allow this small public-surface rename because the user requested it

## Remaining hotspots
- trustworthy compile verification still requires a working ROOT-enabled build
- historical log entries still mention older names where they describe past
  milestones

## Current milestone
- status: done
- subsystem: stacked `mk_cov` smoke coverage
- design rule from `DESIGN.md`: keep verification focused at the workflow edge,
  reuse persisted data surfaces directly, and avoid building a heavier harness
  than the contract requires

## What changed
- added `tools/systematics-sbnfit-export-smoke.sh`
- kept the smoke narrow and app-edge:
  - write synthetic cached `DistributionIO::Spectrum` payloads directly
  - run `mk_cov --manifest` on a good stacked manifest
  - check exported detector, `genie_knobs`, total, and family covariance
    content from the produced ROOT file
  - check explicit rejection on mismatched multisim family metadata
- taught the smoke invocation in `COMMANDS` and `INSTALL`

## Why this is simpler
- one short smoke script is cheaper to maintain than repeated ad hoc ROOT
  inspection whenever the stacked export changes
- writing cached spectra directly avoids rebuilding the full upstream analysis
  chain just to verify one export contract
- the script reuses existing build outputs instead of adding another harness or
  test framework

## Verification
- configure/build commands:
- target-only commands:
-  runtime execution deferred because `ROOT` is not available locally
- shell checks:
-  `bash -n tools/systematics-sbnfit-export-smoke.sh`
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md tools/systematics-sbnfit-export-smoke.sh COMMANDS INSTALL`
- smoke checks:
- results:
  - `bash -n tools/systematics-sbnfit-export-smoke.sh` passed
  - tracked-file `git diff --check` passed for the smoke-coverage touched files
  - runtime execution remains blocked because `ROOT` is not available and
    `root-config` is not on `PATH` in this environment

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - the need to validate the stacked export contract manually with one-off
    ROOT inspection
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - one focused smoke script
  - short workflow-doc mentions for that script

## Decisions
- keep the smoke at the `app/` export edge
- reuse persisted `DistributionIO` payloads directly
- check both successful stacked export and explicit rejection on mismatched
  family metadata

## Remaining hotspots
- trustworthy runtime execution still requires a working local ROOT build tree

## Current milestone
- status: done
- subsystem: explicit stacked multi-process SBNFit export
- design rule from `DESIGN.md`: keep stacked export logic at the `app/` edge,
  reuse persisted covariance-facing payloads directly, and avoid guessing
  cross-process correlations

## What changed
- taught `mk_cov` a manifest-driven stacked export mode at the `app/`
  edge
- kept the single-spectrum export path intact while adding:
  - manifest parsing and stacked nominal assembly
  - a `stack_manifest` tree that records row order, cache selection, and bin
    offsets
  - detector and `genie_knobs` stacked covariance assembly by explicit shared
    source labels
  - stacked multisim family covariance assembly only from retained universes
    with matching branch name and universe count
- updated `COMMANDS`, `USAGE`, and `INSTALL` to teach the new manifest mode
  and strict rejection behavior

## Why this is simpler
- stacked SBNFit export now has one explicit repository-owned contract instead
  of ad hoc post-processing outside the tree
- the exporter reuses persisted detector labels, knob labels, and retained
  universes directly instead of inventing another intermediate format
- rejecting undefined cross-process family correlations is simpler than
  exporting numerically convenient but semantically guessed matrices

## Verification
- configure/build commands:
-  `cmake -S . -B .build/stacked-export -DCMAKE_BUILD_TYPE=Release`
- target-only commands:
-  `cmake --build build --target mk_cov --parallel`
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/mk_cov.cc COMMANDS USAGE INSTALL`
- smoke checks:
- results:
  - tracked-file `git diff --check` passed for the stacked-export touched files
  - `cmake --build build --target mk_cov --parallel` did not provide a
    trustworthy compile check because the existing `build/` tree is stale and
    returned `make: *** No rule to make target 'mk_cov'.  Stop.`
  - fresh configure in `.build/stacked-export` reached dependency discovery and
    then failed because `ROOT` is not available and `root-config` is not on
    `PATH` in this environment

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - the need to hand-assemble stacked SBNFit covariance files outside the repo
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta:
  - one app-edge stacked export mode
  - one focused docs pass for the explicit manifest contract

## Decisions
- make covariance the canonical imported semantic from `hive`
- keep absolute covariance as the canonical persisted form
- keep detector and total envelopes as derived summaries only
- keep `amarantin` as the covariance builder
- treat `weightsPPFX` and `weightsFlux` as one logical flux-family surface with
  branch-dependent resolution
- treat `weightsGenieUp` / `weightsGenieDn` as an optional separate paired
  knob lane, not as extra GENIE multisim universes

## Remaining hotspots
- trustworthy compile/runtime verification still requires a working local ROOT
  build tree

## Current milestone
- status: done
- subsystem: `EventListIO` branch naming + row-wise category API
- design rule from `DESIGN.md`: keep persistence contracts explicit in `io/`
  and keep downstream EventListIO-first consumers aligned with that surface

## What changed
- renamed the canonical persisted EventList truth-category branches to:
  - `__event_category__`
  - `__passes_signal_definition__`
- moved the canonical branch-name ownership onto `EventListIO`
- taught `EventListIO` to expose old and new selected-tree names
  interchangeably through aliases
- renamed the row-wise plot mapper to `PlotEventCategories.hh` /
  `EventCategories`
- renamed plot descriptor option fields from generic `channel` wording to
  `event_category`
- kept `PlotChannels.hh` as a compatibility include alias for older code

## Why this is simpler
- the file format should say `event category` if that is what the code means
- a persisted signal flag should say that it passes the signal definition,
  rather than using a generic `is_signal` name
- EventListIO-first downstream code should not have to guess whether a
  `channel` name means fit channel, event category, or something else
- old/new EventList files can be handled at one persistence boundary instead
  of pushing compatibility branches into every downstream consumer

## Verification
- configure/build commands:
- target-only commands:
-  `cmake --build build --target IO --parallel`
-  `cmake --build build --target Ana --parallel`
-  `cmake --build build --target Plot --parallel`
- shell checks:
-  `git diff --check -- .agent/current_execplan.md docs/minimality-log.md ana/EventListBuild.cc io/EventListIO.hh io/EventListIO.cc io/bits/DERIVED plot/CMakeLists.txt plot/PlotChannels.hh plot/PlotDescriptors.hh plot/PlotEventCategories.hh plot/README plot/StackedHist.cc plot/StackedHist.hh plot/UnstackedHist.cc plot/UnstackedHist.hh`
-  `git diff --no-index --check -- /dev/null plot/PlotEventCategories.hh`
- smoke checks:
-  `build/bin/mk_eventlist --help`
- results:
-  tracked-file `git diff --check` passed for the touched files
-  `plot/PlotEventCategories.hh` produced no whitespace diagnostics under
   `git diff --no-index --check`
-  `cmake --build build --target IO --parallel`,
   `cmake --build build --target Ana --parallel`, and
   `cmake --build build --target Plot --parallel` remain blocked because the
   existing `build/` tree points at `/usr/bin/cmake`
-  `build/bin/mk_eventlist --help` was not attempted because a trustworthy
   current build is not available in this environment

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - local EventList branch-name literals in `ana/EventListBuild.cc`
  - generic plot-side `channel` wording for the row-wise EventList category
    surface
- shell branches removed: 0
- docs/build artifacts removed:
  - stale EventList schema wording in `io/bits/DERIVED`
  - stale row-wise plot README wording that still described generic channels
- approximate LOC delta: moderate schema/consumer rename with one compatibility
  header and EventListIO aliases instead of broader downstream fallback logic

## Decisions
- use `__event_category__` and `__passes_signal_definition__` as the new
  canonical EventList branch names
- keep fit-side `fit::Channel` out of scope
- keep old and new selected-tree column names interoperable at the EventListIO
  boundary
- keep `PlotChannels.hh` as a compatibility include while making
  `PlotEventCategories.hh` canonical

## Remaining hotspots
- build verification still requires either a repaired configured build tree or
  a local ROOT environment

## Current milestone
- status: done
- subsystem: `ana/` signal and event-label naming
- design rule from `DESIGN.md`: keep names direct and explicit so readers do
  not have to guess which kind of “channel” or “signal” a symbol refers to

## What changed
- renamed the hardcoded signal-definition surface to `SignalDefinition`
- renamed the event-level selected-event label surface from `Channels` to
  `EventCategory`
- renamed the installed `ana/` headers and their call sites accordingly:
  - `SignalDefinition.hh`
  - `SignalDefinition.cc`
  - `EventCategory.hh`
- kept fit-side `fit::Channel` unchanged

## Why this is simpler
- `SignalDefinition` reads as a real definition rather than a shorthand alias
- `EventCategory` is more explicit than `Channels` in a codebase that also has
  fit-side channels and other downstream categorisations

## Verification
- configure/build commands:
  - `cmake -S . -B .build/eventcategory-check -DCMAKE_BUILD_TYPE=Release`
- target-only commands:
  - `cmake --build build --target Ana --parallel`
- shell checks:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md ana/CMakeLists.txt ana/README ana/EventCategory.hh ana/EventListBuild.hh ana/EventListBuild.cc io/EventListIO.hh io/EventListIO.cc`
  - `git diff --no-index --check -- /dev/null ana/EventCategory.hh`
  - `git diff --no-index --check -- /dev/null ana/SignalDefinition.hh`
  - `git diff --no-index --check -- /dev/null ana/SignalDefinition.cc`
- smoke checks:
- results:
  - tracked-file `git diff --check` passed
  - the three `git diff --no-index --check` runs produced no whitespace diagnostics for the new files
  - the existing `build/` tree is stale and still points at `/usr/bin/cmake`
  - fresh configure in `.build/eventcategory-check` is blocked by missing ROOT / `root-config`

## Reduction ledger
- files deleted:
  - installed header `ana/Channels.hh` replaced by `ana/EventCategory.hh`
- wrappers removed:
  - the generic `channels` naming at the `ana/` event-label surface
- shell branches removed: 0
- docs/build artifacts removed:
  - stale `ana/README` references to `SignalDef.hh` and `Channels.hh`
- approximate LOC delta: small public-surface naming pass; behavior unchanged

## Decisions
- keep `fit::Channel` unchanged
- rename only the `ana/` event-label surface in this pass

## Remaining hotspots
- fresh build verification still requires a ROOT environment

## Current milestone
- status: done
- subsystem: `ana/` canonical signal definition
- design rule from `DESIGN.md`: add abstractions only when they delete
  complexity; keep workflows in `app/`, and keep analysis-side truth logic in
  `ana/`

## What changed
- added `ana/SignalDef.hh` and `ana/SignalDef.cc` as the one owner of the
  hardcoded Lambda signal definition
- moved the event-level truth predicate and metadata summary string out of
  `ana/Channels.hh` / `ana/EventListBuild.cc` and into `SignalDef`
- removed `BuildConfig.signal_definition` so `mk_eventlist` no longer carries
  a configurable-looking signal-definition seam it does not actually expose
- trimmed `ana/Channels.hh` back to channel classification only
- updated `ana/CMakeLists.txt` and `ana/README` to install and describe the
  new `SignalDef` surface
- kept the current signal behavior, including the truth-vertex-aware checks
  and the persisted `signal_definition` metadata string

## Why this is simpler
- there should be one grep target for the Lambda signal definition
- `mk_eventlist` should use the canonical hardcoded predicate directly instead
  of carrying config it never truly exposes
- `Channels.hh` should not own both channel bookkeeping and the full signal
  predicate

## Verification
- configure/build commands:
  - `cmake -S . -B .build/signaldef-check -DCMAKE_BUILD_TYPE=Release`
- target-only commands:
  - `cmake --build build --target Ana --parallel`
  - `cmake --build build --target mk_eventlist --parallel`
- shell checks:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md ana/CMakeLists.txt ana/README ana/SignalDef.hh ana/SignalDef.cc ana/Channels.hh ana/EventListBuild.hh ana/EventListBuild.cc io/EventListIO.hh io/EventListIO.cc`
  - `git diff --no-index --check -- /dev/null ana/SignalDef.hh`
  - `git diff --no-index --check -- /dev/null ana/SignalDef.cc`
- smoke checks:
- results:
  - tracked-file `git diff --check` passed
  - the two `git diff --no-index --check` runs produced no whitespace diagnostics for the new files
  - the existing `build/` tree is stale and still points at `/usr/bin/cmake`
  - fresh configure in `.build/signaldef-check` is blocked by missing ROOT / `root-config`

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - `BuildConfig.signal_definition`
  - signal-definition ownership from `ana/Channels.hh`
- shell branches removed: 0
- docs/build artifacts removed: stale `ana/README` wording about where the
  signal predicate lives
- approximate LOC delta: about +490 / -180 including the new signal-definition
  files and tracking updates

## Decisions
- create a dedicated `SignalDef` class instead of leaving the signal predicate
  embedded in `Channels.hh`
- keep the signal definition hardcoded rather than exposing new CLI/runtime
  knobs

## Remaining hotspots
- fresh build verification still requires a ROOT environment

## Current milestone
- status: done
- subsystem: `io/` cached-payload naming + sample-build seam
- design rule from `DESIGN.md`: keep `io/` persistence-only and push workflow
  orchestration back out into `app/`

## What changed
- renamed `DistributionIO::Entry` to `DistributionIO::Spectrum` and updated
  the downstream consumers and docs that describe that cached payload surface
- kept the on-disk `DistributionIO` payload layout unchanged; this is an
  in-memory/API noun cleanup, not a file-format rewrite
- removed manifest and `@file` parsing from `io/SampleIO.cc`
- narrowed `SampleIO` to one plain build surface:
  - sample name
  - resolved shard list
  - metadata
  - optional run-db path
- moved the small parsing helpers for:
  - sample manifests
  - legacy `@file` list expansion
  into `app/mk_sample.cc`, where the CLI orchestration already belongs
- updated the local macro `io/macro/mk_sample.C` to call that narrower
  `SampleIO` build surface directly
- updated `io/VISION.md` so it now describes `Spectrum` as current and says
  explicitly that manifest parsing belongs in `app/`

## Why this is simpler
- `DistributionIO::Spectrum` says what the cached object is; `Entry` was too
  vague for a persisted histogram-like payload
- `io/` no longer owns text-file workflow parsing for `mk_sample`
- `SampleIO` is closer to a plain persistence/builder surface and less like a
  second CLI implementation hiding inside the library
- the app now owns the small amount of legacy argument interpretation that
  actually belongs there

## Verification
- local checks:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md io/DistributionIO.hh io/DistributionIO.cc app/mk_xsec_fit.cc plot/EventListPlotting.hh plot/EventListPlotting.cc plot/macro/inspect_dist.C syst/bits/Detail.hh fit/README syst/README docs/adaptive-binning-plan.md io/SampleIO.hh io/SampleIO.cc app/mk_sample.cc io/macro/mk_sample.C io/VISION.md`
  - `bash -n tools/run-macro`
  - `cmake --build build --target IO --parallel`
  - `cmake -S . -B .build/io-spectrum-check -DCMAKE_BUILD_TYPE=Release`
- results:
  - stale `DistributionIO::Entry` references were removed from the touched
    code and docs
  - stale `SampleIO` manifest / `@file` parser APIs were removed from `io/`
  - `git diff --check` passed on the touched tracked files
  - `bash -n tools/run-macro` passed
  - the existing `build/` tree still points at `/usr/bin/cmake`
  - a fresh configure is still blocked on missing ROOT / `root-config`

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - the text-file parsing layer from `SampleIO`
- shell branches removed: 0
- docs/build artifacts removed:
  - stale `DistributionIO::Entry` wording in current docs

## Decisions
- prefer `Spectrum` as the direct noun for one cached `DistributionIO`
  payload
- keep the CLI behavior but move its parsing seam into `app/mk_sample`
- avoid widening this pass into a persisted layout or cache-key redesign

## Current milestone
- status: done
- subsystem: `io/` naming convergence
- design rule from `DESIGN.md`: keep names direct, keep current docs current,
  and remove wrapper ceremony when it does not buy real clarity

## What changed
- renamed the public shard-edge helper from `InputPartitionIO` to `ShardIO`
  and installed `io/ShardIO.hh` instead of the old header
- cleaned up the matching shard-facing names in `io/ShardIO.cc` and
  `io/SampleIO.*`:
  - `list_path`
  - `files`
  - `subruns`
  - `entries`
  - `shards_`
- renamed `kEnriched` to `kSignal` in `SampleIO`, `DatasetIO`, and the
  downstream call sites that branch on sample origin
- kept backward reads for older persisted files by accepting both
  `"signal"` and `"enriched"` on input while writing `"signal"` going
  forward
- trimmed the small `DatasetIO.cc` redundancies identified in review:
  - removed a redundant read-mode assignment
  - removed a dead null check
  - removed a destructor reset that did not matter at object teardown
  - inlined a few one-use wrapper locals
- updated current docs/install surfaces so they describe `ShardIO` and
  `signal` rather than stale historical names

## Why this is simpler
- the public type name now matches the role of the object instead of carrying
  historical partition language
- the workflow language is more direct with `signal` than `enriched`
- current docs and installed headers now describe the code that actually
  exists
- `DatasetIO.cc` keeps the same persistence work with a little less wrapper
  ceremony

## Verification
- local checks:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md io/CMakeLists.txt io/ShardIO.hh io/ShardIO.cc io/SampleIO.hh io/SampleIO.cc io/DatasetIO.hh io/DatasetIO.cc io/macro/print_sample.C io/README io/VISION.md VISION.md INSTALL USAGE docs/repo-internals.puml io/bits/DERIVED ana/EventListBuild.cc plot/PlottingHelper.cc`
  - `bash -n tools/run-macro`
  - `cmake --build build --target IO --parallel`
  - `cmake -S . -B .build/io-rename-check -DCMAKE_BUILD_TYPE=Release`
- results:
  - current-surface `InputPartitionIO` / `kEnriched` references were removed
    from the touched code and docs
  - backward-read support for old `"enriched"` payloads was preserved in the
    enum parsers
  - `git diff --check` passed on the touched tracked files
  - `bash -n tools/run-macro` passed
  - the existing `build/` tree is stale and still references `/usr/bin/cmake`
  - a fresh configure is still blocked on missing ROOT / `root-config`

## Reduction ledger
- files deleted: 1 installed public header path renamed out of the tree
- wrappers removed:
  - old partition naming at the shard-edge public surface
- shell branches removed: 0
- docs/build artifacts removed:
  - stale current-surface `InputPartitionIO` / `enriched` wording

## Decisions
- rename the public shard helper directly to `ShardIO` instead of keeping an
  `Input*` prefix
- preserve backward reads for old origin strings rather than forcing an
  immediate file-format break
- keep persisted `part/partition_*` subdirectories in `SampleIO` unchanged in
  this pass to avoid widening into a compatibility rewrite

## Current milestone
- status: done
- subsystem: `io/` compatibility-layer removal
- design rule from `DESIGN.md`: keep `io/` persistence-only and delete stale
  compatibility scaffolding once the direct path exists

## What changed
- deleted `io/ChannelIO.hh` and `io/ChannelIO.cc`
- deleted the last local helper macros that still depended on that layer:
  - `fit/macro/fit_channel.C`
  - `fit/macro/scan_mu.C`
  - `plot/macro/plot_channel.C`
- removed `ChannelIO` from the `IO` library source/header lists
- updated current docs so they no longer describe `ChannelIO` or `mk_channel`
  as live workflow surfaces
- rewrote `docs/adaptive-binning-plan.md` around a `DistributionIO`-first,
  downstream-assembly path

## Why this is simpler
- there is one fewer persisted surface to explain, maintain, and keep in sync
- the tree no longer carries a compatibility layer whose only live users were
  local macros
- current docs now point at the direct downstream path instead of a deleted
  adapter format

## Verification
- shell checks:
  - `git diff --check -- io/CMakeLists.txt INSTALL COMMANDS io/README io/VISION.md docs/repo-internals.puml docs/adaptive-binning-plan.md .agent/current_execplan.md docs/minimality-log.md`
- smoke checks:
  - none run for ROOT macros; the dependent macros were deleted in this pass

## Reduction ledger
- files deleted: 5
- wrappers removed: 1 persisted compatibility layer
- docs/build artifacts removed: `ChannelIO` from current module/build docs

## Decisions
- remove `ChannelIO` outright instead of keeping a no-longer-used compatibility
  surface
- prefer direct downstream assembly from `DistributionIO` over a second
  persisted bundle format

## Current milestone
- status: done
- subsystem: `io/` correctness guards
- design rule from `DESIGN.md`: keep `io/` on persistence contracts and fail
  early when persisted inputs are incomplete or inconsistent

## What changed
- hardened shard subrun scanning in `io/ShardIO.cc`:
  - every file must expose a `SubRun` tree
  - one sample list may not mix `nuselection/SubRun` and `SubRun`
  - malformed lists now fail before `TChain` can silently skip files
- hardened run-database lookup in `io/SampleIO.cc`:
  - partial `(run, subrun)` coverage now throws
  - the old silent zero-weight fallback for missing rows is gone
- guarded `DatasetIO` sample overwrite readback in `io/DatasetIO.cc`:
  - persist `provenance_count`
  - prefer that count on read so stale `prov/pNNNN` directories are ignored
  - keep the old directory-enumeration fallback for older files
- extended `io/ChannelIO.cc` so `DistributionIO::Family::universe_histograms`
  survives write/read round-trips, while keeping backward-compatible reads
  for older payloads that do not have those branches

## Why this is simpler
- malformed sample lists now fail at the persistence boundary instead of
  showing up later as missing exposure or dropped events
- run-database handling now has two explicit modes:
  - fully covered `run_subrun_pot`
  - all-missing `unit`
  instead of an ambiguous partial-coverage state
- one persisted provenance-count guard is smaller than trying to clean up old
  ROOT directories during every overwrite
- `ChannelIO` now matches the full `DistributionIO::Family` payload instead of
  maintaining a silent partial copy

## Verification
- configure/build commands:
  - `cmake -S . -B .build/io-guards-check -DCMAKE_BUILD_TYPE=Release`
- target-only commands:
  - not run; configure is blocked on missing ROOT
- shell checks:
  - `bash -n tools/run-macro`
- smoke checks:
  - none added in-tree for this path
- results:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md io/ShardIO.cc io/SampleIO.cc io/DatasetIO.cc io/ChannelIO.cc` passed
  - `bash -n tools/run-macro` passed
  - fresh configure failed because this host has no ROOT environment and no
    `root-config` on `PATH`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: roughly +110 / -13 in the code touched for the fixes

## Decisions
- prefer explicit runtime errors over silent zero-weight fallbacks for partial
  persisted normalisation data
- preserve backward-readable ROOT payloads by making the new provenance-count
  and universe-histogram handling additive

## Remaining hotspots
- host-side build verification still depends on a ROOT environment that is not
  available in this session

## Current milestone
- status: done
- subsystem: `tools/` + top-level workflow docs
- design rule from `DESIGN.md`: delete stale scaffolding after feature work,
  keep workflows direct, and reduce wrapper ceremony when the native path is
  already proven

## What changed
- simplified `tools/run-macro`:
  - removed runtime C++ signature scraping
  - kept the same invocation shape
  - kept explicit override prefixes
  - switched to simple literal inference for bool / int / float / string
- removed stale workflow teaching from top-level docs:
  - dropped the old `write_channel` macro example from `COMMANDS`
  - dropped the old `cache_systematics.C` direct ROOT example from `COMMANDS`
  - removed the old keyed `mk_dataset` workflow section as a normal
    “typical” dataset path in `USAGE`
  - fixed the stale positional `mk_eventlist` hand-build example in `USAGE`
  - updated Docker workflow docs so the native path reaches `mk_dist`
- kept older compatibility entrypoints available where they still have
  migration value, but stopped documenting them as the preferred workflow

## Why this is simpler
- `tools/run-macro` no longer depends on parsing macro source just to decide
  how to quote arguments
- the top-level docs now tell one consistent native workflow instead of mixing
  the current path with older compatibility examples in the main teaching flow
- obsolete macro-wrapper examples are no longer competing with the thin CLI
  entrypoints that replaced them

## Verification
- local checks:
  - `git diff --check -- tools/run-macro COMMANDS USAGE INSTALL .agent/current_execplan.md docs/minimality-log.md`
  - `bash -n tools/run-macro`
- results:
  - the macro runner keeps the same documented command shape with less shell
    branching
  - stale legacy-first workflow wording was removed from the main docs

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - remove the runtime signature-scraping layer from `tools/run-macro`
- shell branches removed:
  - per-parameter kind extraction from macro source in `tools/run-macro`
- docs/build artifacts removed:
  - stale keyed-dataset and positional-eventlist examples from the main
    workflow docs
  - stale macro-wrapper examples from `COMMANDS`
- approximate LOC delta: smaller macro wrapper plus doc cleanup

## Decisions
- keep `cache_systematics.C` and `write_channel.C` for now as ad hoc ROOT
  helpers, but stop teaching them as the normal scripted path
- avoid removing any installed targets, public headers, or still-documented
  compatibility CLIs in this cleanup pass

## Remaining hotspots
- `mk_eventlist --cache-*` still exists as a documented compatibility bridge
- `mk_channel` / `ChannelIO` still remain as the current fit bridge on top of
  `DistributionIO`

## Current milestone
- status: done
- subsystem: `plot/` + `fit/` downstream teaching seam
- design rule from `DESIGN.md`: prefer the flatter persisted downstream
  surface, keep workflows in `app/`, and describe compatibility wrappers as
  wrappers rather than the target data flow

## What changed
- rewrote downstream docs so the preferred final-analysis path is now stated as:
  - `mk_eventlist`
  - `mk_dist`
  - `DistributionIO`
- reframed `mk_channel` and `mk_xsec_fit` as the current compatibility bridge
  on top of cached `DistributionIO` entries instead of the target end state
- kept the additive `mk_channel --manifest` path and observed-data provenance
  support on `ChannelIO`, so one channel can now be assembled from many cached
  processes without hand-entered observed bins
- extended `mk_xsec_fit` reports with:
  - `distribution_path`
  - `channel_build_version`
  - observed-data source keys
- updated `plot/README`, `fit/README`, `COMMANDS`, and `USAGE` so the docs now
  match the intended `VISION.md` teaching path

## Why this is simpler
- the normal downstream story now has one clear persisted final surface:
  `DistributionIO`
- `ChannelIO` is described honestly as a smaller compatibility bundle for the
  current fit path, which removes the old doc split between the preferred
  workflow and the documented workflow
- manifest-driven channel assembly deletes ad hoc hand-entered observed-bin
  bookkeeping when the observed spectrum already exists as cached data bins
- the fit report now exposes where the channel came from, so provenance no
  longer requires reopening the ROOT file just to see the `DistributionIO`
  source

## Verification
- local checks:
  - `git diff --check -- plot/README fit/README COMMANDS USAGE io/ChannelIO.hh io/ChannelIO.cc fit/SignalStrengthFit.hh app/mk_channel.cc app/mk_xsec_fit.cc .agent/current_execplan.md docs/minimality-log.md`
- Docker checks:
  - focused Linux rebuild of `IO`, `Fit`, `mk_channel`, and `mk_xsec_fit`
  - `mk_channel --help` usage smoke
  - `mk_xsec_fit --help` usage smoke
  - synthetic manifest-channel smoke:
    - write a small `DistributionIO` file
    - run `mk_channel --manifest`
    - verify observed-data provenance survives on `ChannelIO`
  - synthetic legacy-channel smoke:
    - run the positional `mk_channel` path on the same cache input
  - synthetic fit smoke:
    - run `mk_xsec_fit` on the manifest-built channel
    - verify the report includes `distribution_path` and observed source keys
- results:
  - downstream docs now teach `DistributionIO` as the normal final surface
  - current `mk_channel` / `mk_xsec_fit` compatibility tooling still works on
    top of cached `DistributionIO` entries

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - remove the need to describe `ChannelIO` as the preferred downstream
    analysis surface
- shell branches removed: 0
- docs/build artifacts removed: stale EventListIO-first / channel-first
  downstream wording in plot/fit docs
- approximate LOC delta: small provenance/report additions plus doc rewrites in
  exchange for deleting the old mixed downstream story

## Decisions
- keep `ChannelIO` and `mk_channel` as the current fit bridge until a later
  milestone can delete them without leaving the native fit path orphaned
- build on top of the existing additive `mk_channel --manifest` and
  `ChannelIO::data_source_keys` work instead of rewriting that seam again in
  this pass

## Remaining hotspots
- `plot/` still has older row-wise stack / unstack helpers that open
  `EventListIO` directly
- `mk_xsec_fit` still reads `ChannelIO` rather than `DistributionIO` directly,
  so the last compatibility layer is not deleted yet

## Current milestone
- status: done
- subsystem: `app/` cache-build seam
- design rule from `DESIGN.md`: keep workflows in `app/`, keep `syst/` focused
  on cache construction rather than CLI orchestration, and prefer flatter
  explicit workflow steps over one overloaded app

## What changed
- added a dedicated one-request cache builder:
  - `mk_dist`
  - reads one `EventListIO`
  - writes one `DistributionIO` request
- rewrote the preferred workflow docs so the normal path is now:
  - `mk_eventlist`
  - `mk_dist`
  - downstream channel / fit steps
- kept `mk_eventlist --cache-*` as an explicit legacy compatibility bridge
  instead of silently treating it as the preferred cache path
- updated `mk_eventlist --help` to show:
  - the normal row-wise event-list form
  - the separate legacy compatibility form
  - an explicit `mk_eventlist -> mk_dist` preference note
- updated `COMMANDS`, `USAGE`, and `INSTALL` so `mk_dist` is listed as a built
  and installed app target

## Why this is simpler
- cache construction now has one honest app boundary instead of hiding inside
  the event-list builder
- `mk_eventlist` can read as the row-wise build step again, while `mk_dist`
  reads as the bin-wise cache step
- the preferred workflow is flatter and easier to grep because the two stages
  now have separate names and separate CLIs
- this pass stayed out of the unrelated dirty `syst/` files because the
  existing cache API was already sufficient for the thinner workflow split

## Verification
- local checks:
  - `git diff --check -- app/CMakeLists.txt app/mk_dist.cc app/mk_eventlist.cc COMMANDS USAGE INSTALL`
- Docker checks:
  - focused Linux rebuild of `Syst`, `mk_eventlist`, and `mk_dist`
  - `mk_eventlist --help` usage smoke
  - `mk_dist --help` usage smoke
  - synthetic direct-cache smoke:
    - build event list
    - run `mk_dist`
    - verify the produced `DistributionIO` metadata and payload
  - synthetic legacy-bridge smoke:
    - run `mk_eventlist --cache-*`
    - verify the produced `DistributionIO` metadata and payload
    - verify the explicit legacy warning
- results:
  - `mk_dist` now owns the preferred one-request `DistributionIO` cache path
  - the legacy `mk_eventlist --cache-*` path still works, but now says it is a
    compatibility bridge

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - remove the need to treat `mk_eventlist --cache-*` as the preferred
    persistent-cache entrypoint
- shell branches removed: 0
- docs/build artifacts removed: stale `mk_eventlist`-first cache workflow
  wording in top-level docs
- approximate LOC delta: one thin app plus smaller event-list responsibility
  framing in exchange for making the cache step explicit

## Decisions
- keep `mk_eventlist --cache-*` for now as a documented compatibility bridge
  instead of deleting it in the same pass
- avoid touching `syst/Systematics.*`, `DistributionIO.*`, or `syst/README`
  in this milestone because the current library seam already supported
  `mk_dist` and those files had unrelated in-flight edits

## Remaining hotspots
- `mk_eventlist` still contains the legacy `--cache-*` parser and bridge path
- `mk_dist` currently supports one request at a time; batch request manifests
  are still a later step

## Current milestone
- status: done
- subsystem: `ana/` + `io/` event-list weighting seam
- design rule from `DESIGN.md`: keep event-list construction in `ana/`, keep
  persistence in `io/`, and make downstream row-wise surfaces explicit rather
  than reconstructing old workflow assumptions later

## What changed
- moved nominal event weighting in `ana::build_event_list(...)` off the old
  sample-wide scalar shortcut and onto the embedded DatasetIO run/subrun
  normalization table
- `mk_eventlist` now requires event-tree `run` and `subRun` branches and
  fails if a selected event has no matching run/subrun normalization entry
- split the persisted event-weight surface into:
  - `__w_norm__`
  - `__w_cv__`
  - `__w__`
  - `__w2__`
- copied the run/subrun normalization table through `EventListIO` so the
  row-wise debug surface can inspect the same normalization metadata later
- updated `io/bits/DERIVED`, `ana/README`, `COMMANDS`, and `USAGE` so the
  documented event-list contract matches the persisted surface

## Why this is simpler
- one normalization source now exists for row-wise event weighting:
  the DatasetIO run/subrun table already built upstream
- downstream code no longer needs to guess whether `__w__` came from a local
  run/subrun map or a sample-level scalar
- the selected tree exposes the normalization split directly, so debugging
  does not need another wrapper or a second reconstruction path
- `EventListIO` now carries the same normalization metadata that `ana`
  consumed, which keeps the build/debug surface honest

## Verification
- local checks:
  - `git diff --check -- ana/EventListBuild.cc io/EventListIO.cc io/bits/DERIVED ana/README COMMANDS USAGE`
  - `bash -n tools/run-macro`
- Docker checks:
  - focused Linux rebuild of `IO`, `Ana`, and `mk_eventlist`
  - `mk_eventlist --help` usage smoke
  - synthetic success smoke confirming expected `__w_norm__`, `__w_cv__`,
    `__w__`, and `__w2__` values
  - synthetic failure smoke for missing `run` / `subRun`
  - synthetic failure smoke for missing run/subrun lookup entries
- results:
  - `mk_eventlist` now resolves nominal event weights from the embedded
    run/subrun map for non-data samples
  - data remains unit-weighted so downstream data yields stay raw counts
  - copied `EventListIO` sample metadata now retains the run/subrun
    normalization table used during the build step

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - remove the last sample-wide-scalar shortcut from the canonical
    `DatasetIO -> EventListIO` build path
- shell branches removed: 0
- docs/build artifacts removed: stale scalar-weight wording in the EventListIO
  derived-column contract
- approximate LOC delta: additive validation and explicit weight-surface
  columns in exchange for deleting the old implicit scalar assumption

## Decisions
- keep data unit-weighted in `__w__` so existing row-wise and downstream data
  yields stay count-like even though the run/subrun normalization table is
  still required and copied through the build
- persist the run/subrun normalization table directly in `EventListIO` sample
  storage instead of inventing a second debug-only metadata format

## Remaining hotspots
- `mk_eventlist` still owns the legacy `--cache-*` path, so the preferred
  cache-building workflow is not yet split cleanly into `mk_dist`
- `EventListIO` sample metadata is still a partial copy of `DatasetIO::Sample`
  rather than one obviously shared persistence helper

## Current milestone
- status: done
- subsystem: `app/` + generated dataset workflow seam
- design rule from `DESIGN.md`: keep workflows in `app/`, keep logical sample
  fan-in out of downstream seams, and make scope inputs explicit instead of
  implicit string conventions

## What changed
- added a native scoped `mk_dataset` CLI:
  - `--run RUN --beam BEAM --polarity POLARITY --manifest DATASET.manifest`
  - optional `--campaign NAME`
  - legacy context-string mode kept as a compatibility bridge
- made the native dataset manifest path logical-only:
  - one `sample sample-root-path` row per logical sample
  - duplicate sample rows now fail in the native path
  - native inputs must already carry the matching persisted logical sample
    identity
- added scope validation:
  - sample ROOT beam / polarity must match the requested dataset scope
  - optional sample-definition campaign and inferred run hints must not
    conflict with the requested scope
- taught `tools/render-sample-catalog.sh` to emit dataset scope variables:
  - `dataset_run.*`
  - `dataset_beam.*`
  - `dataset_polarity.*`
  - optional `dataset_campaign.*`
- taught `samples-dag.mk` to call the native `mk_dataset` path whenever those
  explicit dataset-scope variables are present
- updated `COMMANDS`, `USAGE`, and `samples/README` so the preferred dataset
  path matches the generated workflow

## Why this is simpler
- the preferred dataset path is now honest about its scope inputs instead of
  hiding them inside one free-form context string
- `mk_dataset` no longer has to be the first place where shard fan-in becomes
  logical-sample identity in the native path
- generated sample workflows now read as three flat stages:
  - shard list generation
  - logical sample building
  - explicit-scope dataset assembly
- scope checks only apply to the samples actually present, so partial datasets
  still work without inventing one fixed universal sample set

## Verification
- shell checks:
  - `bash -n tools/render-sample-catalog.sh`
  - `bash tools/render-sample-catalog.sh`
  - `make -Bn -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk datasets`
- smoke checks:
  - Docker rebuild plus focused `mk_dataset` build
  - native scope success smoke on a synthetic logical sample root
  - legacy compatibility smoke on the same logical sample root
  - duplicate dataset manifest failure smoke
  - run-scope mismatch failure smoke
  - compiled verifier for the produced dataset ROOT file
- results:
  - generated dataset scope variables were refreshed under
    `samples/generated/datasets.mk`
  - the generated sample DAG now emits native scoped `mk_dataset` invocations
  - Docker verification passed with the expected native usage, success-path
    smokes, and failure-path smokes

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - remove the need for generated workflows to treat repeated-key dataset
    merging as the preferred path to logical sample assembly
- shell branches removed: 0
- docs/build artifacts removed: stale dataset-context-first workflow wording
- approximate LOC delta: additive scope parsing and validation in exchange for
  a sharper logical-only dataset path

## Decisions
- derive the persisted dataset context string from the explicit native dataset
  scope until a later milestone gives `DatasetIO` a dedicated top-level scope
  payload
- keep repeated-key merge behavior only in the legacy context-string bridge
- validate dataset run hints only when sample definitions expose an obvious
  run token, so partial datasets are not forced through one universal sample
  inventory

## Remaining hotspots
- `DatasetIO` still stores scope indirectly as one context string rather than a
  dedicated top-level run / beam / polarity / campaign payload
- `mk_eventlist` still uses the old scalar weighting shortcut instead of the
  persisted run/subrun normalization table

## Current milestone
- status: done
- subsystem: `app/` + `io/` sample build seam + sample workflow shell
- design rule from `DESIGN.md`: keep workflows in `app/`, keep plain-text
  inputs explicit, and keep logical sample grouping out of downstream wrapper
  paths

## What changed
- added a native logical-sample CLI in `mk_sample`:
  - `--sample NAME --manifest SAMPLE.manifest`
  - manifest rows are explicit `shard sample-list-path`
- kept the older positional single-list path as a documented transitional
  bridge and made the older list-of-lists bridge explicit as `@path`
- taught `tools/render-sample-catalog.sh` to emit:
  - one `*.sample.manifest` per logical sample
  - one logical sample root per downstream sample key
  - a generated dataset manifest that points at already-logical sample roots
- taught `samples-dag.mk` to:
  - keep artifact `.list` generation as the shard-facing layer
  - build logical sample roots from generated sample manifests when
    `logical_samples.*` is present
  - preserve the old `artifact:subdir` include shape for legacy `datasets.mk`
- updated `COMMANDS`, `USAGE`, `INSTALL`, and `samples/README` so the
  preferred sample-building story matches the new native path

## Why this is simpler
- shard membership is now a first-class plain-text input instead of something
  later workflow stages have to reconstruct indirectly
- generated sample workflows now stop at one logical `SampleIO` root per
  downstream key, which removes one layer of accidental shard naming from the
  build graph
- `samples-dag.mk` has a flatter split:
  - artifact list generation
  - logical sample root construction
  - optional dataset assembly
- the compatibility story is sharper:
  - native logical sample path for new work
  - older single-list path only as a bridge

## Verification
- target-only commands:
  - `make -f samples-dag.mk print-samples`
  - `make -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk print-samples`
  - `make -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk print-datasets`
- shell checks:
  - `bash -n tools/render-sample-catalog.sh`
  - `bash tools/render-sample-catalog.sh`
  - `make -n -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk samples`
- smoke checks:
  - Docker rebuild plus focused `mk_sample` build
  - native manifest smoke on a synthetic two-shard sample
  - legacy single-list smoke on the same synthetic input
  - duplicate-shard and malformed-manifest failure smokes
  - compiled verifier for the produced logical sample ROOT file
- results:
  - generated sample manifests and generated logical dataset manifests were
    refreshed under `samples/generated/`
  - the generated sample DAG now invokes `mk_sample --sample ... --manifest`
    for logical sample outputs
  - Docker verification passed with the expected native usage, success-path
    smokes, and failure-path smokes
  - an interactive ROOT verification attempt was noisy in this environment
    because the container session picks up an unrelated `/work/build/lib`
    startup search path; a compiled verifier was used instead and passed

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - remove the need for generated workflows to rely on `mk_dataset` repeated
    keys as the first place where shard membership becomes logical-sample
    identity
- shell branches removed: 0
- docs/build artifacts removed: stale generated sample workflow wording
- approximate LOC delta: additive manifest plumbing in exchange for a clearer
  one-logical-sample-per-output workflow

## Decisions
- keep artifact `.list` generation as the shard-facing layer, but make logical
  sample membership explicit through generated sample manifests
- keep the positional single-list `mk_sample` mode for now, but document it as
  transitional
- keep the older list-of-lists bridge only behind explicit `@path` syntax

## Remaining hotspots
- `mk_dataset` still accepts the older repeated-key shard-merge story and
  should move toward an explicit logical-only native scope in the next
  milestone
- `mk_eventlist` still uses the old scalar weighting shortcut instead of the
  persisted run/subrun normalization table

## Current milestone
- status: done
- subsystem: `io/` + legacy dataset assembly seam
- design rule from `DESIGN.md`: keep `io/` persistence-only, prefer plain
  data, and fix the data contract before widening the workflow surface

## What changed
- added explicit logical-sample normalization payloads to the persistence
  layer:
  - per-shard generated exposure by `(run,subrun)` in `InputPartitionIO`
  - per-sample aggregated run/subrun normalization entries in `SampleIO`
  - carried-forward logical normalization entries in `DatasetIO`
- added provenance fields needed for the later manifest-native path:
  - `sample`
  - `normalisation_mode`
  - `sample_list_path`
  - optional `shard`
- taught the legacy repeated-key `mk_dataset` merge path to preserve and
  combine the new run/subrun normalization entries instead of silently
  dropping them
- updated `EventListIO` sample metadata and the dataset print macro to surface
  the new fields for inspection

## Why this is simpler
- the logical normalization contract now lives with the persisted sample and
  dataset objects instead of being reconstructible only as one scalar
- shard provenance is sharper: the source sample-list path and generated
  exposures now travel with the persisted record
- later milestones can move `mk_eventlist` and `mk_dist` onto an explicit
  run/subrun map instead of inventing another side channel

## Verification
- configure/build commands:
- target-only commands:
- `cmake --build build --target IO mk_sample mk_dataset --parallel`
- shell checks:
  - `git diff --check -- io/bits/RunDatabaseService.hh io/bits/RunDatabaseService.cc io/InputPartitionIO.hh io/InputPartitionIO.cc io/DatasetIO.hh io/DatasetIO.cc io/SampleIO.hh io/SampleIO.cc io/EventListIO.cc app/mk_dataset.cc io/macro/print_dataset.C io/README .agent/current_execplan.md docs/minimality-log.md`
- smoke checks:
- `build/bin/mk_sample --help`
- `build/bin/mk_dataset --help || true`
- results:
  - `git diff --check` passed
  - host-side reuse of the checked-in build trees is invalid here:
    - the generated `build/` makefiles reference `/usr/bin/cmake`
    - the checked-in `build/bin/*` and `build-host/bin/*` binaries are Linux
      ELF executables and cannot run on this macOS host
  - Docker verification passed in a fresh Linux build tree:
    - `docker build -t amarantin-dev .`
    - configured `cmake -S . -B .build/m2-docker -DCMAKE_BUILD_TYPE=Release`
    - built `IO`, `mk_sample`, and `mk_dataset`
    - `mk_sample --help` printed the expected usage
    - `mk_dataset --help` printed the current usage text and then followed its
      existing invalid-arguments path

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - remove the need for later workflow steps to reconstruct logical
    normalization only from a sample-wide scalar
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: additive persistence fields and merge plumbing in
  exchange for a clearer normalization contract

## Decisions
- keep `normalisation` as a summary scalar for compatibility, but stop
  treating it as the full logical-sample contract
- store missing per-run target exposure explicitly as zero in the new table so
  later milestones can fail loudly instead of hiding the gap
- preserve the legacy repeated-key `mk_dataset` workflow for now, but make it
  carry the new normalization surface forward

## Remaining hotspots
- native `mk_sample --sample --manifest` still needs to populate the new
  logical sample identity cleanly
- `mk_eventlist` still uses the old scalar weight shortcut and needs to move
  onto the embedded run/subrun normalization map
- the checked-in host build trees remain stale/mixed and should not be reused
  as native macOS verification trees

## Current milestone
- status: blocked
- subsystem: `app/` + `io/` downstream channel workflow
- design rule from `DESIGN.md`: keep workflows in `app/`, keep module
  boundaries sharp, and prefer one plain-text native path over extra wrapper
  layers

## What changed
- started a native channel-assembly pass so final-region building can move
  beyond the current one-signal / one-background bridge
- chosen implementation direction:
  - preserve the old positional `mk_channel` mode
  - add one additive plain-text manifest mode for many
    signal/background/data inputs
  - fill observed data automatically from persisted cached inputs
  - persist a little more channel provenance instead of relying on external
    command history

## Why this is simpler
- the downstream ladder becomes more honest: cached bins can now assemble into
  a final region natively instead of depending on hand-entered observation bins
- one manifest is easier to grep and review than growing more special-purpose
  CLI flags
- final-region provenance moves with `ChannelIO` instead of living only in the
  shell history

## Verification
- configure/build commands:
  - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- target-only commands:
  - `cmake --build build --target mk_channel --parallel`
- shell checks:
  - `git diff --check -- app/mk_channel.cc io/ChannelIO.hh io/ChannelIO.cc COMMANDS USAGE .agent/current_execplan.md docs/minimality-log.md`
- smoke checks:
  - fallback syntax-only compile attempt for `app/mk_channel.cc`
- results:
  - `git diff --check` passed
  - focused build verification is blocked in the local environment:
    - the existing `build/` tree does not define `mk_channel`
    - rerunning CMake fails because local SQLite3 and Eigen package discovery
      is broken here
    - the default `c++` driver in this shell cannot find standard library
      headers, so the syntax-only fallback is not usable either

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - remove the need to hand-enter observed bins for the normal multi-process
    manifest path
- shell branches removed: pending
- docs/build artifacts removed: 0
- approximate LOC delta: modest additive native channel-assembly support plus
  additive observed-data provenance

## Current milestone
- status: blocked
- subsystem: `syst/` + `io/` systematics-cache seam
- design rule from `DESIGN.md`: keep module boundaries sharp, prefer plain
  data plus namespace functions, and keep the internal split flat

## What changed
- made `DistributionIO` persist retained universe histograms for `genie`,
  `flux`, and `reint` families
- made persistent cache reuse validate file metadata against the current
  `EventListIO` path and systematics cache schema version
- stopped rebinned loads from silently linearly rebinning sigma-only family
  payloads across a different binning
- documented one canonical detector-plus-reweighting cache workflow in:
  - `syst/README`
  - `COMMANDS`
  - `USAGE`
- added focused smoke entry points:
  - `tools/systematics-detector-smoke.sh`
  - `tools/systematics-reweight-smoke.sh`

## Why this is simpler
- a cache file now either belongs to the current event list or it is rejected;
  there is no silent relabeling path
- retained universe histograms are now a real public option instead of a dead
  flag
- the docs now describe the same two-lane detector-plus-reweighting story that
  the code implements
- smoke entry points make the two main systematics lanes easier to verify
  directly without inventing a second test harness abstraction

## Verification
- configure/build commands:
  - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- target-only commands:
  - `cmake --build build --target Syst mk_eventlist --parallel`
- shell checks:
  - `bash -n tools/systematics-detector-smoke.sh`
  - `bash -n tools/systematics-reweight-smoke.sh`
  - `git diff --check -- io/DistributionIO.hh io/DistributionIO.cc syst/CMakeLists.txt syst/Systematics.cc syst/Detector.cc syst/Support.cc syst/UniverseFill.cc syst/UniverseSummary.cc syst/bits/Detail.hh syst/README COMMANDS USAGE tools/systematics-detector-smoke.sh tools/systematics-reweight-smoke.sh .agent/current_execplan.md docs/minimality-log.md`
- smoke checks:
  - added detector-lane smoke script
  - added reweighting-lane smoke script with persistent-cache metadata
    mismatch coverage
- results:
  - `git diff --check` passed
  - both new smoke scripts passed `bash -n`
  - focused rebuild verification is still blocked locally:
    - `cmake --build build --target Syst mk_eventlist --parallel` fails because
      the generated `build/` makefiles reference `/usr/bin/cmake`
    - rerunning `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` fails because
      local `SQLite3` and `Eigen3` discovery are unavailable here
    - the smoke scripts were not executed because `root-config` is not on PATH

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - remove the silent "metadata relabel then reuse stale cache" path from
    persistent cache evaluation
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: additive cache-safety checks, payload plumbing, and
  direct smoke scripts

## Decisions
- reject non-empty persistent cache files when their metadata does not match
  the current event list instead of trying to partially relabel them
- treat retained universe histograms as a real persisted payload rather than a
  no-op option
- fail loudly when a cached sigma-only family would need a mathematically
  invalid rebin

## Remaining hotspots
- run the new smoke scripts in an environment with `root-config` plus a valid
  rebuilt `Syst` target
- decide later whether the shorter private filenames in `syst/` should stay as
  `Detector.cc` / `Support.cc` / `UniverseFill.cc` / `UniverseSummary.cc` or
  be renamed once the worktree settles

## Current milestone
- status: done
- subsystem: `fit/` + `app/`
- design rule from `DESIGN.md`: keep workflows in `app/`, keep module
  boundaries sharp, and prefer fixing the direct seam over adding wrappers

## What changed
- removed the extra fixed-`mu` re-minimization from the reported fit result so
  nuisance values, parameter values, covariance, status, and predictions all
  come from one optimum
- made `mk_channel` require explicit observed bins unless
  `--allow-zero-data` is passed
- made `mk_xsec_fit` reject all-zero observed spectra unless
  `--allow-zero-data` is passed
- tightened channel assembly so signal and background caches must share
  `selection_expr`
- changed unlabeled detector-template nuisance fallback names to stay
  process-local instead of correlating `template0`, `template1`, and so on
  across every process

## Why this is simpler
- the fit report no longer mixes two nearby but distinct optima
- zero-observation fits are now explicit intent instead of a missing-input
  accident
- channel metadata now has to match the actual cached distributions it
  assembles
- missing detector labels degrade to local nuisances rather than silently
  inventing global correlations

## Verification
- `git diff --check -- fit/SignalStrengthFit.cc app/mk_channel.cc app/mk_xsec_fit.cc io/macro/write_channel.C USAGE COMMANDS`
- Docker focused smoke:
  - build `IO`, `Fit`, `mk_channel`, and `mk_xsec_fit`
  - verify `mk_channel` fails without data unless `--allow-zero-data` is set
  - verify `mk_xsec_fit` fails on all-zero observed bins unless
    `--allow-zero-data` is set
  - verify selection mismatches are rejected
  - verify unlabeled detector templates are named
    `detector:<process>:templateN`
  - verify nuisance lines match the corresponding parameter lines in the fit
    report
- results:
  - `git diff --check` passed
  - Docker smoke passed with `fit_channel_corrections_smoke=ok`

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - removed the extra result-repackaging step that mixed two fit points in one
    report
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: modest additive safety checks in the existing
  fit/channel surface

## Current milestone
- status: done
- subsystem: `syst/`
- design rule from `DESIGN.md`: keep module boundaries sharp, keep module
  layout flat, and add abstractions only when they delete complexity

## What changed
- split detector handling and universe-family handling out of the monolithic
  `Systematics.cc`
- added:
  - `syst/Support.cc`
  - `syst/Detector.cc`
  - `syst/UniverseFill.cc`
  - `syst/UniverseSummary.cc`
  - `syst/bits/Detail.hh`
- kept `Systematics.hh` as the one public header and left top-level evaluate /
  cache orchestration in `Systematics.cc`

## Why this is simpler
- review and maintenance no longer require paging through detector and
  universe-family logic interleaved in one file
- the top-level entrypoints can stay focused on cache policy and orchestration
- the split stays internal, so the public API does not grow just to express
  implementation detail

## Verification
- configure/build commands:
  - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- target-only commands:
  - `cmake --build build --target Syst mk_eventlist --parallel`
  - `docker build -t amarantin-dev .`
  - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B /tmp/amarantin-syst-check -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/amarantin-syst-check --target Syst --parallel'`
- shell checks:
  - `git diff --check -- syst/CMakeLists.txt syst/Systematics.hh syst/Systematics.cc syst/bits/* .agent/current_execplan.md docs/minimality-log.md`
- smoke checks: pending
- results:
  - `git diff --check` passed
  - Docker focused build passed for `Syst`
  - the default host `build/` tree is still unreliable in this checkout, so
    the focused verification ran in a clean temporary Docker build tree

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - removed the monolithic co-location of detector and universe-family
    implementation inside one `syst/Systematics.cc`
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: net additive private implementation split with a much
  smaller public-orchestration file

## Decisions
- split by systematic responsibility, not by adding one file per public API
  wrapper
- keep the shared helper surface private to `syst/`

## Remaining hotspots
- decide after the split whether cache-key, rebin, and memory-cache helpers
  should stay in `Systematics.cc` or move again
- restore a valid local host configure/build environment so future checks do
  not have to rely on Docker

## Current milestone
- status: done
- subsystem: `tools/` + sample workflow
- design rule from `DESIGN.md`: keep workflows in `app/`, keep module
  boundaries sharp, and add abstractions only when they delete complexity

## What changed
- started a follow-up sample-handling pass to make the flat catalog honest
  about artifact source kinds instead of pretending every shard is a directory
  under one common base
- chosen implementation direction:
  - extend `tools/mklist.sh` with additive non-directory source support
  - let generated `samples-dag.mk` metadata describe per-artifact source kinds
  - replace the fake detector-row comments with real Run 1 detector rows backed
    by SAM good-runs definitions from the legacy workflow
  - make logical dataset assembly a first-class generated target

## Why this is simpler
- detector-variation provenance stays explicit instead of being smuggled into
  guessed directory names
- one source catalog can now describe nominal shards and detector shards on the
  same surface
- downstream users no longer need to hand-assemble the generated manifest into
  a separate `mk_dataset` command

## Verification
- configure/build commands:
  - none; this pass stayed on shell workflow and generated catalog surfaces
- target-only commands:
  - `make -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk print-samples`
  - `make -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk print-datasets`
  - `make -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk -n datasets`
- shell checks:
  - `bash -n tools/mklist.sh`
  - `bash -n tools/render-sample-catalog.sh`
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md tools/mklist.sh samples-dag.mk tools/render-sample-catalog.sh samples/README samples/catalog.tsv COMMANDS USAGE`
- smoke checks:
  - `bash tools/render-sample-catalog.sh`
  - stubbed `samweb` smoke for `tools/mklist.sh --samdef`
  - `tools/mklist.sh --list` normalization smoke
- results:
  - shell checks passed
  - generated run-1 outputs now include detector-variation logical samples and
    make metadata for `dir` plus `samdef` source kinds
  - `samples-dag.mk` now exposes a first-class `datasets` target that dry-runs
    to `build/datasets/run1_fhc.root`
  - `mklist.sh --samdef` resolved stubbed SAM file names into PNFS paths and
    `--list` normalized/sorted an existing list

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - remove the need to hand-write a separate `mk_dataset --defs --manifest`
    command once the generated catalog files already exist
- shell branches removed:
  - remove the hidden assumption that every generated sample list comes from a
    directory scan under one dataset base
- docs/build artifacts removed:
  - replace the fake detector-row comments with real run-1 detector rows
- approximate LOC delta: about `+250`

## Decisions
- use SAM good-runs definition names as catalog source references for detector
  variations when no honest output directory is available locally
- preserve the old `artifact:subdir` include shape as a compatibility path

## Remaining hotspots
- the local source material still does not pin down real run 2 / run 3 shard
  layouts, so this pass stays focused on Run 1 plus detector machinery

## Current milestone
- status: done
- subsystem: `app/` + sample workflow
- design rule from `DESIGN.md`: keep workflows in `app/`, keep module
  boundaries sharp, and prefer plain data over duplicated config layers

## What changed
- started a sample-handling pass to separate shard-level `SampleIO` artifacts
  from logical dataset sample keys
- chosen implementation direction:
  - additive `mk_dataset --manifest` support with repeated-key merging
  - per-artifact metadata overrides in `samples-dag.mk`
  - one flat sample catalog rendered into plain-text generated outputs

## Why this is simpler
- shard names like `beam_s0` and `ext_p0` stay where they belong: in the
  production/build layer
- downstream analysis can use stable logical keys like `beam`, `ext`, and
  detector siblings linked through one nominal key
- a flat catalog is easier to grep and regenerate than spreading sample
  metadata across XML stages, make variables, and ad hoc defs files

## Verification
- `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/mk_dataset.cc samples-dag.mk COMMANDS USAGE samples/README samples/datasets.tsv samples/catalog.tsv tools/render-sample-catalog.sh samples/generated/datasets.mk samples/generated/run1_fhc.sample.defs samples/generated/run1_fhc.dataset.manifest`
- `bash -n tools/render-sample-catalog.sh`
- `bash tools/render-sample-catalog.sh`
- `make -f samples-dag.mk DATASETS_FILE=samples/generated/datasets.mk print-samples`
- Docker configure/build:
  `cmake -S . -B /tmp/amarantin-sample-build -DCMAKE_BUILD_TYPE=Release`
- Docker target build:
  `cmake --build /tmp/amarantin-sample-build --target mk_dataset --parallel`
- Docker focused smoke:
  - create two tiny synthetic `SampleIO` files
  - assemble them through `mk_dataset --defs --manifest`
  - read the merged `DatasetIO` sample back and verify summed POT,
    recomputed normalisation, and stamped logical metadata
- results:
  - `git diff --check` passed
  - catalog render script parsed and generated the expected plain-text outputs
  - generated sample DAG expansion produced the shard-level run1 build paths
  - Docker `mk_dataset` build passed
  - focused smoke passed with:
    `mk_dataset: wrote /tmp/run1_fhc.dataset.root with 1 logical samples from manifest /tmp/run1_fhc.dataset.manifest`
    `sample_manifest_smoke=ok`

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - removed the requirement that one downstream dataset sample key correspond
    to exactly one shard-level `SampleIO` input
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: about `+450`

## Decisions
- keep the first pass additive and compatible with the current `mk_sample` /
  `mk_dataset` CLI surfaces
- make logical-sample merging happen at dataset assembly time, not in `io/`

## Remaining hotspots
- decide later whether the flat sample catalog should become the default root
  `datasets.mk` path
- decide later whether `mk_dataset` should gain a first-class logical-sample
  manifest type instead of the current repeated-key plain-text form

## Current milestone
- status: done
- subsystem: `fit/` + `io/`
- design rule from `DESIGN.md`: prefer plain data plus namespace functions and
  keep module boundaries sharp

## What changed
- renamed the primary fit API to `SignalStrengthFit.hh`, `fit::Problem`, and
  `fit::profile_signal_strength(...)`
- kept `fit/XsecFit.hh` as a thin migration shim while moving repo callsites to
  the new names
- replaced the first-pass coordinate-descent fitter with a ROOT Minuit2 joint
  minimizer path
- changed the default problem builder so family nuisances are shared across
  processes instead of duplicated per process
- wired detector template identities through `ChannelIO` and used detector,
  statistical, and total-envelope fallback payloads in the fit problem builder
- changed interval reporting so missing `Delta q = 1` crossings are explicit via
  `*_found` flags instead of silently appearing as zero uncertainty
- added fit status, EDM, parameter list, and covariance reporting to
  `mk_xsec_fit`

## Why this is simpler
- the old `fit::Model` / `profile_xsec(...)` names understated what the code
  actually did
- one shared nuisance book is easier to reason about than duplicated
  per-process mode parameters
- a single joint optimizer path is flatter than the old coarse `mu` scan plus
  coordinate-minimization loop
- detector identity now travels with the channel instead of being guessed only
  from template position

## Verification
- configure/build commands:
- Docker configure/build in a clean throwaway directory:
  `cmake -S . -B /tmp/amarantin-fit-refactor-build -DCMAKE_BUILD_TYPE=Release`
- target-only commands:
- Docker target build:
  `cmake --build /tmp/amarantin-fit-refactor-build --target Fit mk_xsec_fit --parallel`
- shell checks:
- `git diff --check -- .agent/current_execplan.md docs/minimality-log.md CMakeLists.txt app/mk_channel.cc app/mk_xsec_fit.cc fit/CMakeLists.txt fit/README fit/SignalStrengthFit.hh fit/SignalStrengthFit.cc fit/XsecFit.hh fit/macro/fit_channel.C fit/macro/scan_mu.C io/ChannelIO.hh io/ChannelIO.cc io/macro/write_channel.C .rootlogon.C USAGE`
- `bash -n tools/run-macro`
- smoke checks:
- Docker synthetic channel smoke:
  - compile a tiny `ChannelIO` writer against `libIO.so`
  - write a channel with one signal and two backgrounds sharing the same flux
    mode and detector key
  - run `mk_xsec_fit --output /tmp/signal-strength-smoke.fit.txt`
  - inspect nuisance names, interval flags, and covariance rows in the report
- results:
- `git diff --check` passed
- `bash -n tools/run-macro` passed
- Docker build passed:
  - `Built target Fit`
  - `Built target mk_xsec_fit`
- synthetic fit smoke passed with:
  - `converged: true`
  - `minimizer_status: 0`
  - one shared `flux:weightsPPFX:mode0` nuisance
  - one shared `detector:detA` nuisance
  - `mu_err_total_up_found: true`
  - covariance rows present in the report

## Reduction ledger
- files deleted:
  - `fit/XsecFit.cc`
- wrappers removed:
  - removed the old coordinate-descent profiling implementation
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: net additive; the new fit source replaces the old one
  but also adds covariance/status reporting and the small `ChannelIO` identity
  extension

## Decisions
- rename the primary fit API around signal-strength terminology
- use `total_down` / `total_up` only as fallback uncertainty inputs to avoid
  double-counting component systematics
- when detector templates are missing, keep detector-envelope fallback grouped
  by shared detector identity instead of splitting it back into one nuisance
  per process
- remove the legacy `fit/XsecFit.hh` shim once the repo callsites no longer use
  it

## Remaining hotspots
- host-side ROOT verification remains unavailable in this environment because
  `./.setup.sh` depends on CVMFS products and Fermilab `setup` tooling that are
  not installed here

## Current milestone
- status: done
- subsystem: `fit/`
- design rule from `DESIGN.md`: keep module boundaries sharp and record
  verification after each milestone

## What changed
- relaxed fit validation so detector-only `detector_sample_keys` remain valid
  even when no detector templates survive into `ChannelIO`
- changed the detector-envelope-only fallback to share nuisances by detector
  group key and morph through down / nominal / up rather than using the older
  one-sided linear envelope interpretation

## Why this is simpler
- detector identity now stays coherent even in reduced `ChannelIO` payloads
- the fallback behavior matches the persisted inputs more directly than a
  per-process linearized envelope shift

## Verification
- `git diff --check -- fit/SignalStrengthFit.cc fit/README docs/minimality-log.md .agent/current_execplan.md`
- Docker envelope-only fit smoke:
  - write a synthetic `ChannelIO` channel with `detector_down` / `detector_up`
    but no detector templates
  - run `mk_xsec_fit` on that channel
  - inspect nuisance names in the text report
- host setup check:
  - `source ./.setup.sh`
- results:
  - `git diff --check` passed for the touched files
  - Docker envelope-only smoke passed with one shared `detector:detA` nuisance
  - host setup failed before ROOT setup because:
    - `/cvmfs/uboone.opensciencegrid.org/products/setup_uboone_mcc9.sh: No such file or directory`
    - `/cvmfs/fermilab.opensciencegrid.org/products/common/etc/setups: No such file or directory`
    - `setup: command not found`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: small additive follow-up inside the fit fallback path

## Current milestone
- status: done
- subsystem: `plot/`
- design rule from `DESIGN.md`: delete stale scaffolding after feature work and
  keep abstractions only when they reduce complexity

## What changed
- deleted stale state from `StackedHist` / `UnstackedHist`
- replaced cross-translation-unit helper mirroring with one private
  shared histogram-helper surface
- trimmed legacy compatibility carryover and unused in-tree helper surface from
  `PlottingHelper`
- added accurate weighted-total accessors to `EfficiencyPlot` while keeping raw
  row counters available

## Why this is simpler
- stack / unstack objects should only retain state that actually drives drawing
- one private helper surface is easier to grep and safer to refactor than
  manually mirrored declarations
- `EfficiencyPlot` now exposes the same weighted totals it reports and draws
- undocumented `HERON_*` carryover no longer obscures the active `plot/`
  environment surface

## Verification
- `git diff --check -- .agent/current_execplan.md docs/minimality-log.md plot/StackedHist.hh plot/StackedHist.cc plot/UnstackedHist.hh plot/UnstackedHist.cc plot/EfficiencyPlot.hh plot/EfficiencyPlot.cc plot/PlottingHelper.hh plot/PlottingHelper.cc plot/bits/DataMcHistogramUtils.hh`
- Docker configure/build:
  `cmake -S . -B /tmp/amarantin-plot-cleanup-build -DCMAKE_BUILD_TYPE=Release`
- Docker target build:
  `cmake --build /tmp/amarantin-plot-cleanup-build --target Plot --parallel`
- results:
  - `git diff --check` passed
  - Docker `Plot` rebuild passed: `Built target Plot`

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - removed the stale `StackedHist` / `UnstackedHist` member bookkeeping that
    did not affect drawing
  - removed the manual helper declarations in `UnstackedHist.cc`
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: net negative inside the touched plot classes despite
  the one new private helper header

## Remaining hotspots
- decide later whether `denom_entries()` / `pass_entries()` should eventually be
  renamed outright in a public-surface cleanup pass

## Current milestone
- status: done
- subsystem: `plot/`
- design rule from `DESIGN.md`: prefer plain data and `EventListIO`-first
  downstream APIs

## What changed
- started a legacy-to-current plotting port for `Plotter`, `PlottingHelper`,
  `StackedHist`, and `UnstackedHist`
- chose an `EventListIO`-native descriptor layer instead of importing the old
  `heron` selection / frame wrapper graph
- wired the new plotting surface into the `Plot` target and ROOT startup header
  exposure

## Why this is simpler
- the new code uses data already written by `EventListBuild`: per-sample trees,
  `__analysis_channel__`, and `__w__`
- plot construction stays inside `plot/` without pushing workflow or selection
  abstractions across module boundaries
- the public surface is additive, but the implementation stays much flatter than
  the legacy stack it came from

## Verification
- configure/build commands:
  - Docker configure/build in a clean throwaway build directory:
    `cmake -S . -B /tmp/amarantin-plot-port-build -DCMAKE_BUILD_TYPE=Release`
- target-only commands:
  - Docker target build:
    `cmake --build /tmp/amarantin-plot-port-build --target Plot --parallel`
- shell checks:
  - `bash -n tools/run-macro`
- smoke checks:
  - standalone C++ smoke linked against `libPlot.so` and `libIO.so`
  - smoke wrote a synthetic `EventListIO`, ran both new renderers, and checked:
    - `/tmp/plot-smoke-out/stack_smoke.png`
    - `/tmp/plot-smoke-out/unstack_smoke.png`
- results:
  - `Plot` built successfully in Docker
  - the first build exposed header-surface issues around `TMatrixDSym` and
    incomplete ROOT types held by `unique_ptr`; those were repaired
  - the final smoke passed with:
    - `plot_smoke stack=1 unstack=1`

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - avoided importing legacy `SelectionEntry` / `Frame` wrappers into
    `amarantin`
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: pending current milestone
- approximate LOC delta: about `+2k` additive LOC for the new plotting surface

## Decisions
- keep the port centered on `EventListIO` and framework-owned event-list
  branches
- keep the new plotting descriptors small and additive rather than recreating
  the full legacy plot abstraction surface

## Remaining hotspots
- verify the new sources cleanly build on the current `Plot` target
- decide whether dedicated stack/unstack ROOT macros are needed after the core
  classes land

---

## Current milestone
- status: done
- subsystem: `fit/`
- design rule from `DESIGN.md`: keep `io/` persistence-only and prefer plain
  data plus namespace functions

## What changed
- added a small additive `Fit` library with `fit::Model`, `fit::FitOptions`,
  `fit::Result`, `fit::make_independent_model(...)`, and
  `fit::profile_xsec(...)`
- kept the fit surface on top of `ChannelIO` instead of importing `collie`'s
  I/O and loader abstractions
- added the requested toy signal-strength scan to the root `README`

## Why this is simpler
- the fit input is the repo's existing final assembled channel surface,
  `ChannelIO`, rather than a second parallel persistence format
- the public interface is one header with plain structs and free functions
- the default nuisance model is built directly from persisted modes already
  carried by `ChannelIO::Process`

## Verification
- configure/build commands:
  - host configure attempt failed because the local environment could not supply
    usable `SQLite3` / `Eigen3` discovery to CMake
  - Docker configure/build in the repo image:
    `docker run --rm -v /Users/user/programs/amarantin:/work -w /work amarantin-dev:latest bash -lc 'cmake -S . -B /tmp/amarantin-fit-build -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/amarantin-fit-build --target Fit --parallel'`
- target-only commands:
  - `cmake --build build --target Fit --parallel`
- shell checks:
  - none; no shell helper changed in this pass
- smoke checks:
  - Docker compile-and-run smoke against the public header and linked
    `libFit.so` / `libIO.so`
- results:
  - Docker build passed:
    `Built target Fit`
  - public-header smoke passed:
    `fit_smoke=ok`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: about `+500` additive LOC for the first native fit
  surface

## Decisions
- make the fitter operate on `ChannelIO` directly
- treat each persisted fit mode as an independent nuisance by default until
  shared cross-process correlation metadata is added

## Remaining hotspots
- verify the new `Fit` target in the current build tree
- decide whether a follow-up pass should add shared nuisance books or a CLI
  entrypoint

---

## Current milestone
- status: done
- subsystem: `app/` + `fit/`
- design rule from `DESIGN.md`: keep workflows in `app/` and prefer plain data
  plus namespace functions

## What changed
- wired the native `fit/` library into the normal CLI workflow
- added additive `mk_channel` and `mk_xsec_fit` entrypoints on top of
  `ChannelIO` and the existing `fit::profile_xsec(...)` surface
- updated `COMMANDS`, `INSTALL`, `USAGE`, and the root `README` so the
  channel-and-fit path is documented next to the existing `mk_*` workflow

## Why this is simpler
- the fit stops being a library-only island and becomes reachable through the
  same `mk_*` style used elsewhere in the repo
- the first pass avoids a new persistence class and keeps reporting as plain
  text
- the channel assembly step stays explicit and small instead of adding a
  generic process-spec parser before there is a demonstrated need

## Verification
- configure/build commands:
- Docker configure/build in a throwaway directory:
  `cmake -S . -B /tmp/amarantin-fit-wire-build -DCMAKE_BUILD_TYPE=Release`
- target-only commands:
- Docker target build:
  `cmake --build /tmp/amarantin-fit-wire-build --target mk_channel mk_xsec_fit --parallel`
- shell checks:
- CLI help checks inside the same Docker run:
  - `/tmp/amarantin-fit-wire-build/bin/mk_channel --help`
  - `/tmp/amarantin-fit-wire-build/bin/mk_xsec_fit --help`
- smoke checks:
- end-to-end Docker smoke:
  - compile a tiny `DistributionIO` writer
  - run `mk_channel` on the synthetic cache
  - run `mk_xsec_fit --output /tmp/muon_region.fit.txt`
  - grep the report for `converged`, `mu_hat`, and `predicted_total_bins`
- results:
- build passed:
  - `Built target mk_channel`
  - `Built target mk_xsec_fit`
- end-to-end smoke passed:
  - `mk_channel: wrote /tmp/fit-flow.channels.root channel muon_region from /tmp/fit-flow.dists.root`
  - `mk_xsec_fit: wrote /tmp/muon_region.fit.txt from /tmp/fit-flow.channels.root channel muon_region`
  - `converged: true`
  - `mu_hat: 1.195245`

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: about `+250` additive LOC for the new CLI wiring and
  doc updates

## Decisions
- add small CLIs before considering a new fit-result file format
- keep the fit output as a plain-text report rather than adding `FitResultIO`
  in the first workflow pass

## Remaining hotspots
- decide whether later passes need multi-process channel assembly instead of
  the current one-signal / one-background shape
- decide whether shared cross-process nuisance books should replace the current
  independent-mode default

---

## Current milestone
- status: done
- subsystem: root build-workflow hygiene
- design rule from `DESIGN.md`: keep helper scripts short and keep workflows
  direct

## What changed
- added a hidden `.build/` convention for extra local build trees
- added one short `tools/configure-build` helper to configure
  `.build/<name>` directly
- added workspace exclusions so `build-*`, `cmake-build-*`, `.build`, and
  `install` stop cluttering the explorer and search views
- updated build docs to keep the canonical `build/` path while pointing extra
  scratch builds at `.build/`

## Why this is simpler
- the canonical `build/` path stays intact
- extra builds stop multiplying as visible `build-*` and `cmake-build-*`
  siblings in the root
- the fix is small and local instead of adding a heavy build manager

## Verification
- configure/build commands:
- target-only commands:
- shell checks:
- `bash -n tools/configure-build`
- smoke checks:
- `python3 -m json.tool .vscode/settings.json`
- results:
- helper script syntax check passed
- workspace settings JSON parsed cleanly

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: about `+40` additive LOC for the helper, workspace
  settings, and small doc updates

## Decisions
- prefer preventing new root clutter over deleting existing build trees

## Remaining hotspots
- decide later whether old root-level build trees should be deleted in a
  separate explicit cleanup pass

---

## Current milestone
- status: done
- subsystem: `app/` + `fit/` + installed workflow surface
- design rule from `DESIGN.md`: keep workflows direct, delete stale wrapper
  paths once the native path is proven, and avoid leaving compatibility layers
  as the documented default

## What changed
- removed the legacy cache-building branch from `mk_eventlist`
- deleted the `mk_channel` app target and source file
- moved `mk_xsec_fit` onto direct cached `DistributionIO` inputs with:
  - a quick two-process mode
  - a manifest-driven multi-process mode
  - observed-data assembly from `data` rows
- moved `ChannelIO.hh` off the installed public header surface
- rewrote top-level docs and module docs so the native flow is now:
  - `mk_eventlist`
  - `mk_dist`
  - `mk_xsec_fit`
- reframed `ChannelIO` as a local legacy helper rather than an installed
  workflow surface

## Why this is simpler
- the fit workflow no longer requires a second persisted artifact just to pass
  cached distributions into the fitter
- `mk_eventlist` is back to owning only the row-wise build step
- the installed/public story now matches the documented workflow story
- the final downstream path is flatter and easier to grep:
  `DistributionIO -> mk_xsec_fit`

## Verification
- local checks:
  - `git diff --check -- app/CMakeLists.txt app/mk_eventlist.cc app/mk_xsec_fit.cc fit/SignalStrengthFit.hh fit/SignalStrengthFit.cc io/CMakeLists.txt COMMANDS USAGE INSTALL fit/README io/README io/bits/DERIVED .agent/current_execplan.md docs/minimality-log.md`
- Docker checks:
  - `docker build -t amarantin-dev .`
  - focused Linux rebuild of `IO`, `Ana`, `Syst`, `Fit`, `mk_eventlist`,
    `mk_dist`, and `mk_xsec_fit`
  - `mk_eventlist --help` usage smoke with a negative grep confirming that
    `--cache-systematics` is gone
  - `mk_xsec_fit --help` usage smoke confirming the direct
    `--manifest <fit.manifest>` form
  - synthetic direct-fit smoke:
    - write a temporary `DistributionIO` file with `signal`, `background`,
      and `data` cache entries
    - run `mk_xsec_fit --manifest /tmp/direct-fit.manifest`
    - verify the report includes `distribution_path`, `eventlist_path`,
      `observed_source_keys`, and `signal_process`
- results:
  - focused Docker rebuild passed
  - direct `DistributionIO -> mk_xsec_fit` smoke passed
  - `mk_eventlist` no longer advertises cache flags

## Reduction ledger
- files deleted: 1
- wrappers removed:
  - remove the cache-building bridge from `mk_eventlist`
  - remove the separate persistent-channel bridge from the native fit path
- shell branches removed: 0
- docs/build artifacts removed:
  - `mk_channel` from build/install/docs
  - `mk_eventlist --cache-*` from the documented workflow
  - `ChannelIO.hh` from the installed public-header surface
- approximate LOC delta: net reduction from deleting one app and one legacy
  branch while keeping the direct fit assembly in one CLI

## Decisions
- keep `ChannelIO` in-tree for local legacy macros for now instead of turning
  this pass into a broad macro rewrite
- prefer one in-memory `fit::Channel` assembly path in `mk_xsec_fit` over a
  second persisted downstream format

## Remaining hotspots
- local ROOT macros such as `plot_channel.C`, `fit_channel.C`, `scan_mu.C`,
  and `write_channel.C` still depend on `ChannelIO`, but they are no longer on
  the installed or documented workflow path

---

## Current milestone
- status: done
- subsystem: `ana/` EventList sample-partition policy boundary
- design rule from `DESIGN.md`: keep module boundaries sharp, prefer plain
  data and namespace functions, and keep analysis-specific policy out of
  generic workflow plumbing

## What changed
- removed the inline overlay/signal `count_strange` split from
  `ana/EventListBuild.cc`
- introduced one plain-data `ana::SampleSelectionRule` surface in
  `ana/SignalDefinition.hh`
- moved the canonical analysis-specific EventList sample-partition rule into
  `ana/SignalDefinition.cc`
- updated `ana/README` to name `SignalDefinition` as the owner of that policy
- corrected `io/bits/DERIVED` so it describes the rule as owned by the
  canonical `ana` EventList build policy rather than by `EventListIO`

## Why this is simpler
- `EventListBuild` is back to generic work: chain setup, selection assembly,
  branch validation, and persistence
- the analysis-specific rule is now in one grep-friendly home instead of being
  mixed into generic builder control flow
- future changes can either replace that one policy function or generalise it
  without rediscovering hidden origin-specific branches in the builder

## Verification
- local checks:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md ana/EventListBuild.cc ana/SignalDefinition.hh ana/SignalDefinition.cc ana/README io/bits/DERIVED`
- targeted build checks:
  - attempted `cmake --build build --target Ana mk_eventlist --parallel`
- results:
  - tracked-file `git diff --check` passed
  - the targeted rebuild is blocked because the existing `build/` tree invokes
    `/usr/bin/cmake`, which is not present in this environment
  - a fresh configure/build was not attempted because local ROOT discovery is
    unavailable here: `root-config` is not on `PATH`, and the previously
    cached `/opt/root/bin/root-config` path no longer exists

## Reduction ledger
- files deleted: 0
- wrappers removed:
  - inline analysis-specific overlay/signal filtering branches from
    `ana/EventListBuild.cc`
- shell branches removed: 0
- docs/build artifacts removed:
  - stale wording that implied `EventListIO` owned sample orthogonalisation
- approximate LOC delta:
  - code files in scope: `+40 / -24`
  - plus additive tracking-log updates

## Decisions
- keep this pass behavior-preserving and local to the EventList build path
- centralise the current rule before adding any configurability
- treat the overlay/signal `count_strange` split as analysis policy, not
  generic EventList-builder policy

## Remaining hotspots
- if you want true generalisation rather than better ownership, the next step
  is a `BuildConfig`-level sample-filter policy surface instead of another
  hardcoded analysis helper

---

## Current milestone
- status: done
- subsystem: fit CLI public-surface rename
- design rule from `DESIGN.md`: keep workflows in `app/`, keep the surface
  small and cheap to change, and avoid unnecessary churn beyond the real user
  interface

## What changed
- renamed the app target and installed executable from `mk_xsec_fit` to
  `mk_fit`
- updated fit CLI help, success messages, and error-prefix handling so the
  user-facing program name is consistently `mk_fit`
- updated the current workflow docs and architecture notes to teach `mk_fit`
  as the canonical downstream fit executable
- kept `app/mk_xsec_fit.cc` in place so this pass stayed on the executable
  surface instead of widening into source-file rename churn

## Why this is simpler
- `mk_fit` is shorter, easier to type, and easier to teach in examples
- the rename stayed local to the real public surface: build target, binary
  name, CLI text, and current docs
- leaving the source file path alone kept the change small while still making
  the user-facing interface consistent

## Verification
- local checks:
  - `git diff --check -- .agent/current_execplan.md docs/minimality-log.md app/CMakeLists.txt app/mk_xsec_fit.cc COMMANDS USAGE INSTALL fit/README VISION.md INVARIANTS.md docs/repo-internals.puml`
- Docker checks:
  - `docker build -t amarantin-dev .`
  - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B .build/mk-fit-rename-docker -DCMAKE_BUILD_TYPE=Release && cmake --build .build/mk-fit-rename-docker --target mk_fit --parallel && (.build/mk-fit-rename-docker/bin/mk_fit --help || true) > /tmp/mk_fit.help 2>&1 && ! grep -q -- "mk_xsec_fit" /tmp/mk_fit.help && grep -q -- "usage: mk_fit" /tmp/mk_fit.help'`
- results:
  - tracked-file `git diff --check` passed
  - fresh Docker configure/build passed
  - the Docker smoke confirmed `usage: mk_fit` and no remaining
    `mk_xsec_fit` string in the help output

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed: 0
- docs/build artifacts removed:
  - current workflow references to `mk_xsec_fit`
- approximate LOC delta:
  - small build-surface and doc-surface rename
  - no source-file move in this pass

## Decisions
- rename the executable target now, but keep `app/mk_xsec_fit.cc` as the
  implementation file to avoid unnecessary rename churn
- treat `docs/minimality-log.md` historical `mk_xsec_fit` references as
  historical record rather than rewrite old milestones

## Remaining hotspots
- if you later want the source tree to match the new executable name exactly,
  the follow-up is a pure file-rename pass for `app/mk_xsec_fit.cc`
