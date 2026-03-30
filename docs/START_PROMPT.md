Read `DESIGN.md`, `COMMANDS`, `INSTALL`, `USAGE`, the root `CMakeLists.txt`, and the relevant subdirectory `CMakeLists.txt` files before editing.

Goal:
Make `amarantin` materially smaller, flatter, more direct, and easier to grep, while preserving behavior and public installed surfaces by default.

Ground rules:
- follow `DESIGN.md` literally
- keep `io/` persistence-only
- keep workflows in `app/`
- prefer deletion, deduplication, and namespace functions over new abstractions
- do not do drive-by formatting
- do not change installed headers or build-system surfaces without explicit approval

Process:
1. Create or update `.agent/current_execplan.md` from `.agent/PLANS.md`.
2. Create or update `docs/minimality-log.md` from `docs/minimality-log.template.md`.
3. Inventory simplification opportunities in this priority order:
   - shell complexity in `tools/`, especially `tools/run-macro`
   - internal wrapper collapse and boundary sharpening
   - public utility-class simplification only if a migration is explicitly approved
   - stale docs, includes, and helper scaffolding
4. Rank candidates by simplicity gain divided by regression risk.
5. Pick the highest-value low-risk milestone.
6. If the milestone touches public headers, CLI semantics, or build files, get a read-only critique before editing.
7. Implement the milestone end to end.
8. Run relevant verification:
   - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
   - `cmake --build build --parallel`
   - relevant target-only builds
   - `bash -n tools/mklist.sh tools/run-macro tools/overnight-minimality-pass.sh`
   - relevant CLI smoke checks
9. Repair failures before continuing.
10. Update the ExecPlan and `docs/minimality-log.md`.
11. Continue milestone by milestone until the remaining work is low-value or too risky.

Important hotspots to audit:
- `tools/run-macro`
- `ana/Snapshot.hh`
- `io/EventListIO.hh`
- `syst/Systematics.hh`

Final deliverable:
- code changes
- passing verification
- updated `.agent/current_execplan.md`
- updated `docs/minimality-log.md`
- a concise report on what became smaller, flatter, or easier to grep
