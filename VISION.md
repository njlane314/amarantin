# VISION

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

The target downstream ladder should be explicit:

- `EventListIO` is the row-wise build/debug surface
- `DistributionIO` is the default persisted fine-bin cache surface
- `ChannelIO` is the default final plotting and fitting surface

Final or publication-style downstream work should not normally plot from
`EventListIO` directly. Direct `EventListIO` plotting is for exploration,
inspection, and debugging.

## Target Workflow

The preferred user-facing workflow is:

1. `mk_sample` builds shard-level `SampleIO` files from plain text file lists
2. `mk_dataset` assembles logical analysis samples and stamps workflow metadata
3. `mk_eventlist` turns `DatasetIO` into selected `EventListIO` content
4. `syst/` or thin apps build optional `DistributionIO` caches from
   `EventListIO`
5. native channel-building code assembles final `ChannelIO` regions from many
   cached processes plus automatic observed-data inputs
6. `plot/` renders final `ChannelIO` outputs and `fit/` profiles one or more
   persisted channels

Production shard naming should stay in the sample-build layer. Downstream
analysis should usually use logical sample keys, not shard keys.

If two adjacent steps can be flattened without hiding the data flow or
blurring module boundaries, prefer the flatter path.

The target final-region assembly should be reproducible from persisted inputs.
Manual channel construction or hand-entered observed bins may exist as
temporary bridges, but they are not the intended steady-state workflow.

## Stable Analysis Surfaces

### `EventListIO`

`EventListIO` should remain the stable row-wise analysis surface produced by
`ana`.

Its intended role is:

- selected event trees
- copied sample metadata
- framework-owned helper columns needed for downstream build steps
- debug, inspection, and export-oriented row-wise access

It is not the preferred final plotting surface.

### `DistributionIO`

`DistributionIO` should be the stable persisted fine-bin cache surface.

Its intended role is:

- one cached fine-bin observable per logical sample and cache request
- persisted nominal, statistical, detector, and universe-family payloads
- deterministic cache identity and reproducible provenance back to
  `EventListIO`

It is the default downstream cache surface for repeated region building and
final-channel assembly.

### `ChannelIO`

`ChannelIO` should be the stable final-region surface.

Its intended role is:

- one persisted final region on common binning
- many named processes, not just one signal plus one background
- automatic observed-data bins
- reproducible provenance back to cached inputs and channel-building policy

Final plotting and fitting should usually start from `ChannelIO`.

## Selection Policy

Named cut presets are part of the stable analysis policy.

That means:

- preset names should stay fixed and meaningful over time
- the selected preset name and resolved selection expression should be
  persisted with produced outputs
- downstream code should be able to tell which stable selection policy a file
  was built with

Thresholds and implementation details may evolve, but that evolution should be
explicit and reproducible rather than hidden behind ad hoc one-off CLI text.

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

- read persisted bins or final channels
- build ROOT histograms, canvases, and images

It should not decide physics policy, mutate persistent schemas, or become a
generic helper layer for unrelated modules.

The target final-plot path is through `ChannelIO`.
If `plot/` reads `EventListIO` directly, that should be for debug or
inspection-style views rather than the normal final-region workflow.

### `fit/`

`fit/` should own final region-model assembly helpers and signal-strength
fitting from stable persisted inputs, especially `ChannelIO`.

It should stay small:

- plain data
- free functions
- thin orchestration

If adaptive or final-stage rebinning exists, it should live here or in thin
app code, not in `io/`.

Its long-term scope includes:

- building final region models from one or more persisted channels
- combined or multi-region fits
- shared nuisance handling across regions when the underlying uncertainty is
  common

Fit-side policy may assemble regions and correlations, but persisted storage
ownership still belongs to `io/`.

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

Within that public shape, the intended steady-state native workflow is:

- `mk_sample`
- `mk_dataset`
- `mk_eventlist`
- one or more native cache / channel assembly entry points
- final plot and fit entry points that consume `ChannelIO`

The repository should converge toward native downstream assembly rather than
relying on manual macros or hand-entered final-region inputs.

## Binning Model

The intended persisted binning split is:

- `DistributionIO` stores the stable fine-bin cache on simple uniform binning
- `ChannelIO` may store final adaptive or variable-width binning

Adaptive or variable binning is in scope, but the binning policy should live
in downstream build code, not in `io/`.

`io/` may store the chosen edges and small reproducibility metadata, but it
should not own the adaptive-binning algorithm itself.

## Final Region Assembly

Final region assembly should be a first-class native workflow.

That means:

- building channels from many named processes
- grouping cached inputs into signal and multiple background components
- filling observed data automatically from persisted inputs
- recording enough provenance to reproduce the exact assembled region later

The steady-state channel-building surface should be able to express ordinary
analysis regions without falling back to hand-written CSV bins or
single-purpose macros.

## Combined Fits

Combined or multi-region fits are in scope for the target end state.

That implies:

- multiple `ChannelIO` regions may contribute to one fit problem
- nuisance parameters may be shared across regions when they describe a common
  source
- combined-fit assembly should stay explicit and grep-friendly rather than
  turning into a heavy configuration framework

## What Should Shrink Over Time

The codebase should continue converging toward:

- fewer wrapper layers that only hide one function call
- fewer duplicated metadata surfaces across scripts, makefiles, and persisted
  objects
- less shell branching and ad hoc inference in `tools/`
- fewer stale compatibility shims after migrations settle
- less cross-module helper leakage
- flatter workflows that a new reader can follow with a few greps
- less dependence on manual downstream assembly once stable native channel and
  fit workflows exist

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
- `VISION.md` describes the future end state and convergence direction
- `.agent/current_execplan.md` breaks one approved change into milestones
- `docs/minimality-log.md` records what actually became smaller, flatter, or
  easier to grep

When a target-state rule becomes true and stable, it should usually move into
`DESIGN.md` and become normal policy instead of staying here as a future note.
