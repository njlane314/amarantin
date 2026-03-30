# Minimality Log

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
