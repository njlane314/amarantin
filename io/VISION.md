IO Vision
=========

Purpose
-------

This document narrows the repo-wide vision in `VISION.md` to `io/`.

`DESIGN.md` remains the source of truth. This file is meant to answer a more
local question: what should each file in `io/` eventually be responsible for,
and what should it avoid becoming?

Module End State
----------------

`io/` should stay persistence-only.

Its preferred chain remains:

  input ROOT files
      |
      v
  ShardIO
      |
      v
  SampleIO
      |
      v
  DatasetIO
      |
      v
  EventListIO
      |
      v
  DistributionIO

The intended steady-state split is:

- `ShardIO`
  small shard-level provenance record and scanner at the edge of the workflow
- `SampleIO`
  one logical-sample persistence and normalization surface
- `DatasetIO`
  one dataset-wide container of already-logical samples
- `EventListIO`
  the stable row-wise downstream analysis surface
- `DistributionIO`
  the stable fine-bin cache surface

The intended non-goals are:

- no selection logic in `io/`
- no systematics calculation in `io/`
- no plotting helpers in `io/`
- no generic utility bucket under `io/bits/`
- no growth of local ROOT macros into the primary user workflow

File-By-File Review
-------------------

### `io/CMakeLists.txt`

Status: module build manifest.

This file should stay declarative: source list, header install list, link
dependencies, and nothing more. It should not absorb policy, feature flags, or
workflow branching. When a file leaves the public surface, this file should
reflect that directly rather than preserving compatibility scaffolding forever.

### `io/README`

Status: short orientation note.

This file should remain the fast map of the module and the persistence chain.
It should stay shorter than this document, but it also needs to stay current:
if file names or the visible workflow shape changes, `io/README` should change
with them instead of becoming historical drift. If design rationale,
retirement plans, or end-state policy starts to accumulate there, move that
material here instead.

### `io/ShardIO.hh`

Status: installed header for a small edge-facing helper.

This type should remain one shard-level record: one sample-list path, one shard
name, one set of input files, one recovered run/subrun exposure summary. It
should not grow dataset assembly policy, analysis semantics, or multi-shard
coordination logic.

The current name should stay `ShardIO`. Prefer short, old-school names over
explanatory wrapper names: direct nouns like `ShardIO`, `list_path`, `files`,
`subruns`, `pot_sum`, and `scan` are better fits here than longer
abstraction-heavy alternatives.

### `io/ShardIO.cc`

Status: implementation for `ShardIO`.

This file should stay short, defensive, and ROOT-specific. Its job is to read a
sample list, recover the `SubRun` tree layout, validate that the shard is
internally consistent, and persist the resulting provenance. It should fail
fast on malformed inputs rather than inventing fallback behavior.

This file should keep the cleaned-up `ShardIO` names rather than drifting back
to older partition wording. Prefer small old-style function and variable names
where they improve readability, but do not do rename churn beyond names that
clarify the shard role directly.

### `io/SampleIO.hh`

Status: installed header for the logical-sample layer.

`SampleIO` should remain the narrow bridge from shard-level inputs to one
logical sample with explicit normalization metadata. It should stay plain and
inspectable rather than becoming a deep object hierarchy. If more workflow
orchestration is needed, that belongs in `app/`, not in this header.

The public enums should match the workflow language used elsewhere in the
repository. `Origin::kSignal` is the right current name unless a future
workflow needs a narrower distinction than plain signal-vs-rest.

Naming should stay direct here as well. If names are cleaned up, prefer short
record-style names and plain verbs over explanatory wrappers.

### `io/SampleIO.cc`

Status: implementation of logical-sample build, read, and write.

This file should keep one direct responsibility: build one persisted sample from
already-resolved shard inputs and attach the normalization surface needed
downstream. It is allowed to talk to ROOT and the run database because that is
part of producing the persisted sample record. It should not become a second
CLI layer, and it should not absorb analysis-time weighting rules that belong
in `ana/`.

Manifest parsing and legacy `@file` expansion now belong in `app/mk_sample.cc`.
Keep that workflow interpretation there rather than rebuilding a second parser
inside `io/`.

### `io/DatasetIO.hh`

Status: installed header for dataset containers.

`DatasetIO` should remain the stable container for many named logical samples.
Its job is to preserve sample metadata, provenance, and normalization surfaces
that have already been resolved upstream. It should not reach back into raw
ROOT files or redo shard fan-in.

The naming guidance here should match `SampleIO.hh`: prefer short, direct
record-style names, with `Sample::Origin::kSignal` as the current workflow
name.

The duplicated sample-like record is a design smell, but `DatasetIO` still
needs its own plain persisted sample record. The better end state is not to use
`SampleIO` itself as the dataset record type. It is to converge on one shared
plain sample-data struct used by both layers, while leaving `SampleIO` as the
builder / file wrapper around that data.

The authoritative short-form file-layout note for `DatasetIO` should live in
this header, not only in the `.cc` file. If the on-disk structure changes, the
header comment should change in the same pass.

### `io/DatasetIO.cc`

Status: implementation of dataset persistence.

