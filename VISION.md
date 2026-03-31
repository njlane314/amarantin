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
4. optionally build and update fine bin-wise caches in `DistributionIO`
5. render or fit from persisted caches plus plain-text assembly manifests

The project should optimize for clear data flow, small public surfaces, and
grep-friendly implementation, not for maximum abstraction.

The target downstream ladder should be explicit:

- `EventListIO` is the row-wise build/debug surface
- `DistributionIO` is the default persisted fine-bin cache and downstream
  plotting / fitting surface

Final or publication-style downstream work should not normally plot from
`EventListIO` directly. Direct `EventListIO` plotting is for exploration,
inspection, and debugging.

## Target Workflow

The preferred user-facing workflow is:

1. `mk_sample` builds one logical `SampleIO` from one or more shard inputs
2. `mk_dataset` assembles one `DatasetIO` from logical sample files
3. `mk_eventlist` turns `DatasetIO` into selected `EventListIO` content
4. `mk_dist` or thin apps build optional `DistributionIO` caches from
   `EventListIO`
5. `plot/`, `fit/`, or thin apps assemble named processes, runs, and regions
   from cached `DistributionIO` inputs using plain-text manifests
6. `plot/` renders final views and `fit/` profiles one or more assembled
   region models

The preferred explanation path should match that execution path. A new reader
should be able to follow one concrete ladder from file list to final plot
without switching between separate "debug" and "production" stories midway
through the docs or codebase.

Exploration-oriented `EventListIO` plotting may remain as a useful debug path,
but it should be described as a side path. The normal teaching path and the
normal final-analysis path should converge on:

- plain text file lists
- `SampleIO`
- `DatasetIO`
- `EventListIO`
- `DistributionIO`
- final plot or fit outputs

One `DistributionIO` file should be able to accumulate many cached requests
for different variables, regions, and run scopes. The steady-state workflow
should not require one cache ROOT file per plotted variable.

Production shard naming should stay in the sample-build layer. Downstream
analysis should usually use logical sample keys, not shard keys.

`SampleIO` should stay a lightweight logical-sample persistence surface:

- one logical sample name such as `beam`, `dirt`, `ext`, or `signal`
- source ROOT file paths grouped from one or more shard inputs
- shard-level provenance retained for reproducibility
- run / subrun coverage and POT summaries
- sample metadata such as origin, variation, beam, and polarity
- normalisation derived from the subrun POT and optional run database

Sample building should always make an explicit normalisation decision.
That decision may differ by sample kind, but it should never be an accidental
side effect of whichever metadata happened to be present. The steady-state
workflow should make it clear whether a sample is:

- scaled from subrun POT and run-database POT
- kept at unit scale intentionally
- normalized by some other explicit sample-type policy

That chosen policy should be easy to inspect later from the persisted sample
surface or closely associated workflow metadata.

When one logical sample is assembled from multiple homogeneous inputs, the
implemented workflow should assume those inputs are pooled realizations used to
increase effective statistics while keeping one shared target exposure. Persist
the input-wise generated exposure and target-exposure inputs for auditability,
but compute the logical sample normalization from one shared target exposure,
not one target per input.

This is an explicit workflow assumption, not a fallback inference rule. If a
set of inputs would need additive target exposures instead, they should not be
treated as one ordinary multi-input logical sample under this path; they would
need a different higher-level combination surface or separate sample
identities.

Because pooled realizations may cover different run / subrun subsets, the
steady-state normalization surface cannot rely only on one sample-wide scalar.
The expected contract for this workflow is:

- each shard persists generated exposure by run / subrun
- the run database provides target exposure by run / subrun
- the logical sample sums shard generated exposure by run / subrun
- final event normalization is `target(run,subrun) / pooled_generated(run,subrun)`
- if an event has no matching normalization entry, fail

That is the explicit design assumption for this workflow. Shard-level
normalization bookkeeping may still be useful as an intermediate input
surface, but it is not the final event-weighting contract by itself.
Downstream nominal event weighting should recover the final normalization from
the logical sample's aggregated run / subrun map rather than carrying forward
independent shard-wide scales as if each shard represented the full target
exposure on its own.

