# INVARIANTS

This file records the repository's hard constraints.

It is not a backlog.
It is not a second `VISION.md`.
It is not a restatement of every style preference from `DESIGN.md`.

Treat every item below as "do not change by accident."
Unless an active ExecPlan explicitly approves an exception, these rules hold.

## Document precedence

- `DESIGN.md` is the source of truth for current architecture and coding style.
- `INVARIANTS.md` records the stability contract around that design.
- `VISION.md` describes the intended future state, not today's default rules.
- `.agent/current_execplan.md` is where temporary exceptions, migration notes,
  and acceptance criteria must be written before an invariant is broken on
  purpose.
- `docs/minimality-log.md` records what actually became smaller, flatter, or
  easier to grep.

If these documents disagree, do not guess.
Follow `DESIGN.md` for current code shape and use the active ExecPlan for any
approved exception.

## External surface invariants

- CMake is the source of truth for configure, build, install, and exported
  package behavior.
- The supported build path is out-of-source:
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
  then
  `cmake --build build --parallel`
- Extra local build trees belong under `.build/`, not as new root-level
  `build-*` or `cmake-build-*` directories.
- The repository remains a small C++17 / ROOT project with SQLite-backed run
  database support.
- Installed library targets remain stable by default:
  `IO`, `Ana`, `Syst`, `Plot`, `Fit`.
- Installed downstream package targets remain under the `amarantin::`
  namespace.
- Installed public headers remain under
  `${CMAKE_INSTALL_INCLUDEDIR}/amarantin`.
- Documented executables remain available by default:
  `mk_sample`, `mk_dataset`, `mk_eventlist`, `mk_dist`, `mk_fit`, `mk_cov`.
- External behavior is preserved by default. A break needs an explicit
  migration note in the active ExecPlan and matching doc updates in the same
  milestone.
- CLI evolution is additive-first. Do not silently repurpose or remove a
  documented flag, positional form, or workflow without an explicit migration
  plan.

## Module boundary invariants

- `io/` owns persistence only.
- `io/` may open files, read objects, write objects, and define on-disk
  layout. It does not own selection logic, physics logic, systematic policy,
  fit logic, or workflow orchestration.
- `ana/` owns build-time analysis transforms, sample definitions, selections,
  and event-list construction.
- `syst/` owns systematic calculations and cache construction.
- `plot/` owns rendering only.
- `fit/` owns fit-side assembly and signal-strength fitting logic.
- `app/` owns CLI parsing and workflow orchestration.
- `tools/` stays short, direct shell glue. It must not become a hidden home
  for physics policy, persistence rules, or long-lived workflow logic.
- `samples-dag.mk` and `datasets.mk` are workflow declarations, not a second
  implementation layer for library responsibilities.
- Analysis or systematics logic must not move back into `io/`.
- `plot/` must not become a generic utility bucket.

## Workflow and data invariants

- The canonical persisted ladder stays explicit:
  `SampleIO -> DatasetIO -> EventListIO -> DistributionIO -> plot/fit`
- `SampleIO` owns logical-sample identity, shard provenance, and the chosen
  normalization policy.
- Sample building must make an explicit normalization decision. Normalization
  must not be an accidental side effect of whichever metadata happened to be
  present.
- `DatasetIO` assembles already-logical samples into one analysis scope. It is
  not the preferred place to reconstruct shard fan-in later.
- `EventListIO` is the row-wise build and debug surface.
- `DistributionIO` is the default persisted bin-wise downstream surface.
- Downstream code should usually work from `EventListIO` or
  `DistributionIO`, not reopen earlier workflow layers without a specific
  reason.
- Downstream code should usually use logical sample keys, not shard names.
- If a required normalization entry is missing, fail loudly rather than
  silently inventing a fallback weight.
- Debug, compatibility, or exploratory side paths may exist, but they must
  stay clearly secondary to the documented normal workflow.

## Code-shape invariants

- Prefer plain data and namespace functions over manager, service, provider,
  facade, or builder layers.
- Add abstractions only when they delete more complexity than they add.
- Keep workflows in `app/`.
- Keep module layout flat. Public headers and their main `.cc` files stay in
  the module root, with a small `bits/` directory only for shared private
  helpers.
- Favor grepable control flow over wrapper stacks and helper ceremony.
- Do not introduce rename churn, compatibility scaffolding, or drive-by
  formatting as part of an unrelated pass.
- After every feature or refactor pass, do a deletion pass: remove stale
  wrappers, dead includes, obsolete docs, dead options, unread payload fields,
  and unused build scaffolding.

## Documentation invariants

- `COMMANDS`, `INSTALL`, `USAGE`, CLI help text, and module READMEs should
  describe the same canonical build and execution path.
- The normal teaching path should match the actual preferred execution path.
- If a debug or compatibility path remains, document it as a side path rather
  than letting it compete with the main story.
- When the public contract changes, update the docs in the same milestone.
- This file should stay short and contractual. Design rationale belongs in
  `DESIGN.md`; future target state belongs in `VISION.md`.

## Verification invariants

- Non-trivial refactors use `.agent/current_execplan.md` and
  `docs/minimality-log.md`.
- Work one milestone at a time.
- After each milestone, run the relevant verification before continuing.
- The default verification baseline remains:
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
  `cmake --build build --parallel`
- When scope is local, use focused target builds and shell or CLI smoke checks
  instead of skipping verification entirely.
- A simplification pass is not done until the active milestone is complete or
  explicitly deferred, the relevant verification has run, and the minimality
  log records what actually got smaller or simpler.

## How to break an invariant on purpose

1. Name the exception explicitly in `.agent/current_execplan.md` before
   editing code.
2. State the reason, compatibility impact, migration note, and acceptance
   criteria.
3. Keep the exception scoped to one milestone.
4. Update `INVARIANTS.md` and/or `DESIGN.md` once the repository accepts the
   new rule as permanent.
