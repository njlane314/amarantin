# TARGET

This file describes the intended end state for `amarantin`.

It is not a task list and it is not the day-to-day design source of truth.
`DESIGN.md` governs changes now. This file explains what the repository should
converge toward over time.

## Purpose

`amarantin` should be a small, direct C++ / ROOT toolkit for turning source
ROOT inputs into a few stable persisted analysis surfaces that downstream code
can read without dragging the whole workflow stack along.

The core path should stay easy to explain:

1. assemble source files into `SampleIO`
2. assemble logical samples into `DatasetIO`
3. build selected row-wise content into `EventListIO`
4. optionally build fine bin-wise caches in `DistributionIO`
5. assemble final fit / plot regions in `ChannelIO`
6. render or fit from those persisted outputs

The project should optimize for clear data flow, small public surfaces, and
grep-friendly implementation, not for maximum abstraction.

## Target Workflow

The preferred user-facing workflow is:

1. `mk_sample` builds shard-level `SampleIO` files from plain text file lists
2. `mk_dataset` assembles logical analysis samples and stamps workflow metadata
3. `mk_eventlist` turns `DatasetIO` into selected `EventListIO` content
4. `syst/` or thin apps build optional `DistributionIO` caches from
   `EventListIO`
5. `mk_channel` or similarly thin app-level code assembles final `ChannelIO`
6. `plot/` renders persisted outputs and `fit/` profiles persisted channels

Production shard naming should stay in the sample-build layer. Downstream
analysis should usually use logical sample keys, not shard keys.

If two adjacent steps can be flattened without hiding the data flow or
blurring module boundaries, prefer the flatter path.

## Target Boundaries

### `io/`

`io/` should own persistence only:

- ROOT object layout
- read / write code
- compatibility with persisted files
- plain metadata needed to describe stored content

`io/` should not own:

- physics selection logic
- systematic-evaluation policy
- fit policy
- rendering behavior

### `ana/`

`ana/` should own one-way build-time transforms from source-side persistence
into selected analysis content.

It should stay centered on a small number of obvious entry points, led by
`ana::build_event_list(...)`, plus closely related sample-definition and
snapshot-style helpers when they simplify the workflow.

It should not become a general downstream utility bucket.

### `syst/`

`syst/` should own bin-wise uncertainty evaluation and cache construction.

Its job is:

- read `EventListIO`
- compute systematic summaries
- return in-memory results or write `DistributionIO` payloads

It should not absorb selection-building logic from `ana/` or rendering logic
from `plot/`.

### `plot/`

`plot/` should stay rendering-only.

Its job is:

- read persisted rows, bins, or final channels
- build ROOT histograms, canvases, and images

It should not decide physics policy, mutate persistent schemas, or become a
generic helper layer for unrelated modules.

### `fit/`

`fit/` should own final region-model assembly helpers and signal-strength
fitting from stable persisted inputs, especially `ChannelIO`.

It should stay small:

- plain data
- free functions
- thin orchestration

If adaptive or final-stage rebinning exists, it should live here or in thin
app code, not in `io/`.

### `app/`

`app/` should own CLI orchestration.

Applications should compose library pieces directly and stay thin enough that
the workflow is obvious from one read.

### `tools/`

`tools/` should stay a short shell-helper layer.

It should not become a second application framework or hide important workflow
decisions behind shell branching.

## Desired Public Shape

By default, the repository should preserve a small stable external surface:

- installed libraries: `IO`, `Ana`, `Syst`, `Plot`, `Fit`
- command-line tools: `mk_sample`, `mk_dataset`, `mk_eventlist`,
  `mk_channel`, `mk_xsec_fit`
- plain-text workflow inputs where possible
- installed public headers that map cleanly onto the real module boundaries

Surface evolution should usually be additive first. Public churn should need an
explicit reason, not just internal cleanup momentum.

## What Should Shrink Over Time

The codebase should continue converging toward:

- fewer wrapper layers that only hide one function call
- fewer duplicated metadata surfaces across scripts, makefiles, and persisted
  objects
- less shell branching and ad hoc inference in `tools/`
- fewer stale compatibility shims after migrations settle
- less cross-module helper leakage
- flatter workflows that a new reader can follow with a few greps

## Non-Goals

`amarantin` should not become:

- a generic plugin framework
- a service / manager / factory heavy architecture
- a second configuration language when plain text and CLI arguments are enough
- a place where `io/` absorbs workflow logic
- a place where `plot/` or `fit/` quietly take over selection building
- a place where production-shard details leak into the normal downstream
  analysis interface

## Relationship To Other Docs

- `DESIGN.md` is the present-tense source of truth for architecture rules
- `TARGET.md` describes the future end state and convergence direction
- `.agent/current_execplan.md` breaks one approved change into milestones
- `docs/minimality-log.md` records what actually became smaller, flatter, or
  easier to grep

When a target-state rule becomes true and stable, it should usually move into
`DESIGN.md` and become normal policy instead of staying here as a future note.