It should not be the stage that copies or selects event trees into a new
analysis surface. That heavier materialisation step belongs downstream in
`mk_eventlist` and `EventListIO`.

For the target workflow, `mk_sample` should own shard fan-in for one logical
sample type. If a beam logical sample is made of shards such as `s0`, `s1`,
`s2`, and `s3`, the steady-state native path should be one `mk_sample`
invocation that records those shard inputs and produces one logical
`beam.root`, not four separate shard-named `SampleIO` artifacts that must be
merged later by `mk_dataset`.

For that multi-shard path, a plain-text manifest should be the preferred
native input surface. Positional shard arguments may still exist as a small
convenience mode for hand-run cases, but the normal workflow should point to a
checked-in or generated manifest that lists the shard inputs for one logical
sample. That keeps shard membership explicit, reproducible, and easy to diff.
The manifest should prefer labelled rows over path-only rows so shard identity
is persisted directly rather than inferred later from path conventions. Those
labels only need to be unique within one manifest. They should be clear local
identifiers for the logical sample build, not leaked production path tokens or
repo-global names.

The logical sample name should also be explicit rather than inferred from the
output path. `mk_sample` should accept a direct sample input such as
`beam`, `dirt`, `ext`, or `signal` so the persisted identity is a first-class
workflow decision rather than a filename convention.

The preferred native CLI shape for this path is:

`mk_sample [--run-db PATH] --sample NAME --manifest SAMPLE.manifest output.root origin variation beam polarity`

The initial manifest should stay simple. Each non-comment row should be:

`shard sample-list-path`

For example:

`shard01 samplelists/numi_fhc_run1/beam-s0.list`

`shard02 samplelists/numi_fhc_run1/beam-s1.list`

`shard03 samplelists/numi_fhc_run1/beam-s2.list`

`shard04 samplelists/numi_fhc_run1/beam-s3.list`

Version 1 should not depend on per-row metadata overrides. The manifest is
only for naming the shard inputs to one logical sample build.

The preferred persisted `SampleIO` shape for this path is:

- top-level logical sample identity and metadata:
  - `sample`
  - `origin`
  - `variation`
  - `beam`
  - `polarity`
  - chosen normalization mode / policy
- aggregate logical-sample inputs and summaries:
  - pooled source ROOT file list
  - aggregate generated exposure summaries
  - aggregate target-exposure summaries from the run database
- one logical run / subrun normalization table:
  - `run`
  - `subrun`
  - pooled generated exposure
  - target exposure from the database
  - resolved normalization for that run / subrun
- shard provenance records:
  - `shard`
  - source sample-list path
  - shard source ROOT file list
  - shard generated exposure by run / subrun

That run / subrun normalization table is the authoritative nominal-weighting
surface for the logical sample. A sample-wide scalar may still exist as a
small summary value when it is mathematically meaningful, but downstream event
weighting should not depend on that scalar being sufficient.

Correspondingly, `DatasetIO` should sit one level higher: it should assemble
already-logical `SampleIO` artifacts for one run / beam / polarity / campaign
scope rather than being the first place where shard-level fan-in happens.
Its normal inputs should be logical sample files such as:

- `beam.root`
- `dirt.root`
- `ext.root`
- `signal.root`
- detector-variation logical samples such as `beam-sce.root`

The preferred native CLI shape for this path is:

`mk_dataset [--defs PATH] [--campaign NAME] --run RUN --beam BEAM --polarity POLARITY --manifest DATASET.manifest output.root`

The initial dataset manifest should stay simple. Each non-comment row should
be:

`sample sample-root-path`

For example:

`beam build/samples/run1_fhc/beam.root`

`dirt build/samples/run1_fhc/dirt.root`

`ext build/samples/run1_fhc/ext.root`

`signal build/samples/run1_fhc/signal.root`

`beam-sce build/samples/run1_fhc/beam-sce.root`

Version 1 should not depend on repeated sample rows, shard-level merge
behavior, or per-row metadata overrides. `mk_dataset` consumes already-logical
sample files and assembles one analysis scope from them.

The preferred persisted `DatasetIO` shape for this path is:

- top-level dataset scope:
  - `run`
  - `beam`
  - `polarity`
  - optional `campaign`
- one entry per logical sample name:
  - `sample`
  - source logical `SampleIO` path
  - workflow metadata such as nominal / tag / role / defname / campaign
  - resolved sample classification needed by downstream build code
- enough carried-forward sample detail to drive downstream chaining without
  reopening extra workflow layers:
  - pooled source ROOT file list
  - logical run / subrun normalization surface or a direct reference to it

`DatasetIO` should not redo shard fan-in, infer sample identity from shard
names, or become the first place where logical normalization is reconstructed.
It should collect already-resolved logical samples into one analysis scope and
make that scope easy for `mk_eventlist` to consume.

Dataset assembly should validate internal consistency of the included logical
samples against that explicit scope, but it should not require one universal hard-
coded sample set in order to exist. Different workflows may intentionally
build partial datasets for debugging, detector studies, or focused downstream
assembly. The stricter question of which sample types are required belongs to
the specific downstream workflow that consumes the dataset, not to `mk_dataset`
as a universal rule.

The preferred native CLI shape for `mk_eventlist` should be narrower than the
current tool:

`mk_eventlist [--event-tree NAME] output.root dataset.root`

The default event-list build should therefore be:

`mk_eventlist output.root dataset.root`

with the default assumptions:

- event tree: `EventSelectionFilter`
- subrun tree: hardcoded `SubRun`
- baseline selection: hardcoded `selected != 0`

The intended role of `mk_eventlist` is to build one canonical row-wise
surface for downstream use. It should not also own `DistributionIO`
cache-building, region definitions, or variable-specific histogram requests.
Those belong in the next step.

The preferred native CLI shape for `mk_dist` should be:

`mk_dist [--region EXPR] [--fine-nbins N] [--genie] [--flux] [--reint] [--no-overwrite] output.root eventlist.root var nbins xmin xmax`

The default interpretation for `mk_dist` should be:

- `var`: one observable or branch expression to histogram
- `nbins`, `xmin`, `xmax`: one uniform binning
- `--region`: an optional event-list row filter for this cached distribution
- one invocation fills that cached distribution for all logical samples in the
  input `EventListIO`

In this path, the region cut belongs to `mk_dist`, not to `mk_eventlist`.
`mk_eventlist` decides which rows exist in the event list at all. `mk_dist`
decides which persisted rows contribute to one cached distribution request.
One `EventListIO` file should therefore support many `mk_dist` requests with
different `var` and `region` choices without needing to rebuild the row-wise
surface each time. For one such request, `DistributionIO` should persist the
matching cache entries for every logical sample carried by the input
`EventListIO`, including data samples.

The minimal CLI above is a one-request form. The steady-state cache workflow
should also support batch request manifests or equivalent update mode so one
`DistributionIO` file can be populated efficiently with many variables and
regions rather than forcing one cache file per variable.

`mk_eventlist` should then consume that embedded logical normalization surface
directly. The intended event-weighting contract is:

- `mk_eventlist` reads the logical run / subrun normalization table from
  `DatasetIO`
- for each selected event, it resolves the event's run / subrun identity from
  the default event-branch names `run` and `subRun`
- it looks up the corresponding local normalization entry
- it uses that local normalization as the base nominal event weight
- it then applies the usual central-value physics weights on top
- if the event tree does not expose `run` and `subRun`, fail
- if the lookup has no matching normalization entry, fail

`EventListIO` should therefore persist `__w__` and `__w2__` as resolved
event-level products of the logical normalization map, not as outputs of a
sample-wide scalar assumption.

The intended validation contract for these build steps is:

- `mk_sample`
  - fail if `--sample` is empty
  - fail if the shard manifest is empty or contains duplicate `shard` names
  - fail if any manifest row cannot be parsed as `shard sample-list-path`
  - fail if any sample-list path is missing or unreadable
  - fail if any shard resolves to zero ROOT files
  - fail if generated exposure by run / subrun cannot be recovered for a shard
  - fail if the run database cannot provide target exposure for a required
    run / subrun
  - fail if pooled generated exposure for a required run / subrun is missing,
    non-finite, or non-positive