This file should stay focused on deterministic read/write behavior for dataset
files. It should keep overwrite behavior explicit and predictable. If new
sample-level metadata is required downstream, it should be persisted here once
instead of reconstructed later from filenames or ad hoc conventions.

Long layout documentation should not live only here. Keep implementation detail
comments here when useful, but keep the public file-format summary in the
header and keep it current there.

The main local cleanup target in this file is wrapper ceremony, not large
state bloat. Most locals exist because ROOT needs addressable branch variables
or because the code is assembling persisted records step by step. The trim pass
to prefer is small: remove dead checks, redundant assignments, and single-use
wrapper locals when touched, without obscuring the file layout work.

### `io/EventListIO.hh`

Status: installed header for the stable row-wise analysis surface.

This is the long-lived persisted output of event-list building. It should stay
simple: metadata access, sample enumeration, selected-tree access, subrun-tree
access, and write hooks used by `ana/`. New downstream row-wise workflows
should prefer to anchor on this file format instead of reaching back to
`SampleIO` or `DatasetIO`.

Its metadata surface should stay lean, but the current fields are defensible:
`subrun_tree_name` is needed for readback, and the rest are build provenance
needed to understand and reproduce how the event list was made. Add more only
when they materially improve reproducibility or downstream interpretation.

### `io/EventListIO.cc`

Status: implementation of row-wise event-list persistence.

This file should continue to do direct file-layout work only: write metadata,
copy selected trees, copy subrun trees, and persist the sample normalization
surface needed for event weights. It should not accumulate selection logic,
sample-definition policy, or plotting conveniences.

### `io/DistributionIO.hh`

Status: installed header for the stable fine-bin cache surface.

`DistributionIO` should be the default persisted downstream cache for repeated
plotting and fitting work. It should continue to represent one sample-specific
cached payload inside a larger file, with explicit nominal, statistical,
detector, and universe-family content. `Spectrum` is the right noun here:
specific enough to describe the cached histogram-like payload without
pretending it is a higher-level workflow object.

### `io/DistributionIO.cc`

Status: implementation of fine-bin cache persistence.

This file should stay deterministic and mechanical: write one cached payload,
read one cached payload, and expose enough metadata to make cache identity
explicit. It should not absorb plotting policy, fit assembly policy, or
bin-combination heuristics. Downstream consumers should combine compatible
cached objects explicitly rather than relying on filename conventions.

### `io/macro/mk_sample.C`

Status: local convenience macro.

This file should remain a thin manual wrapper around `SampleIO` for quick local
testing. It is useful as a smoke path for `tools/run-macro`, but it should not
be treated as the canonical sample-building interface when the app already
exists.

### `io/macro/print_sample.C`

Status: local inspection macro.

This file should remain disposable and simple. Its purpose is quick human
inspection of a `SampleIO` file, not a stable reporting interface.

### `io/macro/print_dataset.C`

Status: local inspection macro.

This file should remain a lightweight way to inspect persisted dataset content
and provenance while debugging. It should not accumulate workflow logic or turn
into a second dataset browser API.

### `io/macro/print_eventlist.C`

Status: local inspection macro.

This file should stay explicitly tied to the current event-list schema and
remain easy to edit or delete. If a more durable row-wise inspection interface
is ever needed, it should be built around `EventListIO` as a real downstream
tool, not by making this macro more elaborate.

### `io/bits/RootUtils.hh`

Status: private helper header.

This file should stay tiny and boring: only the minimum ROOT directory and
object helpers needed repeatedly inside `io/`. It should not become a repo-wide
utility layer or a dumping ground for unrelated string and container helpers.

### `io/bits/RunDatabaseService.hh`

Status: private helper header for run-database lookups.

This surface should remain narrow: read-only queries needed to resolve sample
normalization from run/subrun information. It should not widen into a general
database abstraction or a catch-all beam-conditions service.

### `io/bits/RunDatabaseService.cc`

Status: private SQLite implementation.

This file should stay focused on simple read-only query execution with clear
failure behavior. If more run-database fields are needed, prefer adding direct,
named query helpers over introducing ORM-style structure.

### `io/bits/DERIVED`

Status: schema and column reference note for framework-owned persisted content.

This file should remain a human-readable contract note for the derived columns
and file layouts owned by this repository, especially `EventListIO` and
`DistributionIO`. It should describe persisted structure and reserved helper
columns, not implementation trivia.

### `io/bits/NTUPLE`

Status: upstream ntuple reference note.

This file should remain a reference to the upstream input schema that `io/` and
`ana/` expect to consume. It should document assumptions about the input trees
and branch names without turning into a mirror of upstream code.

Likely Simplifications
----------------------

The most plausible future cleanup in `io/` is not a broad redesign. It is a
small set of targeted reductions:

- keep `ShardIO` small as the shard-edge scanner and provenance record
- keep `SampleIO` centered on one logical-sample surface and push any growing
  workflow parsing outward into `app/`
- keep `DistributionIO::Spectrum` as the direct cached-payload noun
- avoid reintroducing a `ChannelIO`-style compatibility persistence layer on
  top of `DistributionIO`

If a proposed change does not make one of those files smaller, flatter, or
easier to grep, it is probably the wrong kind of change for `io/`.
