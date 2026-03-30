# ExecPlan

## 1. Objective
Make `tools/run-macro` smaller, flatter, and more portable by removing the
`mapfile` dependency, collapsing duplicated literal-coercion branches, and
preserving the documented invocation grammar.

## 2. Constraints
- Preserve the supported invocation shape documented in `COMMANDS` and `USAGE`.
- Do not change installed headers, installed targets, or root `CMake` surfaces.
- Keep `io/`, `ana/`, `syst/`, and `plot/` behavior unchanged in this pass.
- Treat `ana/Snapshot.hh`, `io/EventListIO.hh`, and `syst/Systematics.hh` as
  audit-only unless a private-side simplification avoids public-surface edits.
- Leave unrelated worktree changes untouched.

## 3. Design anchor
From `DESIGN.md`:
- keep workflows direct and cheap to change
- add abstractions only when they delete complexity
- keep module boundaries sharp
- do a small deletion pass after each feature pass

For this pass, that means preferring a concentrated `tools/` simplification over
speculative public-header churn.

## 4. System map
- `tools/run-macro`
- `.agent/PLANS.md`
- `.agent/current_execplan.md`
- `docs/minimality-log.md`
- verification commands:
  - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
  - `cmake --build build --parallel`
  - `cmake --build build --target IO Ana Syst Plot mk_sample mk_dataset mk_eventlist`
  - `bash -n tools/mklist.sh tools/run-macro tools/overnight-minimality-pass.sh`
  - `build/bin/mk_sample --help`
  - `build/bin/mk_dataset --help || true`
  - `build/bin/mk_eventlist --help || true`

## 5. Candidate simplifications

### script simplification
- `tools/run-macro`: score `5.0`
  - simplicity gain: high
  - regression risk: low
  - reason: concentrated non-installed shell complexity, plus a real Bash 3
    portability bug from `mapfile` that currently breaks type inference

### wrapper collapse
- private cleanup behind `EventListIO` / `Snapshot` / `Systematics`: score `1.0`
  - simplicity gain: moderate
  - regression risk: moderate to high
  - reason: useful later, but the hotspot headers define downstream contracts

### boundary sharpening
- keep the public hotspot audit in the ledger, but do not edit installed
  surfaces in this pass

### doc / build cleanup
- update only the tracking files required for this milestone

### stale scaffolding
- no separate stale-scaffolding milestone unless verification exposes a local
  deletion in touched files

## 6. Milestones

### Milestone 1
- status: done
- hypothesis: replacing `mapfile` with a direct Bash loop and folding literal
  matching into shared helpers will make `tools/run-macro` shorter to audit,
  more portable, and closer to its documented typed-argument behavior
- files / symbols touched:
  - `tools/run-macro`
  - `.agent/current_execplan.md`
  - `docs/minimality-log.md`
- expected behavior risk: low
- verification commands:
  - `bash -n tools/mklist.sh tools/run-macro tools/overnight-minimality-pass.sh`
  - stubbed `root` invocation checks for representative macros
  - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
  - `cmake --build build --parallel`
  - `cmake --build build --target IO Ana Syst Plot mk_sample mk_dataset mk_eventlist`
  - `build/bin/mk_sample --help`
  - `build/bin/mk_dataset --help || true`
  - `build/bin/mk_eventlist --help || true`
- acceptance criteria:
  - no `mapfile` dependency remains in `tools/run-macro`
  - representative numeric and boolean arguments stay unquoted in the generated
    ROOT invocation
  - shell checks pass
  - requested configure/build/smoke verification is attempted and any failures
    are repaired or recorded

## 7. Public-surface check
- compatibility impact: none intended; this pass stays inside `tools/` plus the
  required tracking docs
- migration note: non-goal; keep current macro wrapper invocation shape
- reviewer sign-off: not required because no installed header, CLI executable,
  or build surface changes are planned

## 8. Reduction ledger
- files deleted: 0
- wrappers removed: 0
- shell branches removed:
  - removed the `mapfile` dependency from argument-kind loading
  - collapsed three standalone literal matcher helpers into one shared matcher
  - replaced the prefixed-literal regex cascade with one `case` dispatch
  - replaced the manual argument join loop with one formatted join
- stale docs removed: 0
- targets or dependencies removed: 0
- approximate LOC delta: `git diff --stat` for this pass is `153 insertions`,
  `313 deletions`; the code change in `tools/run-macro` is net small but
  flatter in control flow

## 9. Decision log
- Prioritize the `tools/run-macro` portability bug over broader C++ cleanup.
- Treat `ana/Snapshot.hh`, `io/EventListIO.hh`, and `syst/Systematics.hh` as
  current audit hotspots, not edit targets.
- Keep the milestone fully inside non-installed code unless verification forces
  a follow-up fix.
- Stop after the tool-only milestone because the remaining audited hotspots are
  public-surface work and the requested build verification is environment-limited.

## 10. Stop conditions
- Stop after this verified loop unless another non-installed simplification has
  clearly comparable payoff and low risk.
- Stop if the remaining work is mostly public-API churn, formatting, or broad
  reorganization.