- `mk_dataset`
  - fail if `--run`, `--beam`, or `--polarity` is missing
  - fail if the dataset manifest is empty or contains duplicate `sample`
    names
  - fail if any manifest row cannot be parsed as `sample sample-root-path`
  - fail if any logical `SampleIO` path is missing or unreadable
  - fail if an input `SampleIO` does not carry the logical normalization
    surface required downstream
  - fail if an input sample's explicit scope metadata conflicts with the
    dataset scope
  - do not require one universal hard-coded sample set
- `mk_eventlist`
  - fail if `DatasetIO` does not contain the embedded logical normalization
    surface
  - fail if a requested event tree is missing for any sample
  - fail if the hardcoded `SubRun` tree is missing for any sample
  - fail if the event tree does not expose `run` and `subRun`
  - fail if an event's `(run, subRun)` lookup has no matching normalization
    entry
  - fail if the hardcoded baseline selection cannot be resolved
- `mk_dist`
  - fail if `var` is empty
  - fail if `nbins` is non-positive or if the histogram range is invalid
  - fail if `--region` cannot be resolved against the persisted event-list
    tree
  - fail if `EventListIO` contains no logical samples to cache
  - fail if enabled reweighting families need branches that are not present on
    a sample for which those families are expected
  - fail if a non-empty `DistributionIO` output file is incompatible with the
    input `EventListIO`

Downstream `syst/` should treat that resolved `EventListIO` weight surface as
authoritative. Its intended contract is:

- the nominal histogram is filled from `__w__`
- detector-variation samples carry their own resolved `__w__` values from the
  same logical-normalization rules
- reweighting families multiply on top of `__w__`, not instead of it
- `syst/` does not reconstruct logical normalization from `SampleIO`,
  `DatasetIO`, or the run database a second time

The current internal shard-provenance helper should keep the direct name that
matches its role: `ShardIO`. It represents one shard-level input list plus its
discovered provenance rather than a generic partition abstraction.

If two adjacent steps can be flattened without hiding the data flow or
blurring module boundaries, prefer the flatter path.

The target final-region assembly should be reproducible from persisted inputs.
Observed data should enter that path as an ordinary persisted sample, not as
hand-entered bin counts.

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

- one cache store containing many dataset-wide requests, with entries for all
  logical samples carried by the input `EventListIO`
- support for both per-run and combined-run downstream assembly from the same
  cached inputs
- persisted nominal, statistical, detector, and universe-family payloads
- deterministic cache identity and reproducible provenance back to
  `EventListIO`

The current generic child-object name `Entry` should eventually converge on a
more specific noun. `Spectrum` is the preferred direction: one sample-specific
cached binned result inside `DistributionIO`, with statistical and systematic
information for one request.

One `Spectrum` should be uniquely defined by:

- one atomic run scope:
  - `run`
  - `beam`
  - `polarity`
  - optional `campaign`
- `sample`
- `var`
- `region`
- binning
- systematic configuration

In other words, one dataset-wide cache request writes many `Spectrum`
objects, usually one per logical sample. Plotting and fitting should be able
to combine spectra across runs or samples only when those identity fields are
compatible in the intended way, rather than by guessing from filenames or
cache insertion order.

The primary cached unit should stay atomic in run scope. Combined-run views
should normally be assembled downstream from multiple compatible atomic
spectra rather than persisted as the first-class cache unit. If one
`DistributionIO` file stores spectra from several runs, that run scope should
still be explicit on every `Spectrum` rather than hidden in the filename or
an opaque combined label.

It is the default persisted downstream surface for repeated plotting and
fitting.

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

- read persisted bins from `DistributionIO`
- resolve semantic plot queries against cached spectra
- build ROOT histograms, canvases, and images

It should not decide physics policy, mutate persistent schemas, or become a
generic helper layer for unrelated modules.

The target final-plot path is through `DistributionIO`.
If `plot/` reads `EventListIO` directly, that should be for debug or
inspection-style views rather than the normal final-region workflow.
Thin macros may wrap the final plot path, but they should not be the only
discoverable or explainable way to render a final region.

