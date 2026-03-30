# Minimality Log

## Current milestone
- status: done
- subsystem: library layout
- design rule from `DESIGN.md`: keep module layout flat

## What changed
- moved public headers and their main `.cc` files out of per-library `include/` and `src/` directories and into each module root
- kept only `io/detail/` as the shared private helper area
- updated CMake include paths so each library exports its module root directly
- moved `SnapshotService` out of `io/` and into `ana/`
- `IO` no longer builds or installs [`SnapshotService.hh`](/Users/user/programs/amarantin/ana/SnapshotService.hh)
- `Ana` now owns [`SnapshotService.hh`](/Users/user/programs/amarantin/ana/SnapshotService.hh) and [`SnapshotService.cc`](/Users/user/programs/amarantin/ana/SnapshotService.cc)
- updated the architecture docs so they describe snapshots as `ana` functionality instead of `io` functionality

## Why this is simpler
- `rg EventListIO` or `rg Systematics` now lands on the public header and main implementation in the same directory
- each library root describes the real public surface directly instead of splitting it across two parallel trees
- `detail/` now means one thing: shared private helper code
- the `IO` library boundary now matches the repo rule instead of fighting it
- the root persistence library is more boring, which is exactly what it should be
- snapshot/export logic now sits beside the other analysis-side transformation code
- the public header name stays the same, so users do not need a source-level migration

## Verification
- configure/build commands:
- `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel && bash tools/run-macro mk_snapshot && ./build/bin/mk_eventlist --help || true'`
- `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IO Ana mk_eventlist --parallel && bash tools/run-macro mk_snapshot && ./build/bin/mk_eventlist --help || true'`
- target-only commands:
- shell checks:
- `bash -n tools/run-macro tools/overnight-minimality-pass.sh tools/mklist.sh`
- smoke checks:
  - `bash tools/run-macro mk_snapshot`
  - `mk_eventlist --help`
- results:
  - `IO`, `Ana`, and `mk_eventlist` built successfully in Docker
  - snapshot macro smoke check passed and reached the entry point:
    `Processing ana/macro/mk_snapshot.C...`
    `mk_snapshot: read_path is required`
  - CLI smoke check passed and printed usage
  - after the layout move, a full Docker build still passed and the macro/CLI smoke checks still worked

## Reduction ledger
- files deleted: 2 from `io/` (`io/include/SnapshotService.hh`, `io/src/SnapshotService.cc`)
- wrappers removed: `SnapshotService` is no longer an `IO` export
- shell branches removed:
- docs/build artifacts removed: 0
- approximate LOC delta: net neutral in code size, but sharper module ownership

## Decisions
- keep the public header name `SnapshotService.hh`
- treat snapshot/export as `ana`, not `io`
- accept file moves when they make the module boundary materially clearer
- prefer flat module roots over `include/` + `src/` splits for this repo size
- keep `detail/` only for shared private helpers, not as a second public surface

## Remaining hotspots
