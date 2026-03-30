# Minimality Log

## Current milestone
- status: blocked
- subsystem: `syst/`
- design rule from `DESIGN.md`: keep module boundaries sharp, keep module
  layout flat, and add abstractions only when they delete complexity

## What changed
- started a `syst/` refactor to split detector handling and universe-family
  handling out of the monolithic `Systematics.cc`
- chosen implementation direction:
  - keep `Systematics.hh` as the one public header
  - move shared private declarations into a small internal helper surface
  - keep top-level evaluate / cache orchestration in `Systematics.cc`

## Why this is simpler
- review and maintenance no longer require paging through detector and
  universe-family logic interleaved in one file
- the top-level entrypoints can stay focused on cache policy and orchestration
- the split stays internal, so the public API does not grow just to express
  implementation detail

## Verification
- configure/build commands: pending
- target-only commands:
  - `cmake --build build --target Syst mk_eventlist --parallel`
- shell checks:
  - `git diff --check -- syst/CMakeLists.txt syst/Systematics.hh syst/Systematics.cc syst/bits/* .agent/current_execplan.md docs/minimality-log.md`
- smoke checks: pending
- results: pending
  - deferred without new code changes while higher-priority sample-workflow
    work proceeded

## Reduction ledger
- files deleted: pending
- wrappers removed: pending
- shell branches removed: 0
- docs/build artifacts removed: 0
- approximate LOC delta: pending

## Decisions
- split by systematic responsibility, not by adding one file per public API
  wrapper
- keep the shared helper surface private to `syst/`

## Remaining hotspots
- decide after the split whether cache-key, rebin, and memory-cache helpers
  should stay in `Systematics.cc` or move again

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
