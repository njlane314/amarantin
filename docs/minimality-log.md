# Minimality Log

## Current milestone
- status: done
- subsystem: `syst/`
- design rule from `DESIGN.md`: add abstractions only when they delete complexity

## What changed
- folded `SystematicsCacheBuilder` into `Systematics`
- moved `CacheRequest`, `CacheBuildOptions`, and `build_systematics_cache(...)` into [`syst/include/Systematics.hh`](/Users/user/programs/amarantin/syst/include/Systematics.hh)
- moved the implementation into [`syst/src/Systematics.cc`](/Users/user/programs/amarantin/syst/src/Systematics.cc)
- reduced [`syst/include/SystematicsCacheBuilder.hh`](/Users/user/programs/amarantin/syst/include/SystematicsCacheBuilder.hh) to a compatibility shim that just includes `Systematics.hh`
- removed [`syst/src/SystematicsCacheBuilder.cc`](/Users/user/programs/amarantin/syst/src/SystematicsCacheBuilder.cc) from the build

## Why this is simpler
- the preferred API shape now matches the repo rule of plain data plus namespace functions
- `SystematicsCacheBuilder` was not a real second subsystem; now the cache-building surface lives with the rest of the systematics API
- include compatibility is preserved, so the fold does not force a migration
- one compiled source file is gone and the public surface is flatter

## Verification
- configure/build commands:
- `docker run --rm -v "$PWD":/work -w /work amarantin-dev bash -lc 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target Syst Plot mk_eventlist --parallel && bash tools/run-macro cache_systematics && ./build/bin/mk_eventlist --help || true'`
- target-only commands:
- shell checks:
- smoke checks:
  - `bash tools/run-macro cache_systematics`
  - `mk_eventlist --help`
- results:
  - `Syst`, `Plot`, and `mk_eventlist` built successfully in Docker
  - macro smoke check passed and reached the entry point:
    `Processing plot/macro/cache_systematics.C...`
    `cache_systematics: read_path is required`
  - CLI smoke check passed and printed usage

## Reduction ledger
- files deleted: 1 (`syst/src/SystematicsCacheBuilder.cc`)
- wrappers removed: `SystematicsCacheBuilder` no longer exists as a separate compiled layer
- shell branches removed:
- docs/build artifacts removed: 0
- approximate LOC delta: `SystematicsCacheBuilder` folded into `Systematics`, with one source file deleted and one header reduced to a shim

## Decisions
- keep `SystematicsCacheBuilder.hh` as a compatibility shim
- prefer `Systematics.hh` as the main public entry point for `syst/`
- accept an additive public API when it reduces repetition and preserves behavior

## Remaining hotspots
- `io/include/SnapshotService.hh`
