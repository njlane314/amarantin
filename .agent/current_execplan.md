# ExecPlan

## 1. Objective
Run small verified simplification loops that make local hotspots flatter and
easier to grep while preserving documented behavior and installed surfaces.

## 2. Constraints
- Do not change the supported invocation grammar described in `COMMANDS`.
- Do not change installed headers or CMake targets.
- Keep the change local to `tools/` plus the required tracking files.
- Leave `io/`, `ana/`, `syst/`, and `plot/` behavior untouched.

## 3. Design anchor
From `DESIGN.md`:
- prefer plain data and namespace functions
- add abstractions only when they delete complexity
- after every feature pass, do a small deletion pass

For this milestone, that means reducing shell branching and helper ceremony in
`tools/run-macro` without widening its scope.

## 4. System map
- `tools/run-macro`
- `COMMANDS`
- `AGENTS.md`
- `.agent/PLANS.md`
- `docs/minimality-log.md`

Verification:
- `bash -n tools/mklist.sh tools/run-macro tools/overnight-minimality-pass.sh`
- one representative wrapper smoke check if ROOT is available

## 5. Candidate simplifications

### wrapper collapse
- inline macro-path lookup instead of a dedicated search helper
- flatten typed-literal coercion into smaller reusable checks

### script simplification
- remove duplicated regex branches
- replace heredoc array fill with `mapfile`

### boundary sharpening
- keep the change fully inside `tools/`

### doc / build cleanup
- no doc change unless CLI behavior changes

### stale scaffolding
- none in this milestone

## 6. Milestones

### Milestone 1
- status: done
- hypothesis: a flatter `tools/run-macro` is easier to audit and maintain
- files / symbols touched:
  - `tools/run-macro`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: low
- verification commands:
  - `bash -n tools/mklist.sh tools/run-macro tools/overnight-minimality-pass.sh`
  - `bash tools/run-macro plot_event_display`
- acceptance criteria:
  - script is shorter or flatter
  - documented invocation forms still work
  - shell checks pass

### Milestone 2
- status: done
- hypothesis: `SnapshotService` should stop being the only API shape internally;
  a namespace-style API plus a wrapper class is flatter and fits `DESIGN.md`
  better without breaking installed code
- files / symbols touched:
  - `io/include/SnapshotService.hh`
  - `io/src/SnapshotService.cc`
  - `io/macro/mk_snapshot.C`
- expected behavior risk: low
- verification commands:
  - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IO --parallel && bash tools/run-macro mk_snapshot'`
- acceptance criteria:
  - installed `SnapshotService` API still compiles
  - internal call sites can use namespace functions instead of the static-only class
  - `IO` builds and the snapshot macro reaches its entry point

### Milestone 3
- status: done
- hypothesis: `EventListSelection` should follow the same pattern as `SnapshotService`;
  namespace functions are the simpler shape, while the installed class can remain
  as a thin compatibility wrapper
- files / symbols touched:
  - `ana/include/EventListSelection.hh`
  - `ana/src/EventListSelection.cc`
  - `ana/src/EventListBuilder.cc`
- expected behavior risk: low
- verification commands:
  - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target Ana mk_eventlist --parallel && ./build/bin/mk_eventlist --help || true'`
- acceptance criteria:
  - installed `EventListSelection` API still compiles
  - internal builder code can use namespace functions instead of the static-only class
  - `Ana` and `mk_eventlist` build and the CLI still responds normally

### Milestone 4
- status: done
- hypothesis: `AnalysisChannels` should use the same namespace-first pattern as the other static-only public utility headers; this keeps the preferred internal shape simple without breaking the installed header
- files / symbols touched:
  - `ana/include/AnalysisChannels.hh`
  - `ana/src/EventListBuilder.cc`
- expected behavior risk: low
- verification commands:
  - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target Ana mk_eventlist --parallel && ./build/bin/mk_eventlist --help || true'`
- acceptance criteria:
  - installed `AnalysisChannels` API still compiles
  - internal builder code can use namespace functions instead of the static-only class
  - `Ana` and `mk_eventlist` build and the CLI still responds normally

### Milestone 5
- status: done
- hypothesis: `SystematicsEngine` should follow the same namespace-first pattern as the other static-only utility headers; the installed wrapper can remain while internal code moves to direct `syst::...` calls
- files / symbols touched:
  - `syst/include/Systematics.hh`
  - `syst/src/Systematics.cc`
  - `syst/src/SystematicsCacheBuilder.cc`
  - `plot/macro/cache_systematics.C`
- expected behavior risk: low
- verification commands:
  - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target Syst Plot mk_eventlist --parallel && bash tools/run-macro cache_systematics && ./build/bin/mk_eventlist --help || true'`
