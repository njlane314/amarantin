Syst Vision
===========

Purpose
-------

This document narrows the repo-wide vision in `VISION.md` to `syst/`.

It is informed by the good parts of `hive` systematics handling, especially
its covariance-first treatment of detector and reweighting systematics, while
keeping `amarantin` aligned with `DESIGN.md`:

- `io/` owns persistence only
- `syst/` owns systematic calculations and cache construction
- `app/` owns workflow orchestration
- downstream code should stay on `EventListIO` / `DistributionIO` surfaces

This is not a request to import `hive`'s XML-driven orchestration, large
monolithic CLI, or `system()`-based plumbing. The import target is the
mathematical contract and the data model, not the surrounding shell and XML
framework.

`amarantin` should remain its own covariance builder. The import target is the
systematics semantics that `hive` encodes around SBNFit-style covariances, not
delegation of covariance construction to an external package.

Current Gap
-----------

`amarantin` already has the right high-level split:

- detector systematics from alternate selected samples
- reweighting systematics from universe weights on the nominal sample

The main weakness is that the canonical downstream summary is still too
envelope-first:

- detector output is primarily a binwise min/max envelope
- total output is primarily a quadrature up/down envelope
- cached covariance can exist for reweight families, but downstream `fit/`
  does not yet treat covariance as the primary contract

`hive` is stronger in one important way: covariance is the canonical object,
and envelopes are derived later for plotting or summaries.

Module End State
----------------

`syst/` should become covariance-first.

Its preferred chain should be:

  selected EventListIO rows
      |
      v
  nominal prediction vector
      +
  detector source shifts
      +
  reweight-universe source shifts
      +
  statistical covariance
      |
      v
  source-resolved covariance objects
      |
      v
  optional eigenmode compression / display envelopes
      |
      v
  DistributionIO cache payloads

Covariance Provenance
---------------------

The covariances in this design do not come from an opaque external matrix
source. They come from event-level variations that are already natural on the
`EventListIO` surface:

- nominal selected events and their persisted nominal weights
- universe weights for reweightable families
- explicit detector-variation selected samples

This is the semantic import from `hive`:

- reweight covariance is built from universe-shifted predictions on the same
  nominal event set
- detector covariance is built from source-by-source detector shifts relative
  to a central-value prediction
- total covariance is the sum of source covariances
- fractional covariance is a compatibility view, not the only meaningful form

So the target architecture here is:

- `amarantin` builds covariance itself in `syst/`
- `DistributionIO` persists the resulting covariance-facing payloads
- downstream code consumes those payloads directly
- SBNFit-style inputs and outputs remain compatibility surfaces, not the
  source of truth for the builder

In practical terms, `amarantin` should not need `hive`'s XML templates or an
external covariance-building class in order to reproduce the same systematic
meaning.

The canonical objects should be:

- nominal predicted yields
- source-resolved shift vectors or universe templates
- absolute covariance matrices
- optional fractional covariance views
- optional compressed eigenmodes derived from covariance

The following should be derived views, not primary objects:

- detector up/down envelopes
- family up/down envelopes
- total up/down envelopes

Those envelopes are still useful for quick plots and smoke checks, but they
should no longer be the only information that survives.

SearchingForStrangeness EventWeight Surface
-------------------------------------------

`/Users/user/programs/searchingforstrangeness` is the concrete upstream branch
surface this framework should target.

The important boundary fact is:

- in checked-in `searchingforstrangeness` configs there is no dedicated
  `EventWeights` tree
- the weight branches live directly on the main `EventSelectionFilter` event
  tree
- `amarantin` should therefore consume those persisted branches directly from
  `EventListIO`, not try to reconstruct art `MCEventWeight` products

The branch-to-systematics contract should be:

| Branch surface | Meaning in `searchingforstrangeness` | Role in `amarantin` |
| --- | --- | --- |
| `weightSpline`, `weightTune`, `weightSplineTimesTune` | central-value GENIE spline/tune correction summaries | nominal-weight only; fold into `__w_cv__`, not a systematic family |
| `ppfx_cv` | central-value NuMI PPFX correction summary | nominal-weight only; fold into `__w_cv__` for NuMI, not a systematic family |
| `RootinoFix` | one-sided central-value correction from GENIE knob production | nominal-weight only; fold into `__w_cv__`, not a detector or reweight nuisance |
| `weightsGenie` | compressed GENIE multisim universes | canonical GENIE covariance family |
| `weightsReint` | compressed Geant4 reinteraction multisim universes | canonical reinteraction covariance family |
| `weightsPPFX` | compressed NuMI PPFX multisim universes | canonical flux covariance family for NuMI |
| `weightsFlux` | compressed non-NuMI / legacy beamline flux universes | canonical flux covariance family fallback when `weightsPPFX` is absent |
| `weightsGenieUp`, `weightsGenieDn` | paired compressed up/down knob weights in a fixed upstream order | optional explicit paired GENIE-knob source lane; keep separate from `weightsGenie` multisim |
| scalar `knob*up`, `knob*dn` branches | scalar projections of a subset of GENIE knob weights | debug / fallback only; not the canonical systematic surface if paired vectors survive |

The branch encoding already matches `amarantin`'s current universe decode:

- `searchingforstrangeness` compresses multisim and paired up/down vectors as
  `unsigned short` values scaled by `1000`
- `amarantin` already decodes multisim vectors with `raw / 1000.0` and
  sanitises non-finite or non-positive values
- no additional rescaling convention should be introduced downstream

Upstream branch semantics
-------------------------

`searchingforstrangeness` currently assembles those branches as follows:

- `weightsGenie`
  - built by multiplying together the `All_UBGenie` blocks coming from the
    checked-in eventweight process sequence
  - default checked-in production surface is `500` universes
  - this is already the final persisted GENIE multisim family that
    `amarantin` should consume; `amarantin` should not try to rediscover the
    upstream art process split
- `weightsPPFX`
  - built in NuMI tuple mode from `ppfx_*` multisim blocks
  - checked-in production surface is `600` universes
  - this is the preferred flux family whenever it is present
- `weightsFlux`
  - built in non-NuMI mode by multiplying the legacy beamline flux multisim
    groups into one final vector
  - this is logically the same family role as `weightsPPFX`, but for a
    different upstream beam configuration
- `weightsReint`
  - built by multiplying the available Geant4 hadron reinteraction vectors
    into one final reinteraction multisim family
- `weightsGenieUp` / `weightsGenieDn`
  - built from the separate `eventweightGenieKnobs` surface
  - the vector entries are ordered by an upstream hardcoded knob list, not by
    branch name discovery at read time
  - if `amarantin` imports this lane, it must freeze and review that knob
    order explicitly rather than infer labels from the ntuple

Framework mapping
-----------------

The intended `amarantin` usage should be:

- nominal weight path:
  - keep the current `ana::build_event_list(...)` contract
  - compute `__w_cv__` from:
    - `weightSplineTimesTune` when available, otherwise
      `weightSpline * weightTune`
    - times `ppfx_cv` for NuMI samples when present
    - times `RootinoFix` when present
  - do not re-interpret those central-value factors as separate covariance
    families
- canonical covariance families in `syst/`:
  - GENIE family: `weightsGenie`
  - reinteraction family: `weightsReint`
  - logical flux family:
    - prefer `weightsPPFX`
    - otherwise use `weightsFlux`
- optional explicit knob-pair lane:
  - treat `weightsGenieUp` / `weightsGenieDn` as a separate paired-source
    family, not as extra entries inside `weightsGenie`
  - for knob source `k`, the intended shift vector is:

```text
s^k_{p,b} = 0.5 * (n^{up,k}_{p,b} - n^{down,k}_{p,b})
V^{knob}_{(p,b),(q,c)} = sum_k s^k_{p,b} * s^k_{q,c}
```

  - this paired lane should remain optional until the upstream knob ordering is
    copied into a reviewed local contract
  - when enabled, persist:
    - reviewed source labels
    - source shift vectors
    - absolute covariance
- scalar `knob*up/dn` branches:
  - do not use them as the primary source when `weightsGenieUp/Dn` exist
  - they are acceptable only as a narrow fallback or validation surface for a
    subset of knobs

Immediate implications for `amarantin`
--------------------------------------

This upstream branch surface sharpens three concrete implementation points:

- flux-family resolution in `syst/` should be branch-set aware:
  - prefer `weightsPPFX`
  - otherwise fall back to `weightsFlux`
- if knob-pair propagation is enabled, `syst/` should import the
  `weightsGenieUp/Dn` vector lane with explicit source labels, not scrape the
  scalar `knob*` branches one by one
- downstream fit/export code should consume that knob-pair lane as explicit
  source shifts or derived covariance, not collapse it back into a generic
  envelope at the boundary

Design Anchors
--------------

From `DESIGN.md`:

- prefer plain data and namespace functions
- keep module boundaries sharp
- downstream code should usually open `EventListIO` and stay on that surface
- `DistributionIO` is the bin-wise cache surface

That implies:

- `syst/` should compute and describe the math contract for systematics
- `syst/` should own covariance construction from event-level inputs
- `DistributionIO` should persist enough information to rebin and fit later
- `fit/` should consume covariance or covariance-derived modes directly
- `plot/` may draw envelopes, but should not define the systematic semantics

Notation
--------

Use one consistent notation for review.

Let:

- `p` index a process or sample
- `b, c` index histogram bins
- `i, j` index entries in a stacked full prediction vector
- `u` index universes inside one reweight family
- `d` index detector systematic sources

Nominal prediction
------------------

For one process `p` and bin `b`, define the nominal yield as:

```text
n_{p,b} = sum_{e in (p,b)} w_e
```

where `w_e` is the nominal persisted event weight `__w__`.

For stacked downstream assembly, define one full vector:

```text
y_i = stacked nominal prediction over (process, bin)
```

The exact stacking order must be explicit and stable anywhere full covariance
is built or collapsed.

Reweight Families
-----------------

For one reweight family `F` and universe `u`:

```text
n^F_{p,u,b} = sum_{e in (p,b)} w_e * alpha^F_{e,u}
Delta^F_{p,u,b} = n^F_{p,u,b} - n_{p,b}
```

Candidate family covariance:

```text
V^F_{(p,b),(q,c)} = norm_F * sum_u Delta^F_{p,u,b} * Delta^F_{q,u,c}
```

Interpretation:

- the builder input is the nominal selected event set
- each universe changes the event contribution through a universe weight
- the covariance is assembled from those event-level universe shifts

For min/max-style reweight sources, the same family contract should be reduced
to explicit shift vectors before entering the covariance sum, rather than
flattened to a plotting envelope.

Review point:

- internal canonical convention:
  - `norm_F = 1 / N_F`
- compatibility/export layers may translate to another convention if an
  external consumer requires it

Rationale:

- the universe set is treated as the provided systematic ensemble
- the internal cache contract should not depend on an external fitter's
  estimator convention
- this matches the current `amarantin` behavior and avoids unnecessary churn

Detector Systematics
--------------------

For one detector source `d`, define a matched central-value detector sample and
one detector-varied sample for the same source:

```text
n^{cv,d}_{p,b} = yield from the matched central-value detector sample
n^{var,d}_{p,b} = yield from the detector-varied sample
Delta^det_{p,d,b} = n^{var,d}_{p,b} - n^{cv,d}_{p,b}
```

Interpretation:

- the builder input for source `d` is a matched central-value prediction plus
  one detector-varied prediction for that same source
- detector covariance is therefore built from explicit sample-to-sample shifts
  at the selected-event level
- this is source-resolved before any total matrix merge or display envelope

For one source `d`, define the source covariance as an outer product of the
shift vector:

```text
V^{det,d}_{(p,b),(q,c)} =
Delta^det_{p,d,b} * Delta^det_{q,d,c}
```

If detector sources are treated as independent one-sigma shifts, the full
detector covariance is:

```text
V^det_{(p,b),(q,c)} = sum_d V^{det,d}_{(p,b),(q,c)}
```

Equivalently, in vector form:

```text
V^{det,d} = s^(d) * (s^(d))^T
V^det = sum_d s^(d) * (s^(d))^T
```

where `s^(d)` is the full stacked shift vector for source `d`.

If a future detector source is provided as an explicit up/down pair for one
underlying nuisance `k`, then the pairwise central-difference form should be:

```text
n^{up,k}_{p,b} = yield from detector up sample
n^{down,k}_{p,b} = yield from detector down sample
Delta^{pair}_{p,k,b} = 0.5 * (n^{up,k}_{p,b} - n^{down,k}_{p,b})
V^{det,k}_{(p,b),(q,c)} =
Delta^{pair}_{p,k,b} * Delta^{pair}_{q,k,c}
```

That paired form is not the default. It should only be used when the metadata
explicitly says two samples are the up/down realizations of one detector
source.

This is the core semantic import from `hive`: detector variations should be
preserved as covariance-generating source shifts, not reduced immediately to a
binwise min/max envelope.

Detector-source contract:

- detector covariance is built one named source at a time
- each named detector source maps to one covariance-generating shift relative
  to a central-value prediction
- detector sources are independent by default
- pairwise central-difference treatment is reserved for an explicit up/down
  detector metadata contract, not inferred by default

This matches the visible `hive` / SBNFit-style detector setup:

- `hive` loops over named detector sources such as `WiremodX`, `WiremodYZ`,
  `SCE`, and `Recomb2`
- for each source, it generates one covariance file and then adds the
  resulting source matrices together
- the detector templates carry one `central_value="true"` sample and one
  `central_value="false"` sample for the same `associated_systematic`

So the `amarantin` import should be source-by-source detector covariance
against nominal, not envelope-first treatment and not implicit central
difference from name matching alone.

What Is Needed
--------------

To build detector covariance correctly in `amarantin`, `syst/` needs:

- a stable detector source label `d` for each detector variation
- for each source `d`, a matched central-value sample key and a matched varied
  sample key
- identical selection, observable definition, and target binning for the
  central-value and varied samples
- identical process decomposition and stable stacked `(process, bin)` order
- the same event-weight convention on both sides, using the persisted nominal
  selected-event weight `__w__`
- a deterministic rule for whether detector covariance is assembled in
  per-process block form or directly in one stacked full-vector form
- validation that the matched central-value detector sample is compatible with
  the analysis nominal prediction under the same normalization convention

If explicit up/down detector pairs are ever supported, `syst/` will also need:

- metadata that marks the two samples as one underlying detector source
- polarity or role labels such as `up` and `down`
- a rule for how the central-difference shift is constructed and persisted

Statistical Covariance
----------------------

The nominal MC statistical covariance should remain explicit and diagonal in
the first pass:

```text
V^stat_{(p,b),(q,c)} = delta_{pq} * delta_{bc} * sumw2_{p,b}
```

where `sumw2` is taken from the nominal selected-row histogram fill.

Total Covariance
----------------

The preferred total covariance contract is:

```text
V^tot = V^stat + V^det + V^genie + V^flux + V^reint + ...
```

If later sources are added, they should extend this sum explicitly.

Derived display bands should come from the diagonal:

```text
sigma_i = sqrt(max(V^tot_{ii}, 0))
y_i^up = y_i + sigma_i
y_i^down = max(0, y_i - sigma_i)
```

Those are display summaries only. They must not replace `V^tot` as the
primary persisted or fit-facing object.

Fractional Covariance
---------------------

`hive` works heavily with fractional covariance files. `amarantin` should make
the relation explicit:

```text
F_{ij} = V_{ij} / (y_i * y_j)
V_{ij} = F_{ij} * y_i * y_j
```

Canonical persisted form:

- canonical in-memory form: absolute covariance `V`
- canonical persisted form: absolute covariance `V`
- fractional covariance `F` is derived only when needed for compatibility or
  export
- zero-bin handling therefore remains a boundary-format concern for `F`, not a
  core cache-schema concern

Rationale:

- `amarantin` builds covariance from source shifts, which naturally produces
  absolute covariance
- rebinning and collapse act linearly on `V`
- storing both `V` and `F` creates duplication and drift risk
- `F` is cheap to derive from the persisted nominal yields plus `V`

This means SBNFit-style fractional covariance should be treated as a boundary
format, not as evidence that `amarantin` should stop building the covariance
itself.

Rebinning
---------

Any persisted covariance that survives rebinning should obey:

```text
y' = R * y
V' = R * V * R^T
```

