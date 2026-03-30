# ExecPlan

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
  - `syst/SystematicsSupport.cc`
  - `syst/SystematicsDetector.cc`
  - `syst/SystematicsUniverseFill.cc`
  - `syst/SystematicsUniverseSummary.cc`
  - `syst/bits/SystematicsInternal.hh`
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