For final plotting, macros and helpers should not expose raw cache keys.
They should ask for cached spectra through a small semantic query surface,
for example by:

- run scope
- `sample`
- `var`
- `region`
- optional binning when needed to disambiguate

That query should resolve to exactly one cached `Spectrum` or fail. The
hashed cache key remains an internal storage detail of `DistributionIO`,
not the public plotting interface.

For debug-oriented `EventListIO` plotting, the weight surface should stay
inspectable rather than opaque. Plotting helpers should be able to use and
expose:

- `__w_norm__` for resolved run / subrun normalization
- `__w_cv__` for central-value physics weighting
- `__w__` for the final nominal event weight

That keeps normalization debugging in the row-wise plot path without changing
the preferred final-plot path through `DistributionIO`.

### `fit/`

`fit/` should own final region-model assembly helpers and signal-strength
fitting from stable persisted inputs, especially `DistributionIO` and,
when useful, a fit-facing persisted `ModelIO`.

It should stay small:

- plain data
- free functions
- thin orchestration

If adaptive or final-stage rebinning exists, it should live here or in thin
app code, not in `io/`.

Its long-term scope includes:

- building final region models from one or more persisted cache requests
- combined or multi-region fits
- shared nuisance handling across regions when the underlying uncertainty is
  common

`DistributionIO` remains the core cached spectrum store. `fit/` should be
able to assemble in-memory fit problems from semantic spectrum queries over
that store. When a frozen assembled fit input is useful for reproducibility
or decoupling, that assembled object should persist as `ModelIO`.

`ModelIO` is the fit-facing analogue of a final assembled model snapshot:

- one or more regions
- observed data bins
- named processes with nominal and uncertainty payloads
- the correlation and nuisance information needed by the fit

`ModelIO` should be built downstream from `DistributionIO`; it should not
replace `DistributionIO` as the core cached store, and it should not trigger
a second pass over `EventListIO`.

By the time work reaches `fit/`, logical normalization and event weighting
should already have been resolved upstream and summarized into the persisted
cache payloads. `fit/` should not reopen shard provenance, inspect
run / subrun normalization maps, or reconstruct nominal event weights from
earlier workflow layers.

Fit-side policy should assemble processes, runs, regions, and correlations in
memory, but persisted storage ownership still belongs to `io/`.

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
  `mk_dist`, `mk_xsec_fit`
- plain-text workflow inputs where possible
- installed public headers that map cleanly onto the real module boundaries

Surface evolution should usually be additive first. Public churn should need an
explicit reason, not just internal cleanup momentum.

Within that public shape, the intended steady-state native workflow is:

- `mk_sample`
- `mk_dataset`
- `mk_eventlist`
- `mk_dist`
- final plot entry points that consume `DistributionIO` through semantic
  spectrum queries
- fit entry points that consume `DistributionIO` directly or a frozen
  `ModelIO` snapshot when useful

The repository should converge toward native downstream assembly rather than
relying on manual macros or hand-entered final-region inputs.

## Binning Model

The intended persisted binning split is:

- `DistributionIO` stores the stable fine-bin cache on simple uniform binning
- final adaptive or variable-width binning, if needed, is a downstream
  assembly policy applied on top of cached inputs rather than a second
  persisted surface by default

Adaptive or variable binning is in scope, but the binning policy should live
in downstream build code, not in `io/`.

`io/` may store the chosen edges and small reproducibility metadata, but it
should not own the adaptive-binning algorithm itself.

## Process And Region Assembly

Process and region assembly should be a first-class native workflow.

That means:

- building final views and fit problems from many named processes
- grouping cached sample inputs into signal and multiple background components
- filling observed data automatically from persisted inputs
- supporting both per-run and combined-run assembly from the same cached
  inputs
- recording enough provenance detail to reproduce the exact
  assembled result later

The steady-state assembly surface should be able to express ordinary analysis
regions without falling back to hand-written CSV bins or single-purpose
macros.

## Combined Fits

Combined or multi-region fits are in scope for the target end state.

That implies:

- multiple assembled run or region views may contribute to one fit problem
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
- less dependence on manual downstream assembly once stable native cache and
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