where `R` is the explicit rebin matrix from fine bins to target bins.

The same rule should apply to:

- detector covariance
- reweight-family covariance
- statistical covariance
- total covariance

This is already the right mental model for the current family rebinning code
and should become the universal rule for the full systematics surface.

Collapse
--------

`hive`'s important downstream idea is not the exact class layout but the fact
that a larger covariance object can be collapsed into the final plotted or fit
space by an explicit linear map.

Use the same contract here:

```text
y_coll = C * y_full
V_coll = C * V_full * C^T
```

where `C` maps:

- full stacked process-bin space
  to
- one collapsed channel/bin space

This collapse should happen in downstream `fit/` assembly, not in `io/`.

Eigenmode Compression
---------------------

Compression remains useful, but it should be derived from covariance rather
than standing in for the definition of the systematic itself.

For a covariance matrix `V`:

```text
V = Q * Lambda * Q^T
M_k = sqrt(lambda_k) * q_k
V approx sum_k M_k * M_k^T
```

Persisted eigenmodes should therefore be:

- an optimization and fit convenience
- derived from a canonical covariance matrix
- optional to drop or truncate when the full covariance is also available

They should not be the only exact record of the systematic if later rebinning
or alternate collapse is expected.

Persistence Contract
--------------------

`DistributionIO` should eventually persist enough information to reconstruct:

- nominal bins
- `sumw2`
- detector source labels
- detector source shift vectors or templates
- detector covariance
- universe-family source labels
- universe-family covariance
- optional eigenmodes
- optional retained universes when exact replay is needed

The cache schema should stay mechanical and grep-friendly, but it must be rich
enough that the downstream fit does not fall back to envelope-only semantics.

Non-Goals
---------

Do not import the following from `hive`:

- XML templates as the primary systematics definition language
- environment-variable path discovery for templates
- `system()` calls for file generation or text replacement
- one large CLI mode switchboard that mixes plotting, fitting, and covariance
  generation
- delegation of covariance construction to an external builder when the needed
  event-level ingredients already live on `EventListIO`

`amarantin` should import the covariance semantics, not the orchestration
machinery.

Staged Plan
-----------

### Milestone 0: Lock the math contract

- write down the exact covariance equations and conventions
- resolve open review items:
  - full-vector stacking and collapse order

Acceptance:

- one reviewed document exists
- cache/versioning implications are explicit

### Milestone 1: Make detector systematics covariance-first

- keep alternate detector samples in `EventListIO`
- compute one shift vector per detector source
- persist detector covariance and source labels
- keep detector envelope only as a derived summary

Acceptance:

- detector covariance survives cache readback and rebinning
- detector envelope is no longer the only detector payload

### Milestone 2: Make reweight families covariance-canonical

- keep current family universe fill path
- treat covariance as the canonical family product
- derive eigenmodes and sigma bands from covariance
- keep retained universes optional

Acceptance:

- family covariance is always reconstructible from the persisted canonical
  payload

### Milestone 3: Teach `fit/` to consume covariance-first payloads

- build nuisances from covariance-derived modes or explicit source shifts
- support full-vector collapse `C * V * C^T` where needed
- avoid degrading a rich covariance payload to one sigma-only nuisance unless
  explicitly requested

Acceptance:

- fit semantics no longer depend primarily on envelope survival

### Milestone 4: Add compatibility/export surfaces only if needed

- if SBNFit-style export is required, build a thin export path from the
  covariance-first cache
- keep export concerns out of the core `syst/` math

Acceptance:

- compatibility logic stays at the edge, not in the core contract

Open Review Questions
---------------------

1. Should full covariance assembly across processes live only in `fit/`, or
   should `syst/` provide a small helper for building the stacked vector and
   source-shift matrix?
2. Is exact universe retention required in the default cache policy, or only
   for debug / validation builds?

Decision Bias
-------------

When a choice is unclear, bias toward:

- covariance-first semantics
- explicit linear algebra contracts
- source-resolved provenance
- small plain-data APIs
- thin downstream adapters

Bias against:

- envelope-only persistence
- hidden normalization conventions
- format decisions driven by plotting convenience
- HIVE-style workflow sprawl leaking into `app/` or `io/`
