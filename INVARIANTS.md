# INVARIANTS

This file records what must stay true while `amarantin` converges toward
`VISION.md`.

It is not a task list. It is not a second design document. It is the stability
contract for simplification and workflow changes.

## Document roles

- `DESIGN.md` is the source of truth for current architecture and style.
- `INVARIANTS.md` records what must not change by accident.
- `VISION.md` describes the intended end state.
- `.agent/current_execplan.md` describes the current milestone and any approved
  exceptions.
- `docs/minimality-log.md` records what actually became smaller, flatter, or
  easier to grep.

If these documents disagree, follow `DESIGN.md` for the current codebase until
an explicit plan updates the repository's accepted rules.

Unless an ExecPlan explicitly approves a change, treat the items below as
non-negotiable.

## Public surface invariants

- CMake is the source of truth for configure, build, and install behavior.
- The supported build path is out-of-source:
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
  followed by `cmake --build build --parallel`.
- Extra local build trees belong under `.build/`, not as new root-level
  `build-*` or `cmake-build-*` directories.
- The repository remains a small C++17 / ROOT project with SQLite-backed run
  database support.
- Installed library targets remain stable by default:
  `IO`, `Ana`, `Syst`, `Plot`, `Fit`.
- Installed downstream CMake package targets remain under the
  `amarantin::` namespace.
- Installed public headers remain under
  `${CMAKE_INSTALL_INCLUDEDIR}/amarantin`.
- Existing documented executables remain available by default:
  `mk_sample`, `mk_dataset`, `mk_eventlist`, `mk_dist`, `mk_fit`,
  `mk_cov`.
- CLI evolution is additive-first. Add the new path before removing or
  repurposing documented old flags, positional forms, or workflows.
- External behavior is preserved by default. Any approved break needs an
  explicit migration note in the active ExecPlan and matching doc updates.

## Module boundary invariants

- `io/` owns persistence only.
- `io/` may open files, read objects, write objects, and define on-disk
  layout. It does not own selection logic, physics logic, systematic policy,
  or workflow orchestration.
- `ana/` owns build-time analysis transforms, sample definitions, selections,
  and event-list construction.
- `syst/` owns systematic calculations and cache construction.
- `plot/` owns rendering only.
- `fit/` owns fit-side assembly and signal-strength fit helpers.
- `app/` owns CLI parsing and workflow orchestration.
- `tools/` stays short, direct shell glue. It must not become the hidden home
  for analysis policy or persistence rules.
- `samples-dag.mk` and `datasets.mk` are workflow declarations, not a place to
  re-implement library responsibilities.
- Analysis or systematics logic must not move back into `io/`.
- `plot/` must not become a generic utility bucket.

## Workflow and data-contract invariants

- The canonical persisted ladder stays explicit:
  `SampleIO -> DatasetIO -> EventListIO -> DistributionIO -> plot/fit`.
- `SampleIO` owns logical-sample identity, shard provenance, and the chosen
  normalization policy.
- Sample building must make an explicit normalization decision. Normalization
  must not be an accidental side effect of whichever metadata happened to be
  present.
- `DatasetIO` assembles already-logical samples into one analysis scope. It is
  not the preferred long-term place where shard fan-in is invented or
  reconstructed.
- `EventListIO` is the row-wise build and debug surface.
- `DistributionIO` is the default persisted bin-wise downstream surface.
- Downstream code should usually use logical sample keys, not shard names.
- Final or publication-style downstream work should normally use persisted
  caches or final assembly objects, not reopen earlier workflow layers.
- If a required normalization entry is missing, the correct default is to fail
  loudly rather than silently inventing a fallback weight.
- Debug side paths may exist, but they must stay clearly secondary to the
  normal documented workflow.

## Simplicity invariants

- Prefer plain data and namespace functions over service, manager, provider,
  facade, or builder layers.
- Add abstractions only when they delete more complexity than they add.
- Keep workflows in `app/`.
- Keep module layout flat. Public headers and their main `.cc` files stay in
  the module root, with a small `bits/` directory only for shared private
  helpers.
- Favor one grepable function over wrapper stacks and helper ceremony.
- Do not introduce rename churn or drive-by formatting as part of a
  simplification pass.
- After every feature or refactor pass, do a small deletion pass: remove stale
  wrappers, dead includes, obsolete docs, and unused build scaffolding.

## Documentation invariants

- `COMMANDS`, `INSTALL`, `USAGE`, CLI help text, and module READMEs should
  describe the same canonical build and execution path.
- The normal teaching path should match the actual preferred execution path.
- If a debug or compatibility path remains, document it as a side path rather
  than letting it compete with the main story.
- When the public contract changes, update the docs in the same milestone.

## Verification invariants

- Non-trivial refactors use `.agent/current_execplan.md` and
  `docs/minimality-log.md`.
- Work one milestone at a time.
- After each milestone, run the relevant verification before continuing.
- The default verification baseline remains:
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
  `cmake --build build --parallel`
- When scope is local, use focused target builds and shell or CLI smoke checks
  instead of skipping verification.
- A simplification pass is not done until the active milestone is complete or
  explicitly deferred, the relevant verification has run, and the minimality
  log records what got smaller or simpler.

## How to break an invariant on purpose

- Name the exception explicitly in `.agent/current_execplan.md` before editing
  code.
- State the compatibility impact, migration note, and acceptance criteria.
- Keep the exception scoped to one milestone.
- Update this file once the repository accepts the new rule as permanent.