- acceptance criteria:
  - installed `SystematicsEngine` API still compiles
  - internal `syst/` and macro code can use namespace functions instead of the static-only class
  - `Syst`, `Plot`, and `mk_eventlist` build and the macro/CLI smoke checks still respond normally

### Milestone 6
- status: done
- hypothesis: `SystematicsCacheBuilder` is not a real subsystem; folding its structs and one function into `Systematics.hh/cc` should reduce conceptual spread and delete one source file while keeping include compatibility
- files / symbols touched:
  - `syst/include/Systematics.hh`
  - `syst/include/SystematicsCacheBuilder.hh`
  - `syst/src/Systematics.cc`
  - `syst/src/SystematicsCacheBuilder.cc`
  - `syst/CMakeLists.txt`
- expected behavior risk: low
- verification commands:
  - `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target Syst Plot mk_eventlist --parallel && bash tools/run-macro cache_systematics && ./build/bin/mk_eventlist --help || true'`
- acceptance criteria:
  - `CacheRequest`, `CacheBuildOptions`, and `build_systematics_cache(...)` live in the main `Systematics` interface
  - `SystematicsCacheBuilder.hh` remains as a compatibility shim
  - one source file is removed from the build
  - `Syst`, `Plot`, and `mk_eventlist` build and the macro/CLI smoke checks still respond normally

## 7. Public-surface check
- compatibility impact:
  - Milestone 1: none intended
  - Milestone 2: additive namespace API in `SnapshotService.hh`; existing class kept as a compatibility wrapper
  - Milestone 3: additive namespace API in `EventListSelection.hh`; existing class kept as a compatibility wrapper
  - Milestone 4: additive namespace API in `AnalysisChannels.hh`; existing class kept as a compatibility wrapper
  - Milestone 5: additive namespace API in `Systematics.hh`; existing class kept as a compatibility wrapper
  - Milestone 6: `SystematicsCacheBuilder.hh` becomes a compatibility include shim; declarations move into `Systematics.hh`
- migration note:
  - Milestone 1: non-goal; keep current CLI behavior
  - Milestone 2: internal and new code should prefer `snapshot::...`; existing `SnapshotService::...` remains valid
  - Milestone 3: internal and new code should prefer `eventlist_selection::...`; existing `EventListSelection::...` remains valid
  - Milestone 4: internal and new code should prefer `analysis_channels::...`; existing `AnalysisChannels::...` remains valid
  - Milestone 5: internal and new code should prefer free `syst::...` functions; existing `syst::SystematicsEngine::...` remains valid
  - Milestone 6: internal and new code should include `Systematics.hh`; existing includes of `SystematicsCacheBuilder.hh` remain valid
- reviewer sign-off: not required; no installed surface was removed

## 8. Reduction ledger
- files deleted: 0
- wrappers removed:
  - `find_macro`
  - duplicated per-call snapshot orchestration collapsed into `snapshot_to_scratch(...)`
  - `EventListSelection` no longer serves as the only real API shape internally
  - `AnalysisChannels` no longer serves as the only real API shape internally
  - `SystematicsEngine` no longer serves as the only real API shape internally
  - `SystematicsCacheBuilder` is no longer a separate compiled layer
- shell branches removed: several duplicated literal-coercion branches folded behind shared checks
- stale docs removed: 0
- targets or dependencies removed: 0
- approximate LOC delta:
  - `tools/run-macro` +6 lines, with flatter control flow
  - `SnapshotService` implementation +54 lines, but with one shared snapshot path and a smaller public pattern
  - `EventListSelection` touched in 3 files, net larger, but with a flatter internal call path
  - `AnalysisChannels` touched in 2 files, net larger, but with a flatter internal call path
  - `SystematicsEngine` touched in 4 files, net larger, but with a flatter internal call path
  - `SystematicsCacheBuilder` folded into `Systematics`, with 1 source file deleted

## 9. Decision log
- Keep the milestone inside `tools/run-macro`.
- Preserve the typed prefix overrides and signature-based inference.
- Accept a small LOC increase when it removes repetition and keeps behavior stable.
- For public static-only utility headers, prefer additive namespace functions plus compatibility wrappers before considering a breaking rewrite.
- Keep `EventListBuilder` using the namespace-first API where possible.
- Keep header-only classification logic header-only if the API stays small and self-contained.
- For wrappers returning types with forward declarations, keep the wrapper definitions out of public headers.
- If a header/source pair only carries one function plus two small structs, prefer folding it into the main module surface and leaving a shim include.

## 10. Stop conditions
- stop after the current verified loop
- do not broaden beyond `SystematicsCacheBuilder` in this pass
