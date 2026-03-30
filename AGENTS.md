# AGENTS.md

## Read first
Before editing, read these files in this order:
1. `DESIGN.md`
2. `COMMANDS`
3. `INSTALL`
4. `USAGE`
5. `CMakeLists.txt` and the relevant subdirectory `CMakeLists.txt`

`DESIGN.md` is the source of truth for style and architecture.

## Repository map
- `io/`
  - persistence only; ROOT object layout, sample / dataset / event-list I/O
- `ana/`
  - event-list construction, selection logic, sample definitions
- `syst/`
  - systematic calculations and cache construction
- `plot/`
  - rendering only
- `app/`
  - thin CLI orchestration: `mk_sample`, `mk_dataset`, `mk_eventlist`
- `tools/`
  - shell helpers; keep them short and direct
- `samples-dag.mk`, `datasets.mk`
  - higher-level workflow declarations and sample build orchestration

## Core policy
Follow `DESIGN.md` literally.

In particular:
- keep `io/` persistence-only
- prefer plain data and namespace functions
- keep workflows in `app/`
- add abstractions only when they delete complexity
- do a small deletion pass after every feature pass

## What to optimize for
When simplifying `amarantin`, optimize for:
- fewer concepts per workflow
- sharper module boundaries
- flatter control flow
- less wrapper ceremony
- less ad hoc shell logic
- easier grep-based navigation

Prefer the style already present in:
- `ana::build_event_list(...)`
- `syst::build_systematics_cache(...)`

## Guardrails
- Preserve external behavior unless the ExecPlan explicitly approves a change.
- Preserve installed CMake targets and installed public headers by default.
- Do not move analysis or systematics logic back into `io/`.
- Do not let `plot/` become a generic utility bucket.
- Do not do rename churn or drive-by formatting.
- Keep changes milestone-sized and continuously verified.

## Current priorities
### High value, lower risk
1. Simplify `tools/run-macro` if branching or type inference can be reduced without changing the documented invocation shape.
2. Trim stale docs, includes, and helper scaffolding after each feature pass.
3. Flatten wrapper layers in non-installed code before adding new abstraction.

### Medium risk
1. Audit static-only public utility headers such as:
   - `io/include/SnapshotService.hh`
   - `ana/include/EventListSelection.hh`
   - `ana/include/AnalysisChannels.hh`
2. Only change those APIs if a migration is explicitly approved.

### Lower priority
- broad redesign in `plot/` or `syst/`
- build-system churn after the recent CMake cleanup

## Mandatory workflow for non-trivial refactors
For any multi-file or multi-hour simplification pass:
1. Create or update `.agent/current_execplan.md` from `.agent/PLANS.md`.
2. Create or update `docs/minimality-log.md` from `docs/minimality-log.template.md`.
3. Work one milestone at a time.
4. After each milestone, run relevant verification and repair failures before continuing.
5. Record what got smaller, flatter, or easier to grep.

## Verification
Primary build path:
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- `cmake --build build --parallel`

Selected target builds when scope is local:
- `cmake --build build --target IO`
- `cmake --build build --target Ana`
- `cmake --build build --target Syst`
- `cmake --build build --target Plot`
- `cmake --build build --target mk_sample`
- `cmake --build build --target mk_dataset`
- `cmake --build build --target mk_eventlist`

Shell checks:
- `bash -n tools/mklist.sh`
- `bash -n tools/run-macro`
- `bash -n tools/overnight-minimality-pass.sh`

CLI smoke checks:
- `build/bin/mk_sample --help`
- `build/bin/mk_dataset --help || true`
- `build/bin/mk_eventlist --help || true`

If a ROOT environment is available and the change touches macro plumbing, run one representative `tools/run-macro` example from `COMMANDS`.

## Done means
A simplification pass is done only when:
- the planned milestone is complete or explicitly deferred
- relevant verification passes
- `.agent/current_execplan.md` has no `in_progress` items
- `docs/minimality-log.md` records the reductions and remaining hotspots
