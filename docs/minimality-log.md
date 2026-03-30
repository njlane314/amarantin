# Minimality Log

## Current milestone
- status: done
- subsystem: `tools/run-macro`
- design rule from `DESIGN.md`: add abstractions only when they delete
  complexity

## What changed
- removed the `mapfile` dependency from [`tools/run-macro`](/Users/user/programs/amarantin/tools/run-macro) so argument-kind loading works on the Bash shipped on this machine
- collapsed typed-literal matching behind one shared helper instead of three one-off matcher functions
- replaced the prefixed-literal regex ladder with one `case` dispatch and removed the manual argument-join loop
- rewrote the current pass plan around the tool-only milestone and kept the public hotspot audit for [`ana/Snapshot.hh`](/Users/user/programs/amarantin/ana/Snapshot.hh), [`io/EventListIO.hh`](/Users/user/programs/amarantin/io/EventListIO.hh), and [`syst/Systematics.hh`](/Users/user/programs/amarantin/syst/Systematics.hh) as read-only follow-up work

## Why this is simpler
- the highest-value low-risk work is concentrated in one non-installed script
- the wrapper no longer depends on a Bash 4 builtin just to collect inferred argument kinds
- one literal matcher now serves both inferred and prefixed coercion paths
- the invocation builder has less per-argument shell ceremony and fewer repeated branches

## Verification
- configure/build commands:
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- target-only commands:
- not run; configure could not complete without `ROOT`
- shell checks:
- `bash -n tools/mklist.sh tools/run-macro tools/overnight-minimality-pass.sh`
- smoke checks:
- stubbed `root` runs:
  - `bash tools/run-macro plot_event_display build/eventlists/run1.root beam-s0 0 U detector`
  - `bash tools/run-macro cache_systematics build/eventlists/run1.root beam-s0 topological_score 50 0 1 __pass_muon__ 400 beam-s0-sce,beam-s0-wiremodx true false false build/output.dists.root`
  - `bash tools/run-macro print_eventlist s:build/eventlists/run1.root`
- CLI smoke attempts:
  - `build/bin/mk_sample --help`
  - `build/bin/mk_dataset --help || true`
  - `build/bin/mk_eventlist --help || true`
- results:
  - shell syntax checks passed
  - stubbed macro invocations passed and showed inferred `int` and `bool` arguments staying unquoted again
  - host configure failed because `ROOT` is not installed or not discoverable on `PATH`
  - Docker verification could not be used because the sandbox cannot access the Docker daemon socket
  - existing `build/bin` programs are Linux ELF binaries from another environment, so the requested CLI smoke checks could not execute on this macOS host

## Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed:
  - `mapfile` load path
  - three separate literal matcher helpers
  - the prefixed-literal regex ladder
  - the manual join loop
- docs/build artifacts removed: 0
- approximate LOC delta: `git diff --stat` for this pass is `153 insertions`,
  `313 deletions`

## Decisions
- fix the real `mapfile` portability failure in `tools/run-macro` before
  considering deeper hotspot cleanup
- keep installed headers and build surfaces unchanged in this pass
- stop after the tool-only milestone because further audited work now pushes
  toward public-surface changes or needs a working ROOT build environment

## Remaining hotspots
- `ana/Snapshot.hh`
- `io/EventListIO.hh`
- `syst/Systematics.hh`
