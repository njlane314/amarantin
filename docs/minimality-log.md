# Minimality Log

## Latest pass
- status: done
- subsystem: event-list boundary
- design rule from `DESIGN.md`: downstream code should usually open `EventListIO` and stay on that surface

### Reductions
- moved detector-sibling lookup from `ana/SampleDef.hh` onto [`EventListIO.hh`](/Users/user/programs/amarantin/io/EventListIO.hh)
- removed the `syst -> ana` dependency for detector sibling discovery
- stopped installing [`ana/Cuts.hh`](/Users/user/programs/amarantin/ana/Cuts.hh) and [`ana/Channels.hh`](/Users/user/programs/amarantin/ana/Channels.hh) as public `Ana` headers
- flattened [`ana/EventListBuild.hh`](/Users/user/programs/amarantin/ana/EventListBuild.hh) so it carries plain build fields instead of exposing `cuts::Config`
- made plotting helpers explicitly `EventListIO`-first in [`plot/EventListPlotting.hh`](/Users/user/programs/amarantin/plot/EventListPlotting.hh) and [`plot/EventDisplay.hh`](/Users/user/programs/amarantin/plot/EventDisplay.hh)

## Current milestone
- status: done
- subsystem: library layout
- design rule from `DESIGN.md`: keep module layout flat

## What changed
- moved public headers and their main `.cc` files out of per-library `include/` and `src/` directories and into each module root
- kept only `io/bits/` as the shared private helper area
- updated CMake include paths so each library exports its module root directly
- moved snapshot export out of `io/` and into `ana/`
- `IO` no longer builds or installs [`Snapshot.hh`](/Users/user/programs/amarantin/ana/Snapshot.hh)
- `Ana` now owns [`Snapshot.hh`](/Users/user/programs/amarantin/ana/Snapshot.hh) and [`Snapshot.cc`](/Users/user/programs/amarantin/ana/Snapshot.cc)
- updated the architecture docs so they describe snapshots as `ana` functionality instead of `io` functionality

## Why this is simpler
- `rg EventListIO` or `rg Systematics` now lands on the public header and main implementation in the same directory
- each library root describes the real public surface directly instead of splitting it across two parallel trees
- `bits/` now means one thing: shared private helper code
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
- wrappers removed: `SnapshotService`, `EventListSelection`, and `AnalysisChannels`
- shell branches removed:
- docs/build artifacts removed: 0
- approximate LOC delta: net neutral in code size, but sharper module ownership

## Decisions
- treat snapshot/export as `ana`, not `io`
- accept file moves when they make the module boundary materially clearer
- prefer flat module roots over `include/` + `src/` splits for this repo size
- keep `bits/` only for shared private helpers, not as a second public surface
- prefer the tighter `EventListBuild`, `Cuts`, `Channels`, and `Snapshot` names in `ana/`

## Remaining hotspots
