# Adaptive Binning Plan

## Goal

Add adaptive coarse binning as a reproducible downstream rebinning step.

The intended flow is:

1. keep `DistributionIO` as the stable fine-binned cache
2. derive one deterministic coarse binning from expected MC
3. rebin all processes and uncertainty payloads with the same edge map
4. persist the final region in `ChannelIO`

This is explicitly not a plan for "every plot chooses its own bins on the fly".

## Design Anchors

From [`DESIGN.md`](../DESIGN.md):

- `io/` owns persistence only
- prefer plain data and namespace functions
- keep workflows in `app/`
- `plot/` is rendering only

That implies:

- `DistributionIO` and `ChannelIO` may store binning metadata, but they should not own the adaptive-binning policy
- the adaptive-binning algorithm should live in downstream code, not in `io/`
- `plot/` should consume the final coarse result, not derive policy decisions itself

## Current State

Current fixed-width bin specs:

- [`DistributionIO::Spec`](../io/DistributionIO.hh)
- [`ChannelIO::Spec`](../io/ChannelIO.hh)

Current fine-cache layout notes:

- [`io/bits/DERIVED`](../io/bits/DERIVED)

Today, both specs only carry:

- `nbins`
- `xmin`
- `xmax`

That is enough for uniform fine caches, but not enough to persist an adaptive coarse binning.

## Recommended Placement

### Persistence

Keep persistence changes minimal:

- extend `DistributionIO::Spec` with optional `bin_edges`
- extend `ChannelIO::Spec` with optional `bin_edges`
- keep `nbins/xmin/xmax` for backward compatibility and easy grepability

### Algorithm

Put the adaptive-binning and channel-assembly functions in `fit/`, not `io/` or `plot/`.

Rationale:

- `fit/` already depends on `IO`
- adaptive binning is part of building a final region model, not rendering
- this avoids pushing workflow logic back into `io/`

Suggested new surface:

- `fit/AdaptiveBinning.hh`
- `fit/AdaptiveBinning.cc`
- optionally later `fit/ChannelBuild.hh`

## Data Model Change

### Phase 1: minimal compatible extension

Add optional edges to both specs:

```cpp
struct Spec
{
    std::string branch_expr;
    std::string selection_expr;
    int nbins = 0;
    double xmin = 0.0;
    double xmax = 0.0;
    std::vector<double> bin_edges;
};
```

Interpretation:

- `bin_edges.empty()` means legacy uniform binning, interpreted from `nbins/xmin/xmax`
- non-empty `bin_edges` means explicit persisted edges
- when `bin_edges` is present, `nbins`, `xmin`, and `xmax` are still filled redundantly for compatibility

### Channel-level provenance

Adaptive binning needs persisted provenance so the result is reproducible.

Add a small channel-level metadata block, either as:

- fields in `ChannelIO::Spec`, or
- a nested `AdaptiveBinningInfo` in `ChannelIO::Channel`

Recommended shape:

```cpp
struct AdaptiveBinningInfo
{
    bool enabled = false;
    std::string policy_name;
    int policy_version = 1;
    std::string reference_label;
    std::vector<std::string> reference_processes;
    double min_expected = 0.0;
    double max_frac_stat = -1.0;
    std::vector<double> pinned_edges;
    int fine_nbins = 0;
};
```

This should be channel-specific, not file-global, because different channels may choose different coarse edges.

## Edge-Derivation Policy

### Inputs

Derive coarse edges from expected MC only:

- never from observed data
- normally from total expected MC in one region
- optionally from a user-selected subset of processes

The first implementation should use a simple deterministic merge policy:

1. start from the first fine bin
2. accumulate adjacent fine bins
3. stop merging when both conditions hold:
   - `expected >= min_expected`
   - `sqrt(sumw2) / expected <= max_frac_stat`
4. never cross a pinned edge
5. repeat until the high edge is reached

Tail handling:

- if the final tail fails thresholds, merge it backward into the previous coarse bin
- if a pinned edge prevents that, keep the underfilled tail and record it in metadata/logging

### Boundary discipline

For the first version, require coarse edges to be snapped to fine-bin boundaries.

That keeps the first implementation simple:

- no fractional overlap matrix
- exact sums for nominal and `sumw2`
- easier covariance propagation

Later, if variable-width `DistributionIO` becomes common, a generic overlap-matrix rebin can be added.

## First API Sketch

Keep the API as plain data plus free functions.

```cpp
namespace fit
{
    struct AdaptiveBinningSpec
    {
        double min_expected = 10.0;
        double max_frac_stat = 0.30;
        std::vector<double> pinned_edges;
        int min_fine_bins_per_coarse = 1;
        std::string policy_name = "min_expected_stat_v1";
        int policy_version = 1;
    };

    struct AdaptiveBinningResult
    {
        std::vector<double> bin_edges;
        std::vector<int> fine_bin_begin;
        std::vector<int> fine_bin_end;
        std::vector<double> reference_nominal;
        std::vector<double> reference_sumw2;
    };

    AdaptiveBinningResult derive_adaptive_edges(
        int fine_nbins,
        double fine_xmin,
        double fine_xmax,
        const std::vector<double> &reference_nominal,
        const std::vector<double> &reference_sumw2,
        const AdaptiveBinningSpec &spec = AdaptiveBinningSpec{});
}
```

This takes the current fixed-width `DistributionIO` shape directly.

Useful follow-on helpers:

```cpp
namespace fit
{
    DistributionIO::Entry rebin_distribution(
        const DistributionIO::Entry &entry,
        const std::vector<double> &coarse_edges);

    ChannelIO::Process make_channel_process(
        const std::string &name,
        ChannelIO::ProcessKind kind,
        const std::vector<DistributionIO::Entry> &entries,
        const std::vector<double> &coarse_edges);
}
```

## Rebinning Rules

All payloads must be rebinned with the same map.

### Simple sums

Rebin by exact bin merge:

- `nominal`
- `sumw2`
- `detector_down`
- `detector_up`
- `detector_templates`

### Family payloads

For `genie`, `flux`, and `reint`:

- rebin covariance with `C' = R C R^T`
- derive rebinned `sigma` from `diag(C')`
- if eigenmodes are persisted, either:
  - rebin full covariance then recompress, or
  - rebin modes with `R` and then re-orthogonalise

The safer first implementation is:

1. expand to covariance
2. rebin covariance
3. recompute `sigma`
4. recompress eigenmodes if wanted

### Total envelopes

Do not directly merge fine-bin `total_up` / `total_down` and trust the result.

Instead:

1. rebin the underlying detector and family payloads
2. recompute the total coarse `total_up` / `total_down`

That keeps the final envelope consistent with the rebinned uncertainty model.

## Channel Assembly Path

Adaptive binning belongs at the point where a final channel is built.

Recommended assembly sequence:

1. choose the set of fine-binned `DistributionIO::Entry` inputs for the region
2. sum nominal MC to make the reference spectrum
3. call `derive_adaptive_edges(...)`
4. rebin each input entry with those edges
5. assemble `ChannelIO::Process` values from the rebinned entries
6. write one final coarse `ChannelIO::Channel`

Observed data should only be rebinned after the edges are frozen.

## CLI / Workflow Shape

Do not add adaptive logic to `mk_eventlist`.

That would blur the boundary between:

- event-list construction
- fine-cache construction
- final region assembly

Preferred future workflow:

1. `mk_eventlist` writes fine `DistributionIO`
2. a new thin app, likely `mk_channel`, assembles coarse `ChannelIO`
3. `plot/` reads `ChannelIO` or already-rebinned products

If a new app is too much for the first step, start with:

- library functions in `fit/`
- one macro or small helper in `app/` that exercises them

## Staged Implementation Plan

### Milestone 1: persistence-only compatibility

- add `bin_edges` to both specs
- update `DistributionIO` and `ChannelIO` read/write paths
- preserve legacy readback when `bin_edges` is absent

Verification:

- old files still read
- newly written fixed-width files round-trip unchanged
- explicit-edge files round-trip with the same edge vector

### Milestone 2: edge derivation

- add `fit::AdaptiveBinningSpec`
- add `fit::derive_adaptive_edges(...)`
- require coarse edges to align to fine-bin boundaries

Verification:

- deterministic output for the same reference input
- pinned-edge behavior covered
- empty / low-stat tails handled explicitly

### Milestone 3: payload rebinning

- add `rebin_distribution(...)`
- propagate nominal, `sumw2`, detector, and family payloads
- recompute coarse `total_up` / `total_down`

Verification:

- total yield preserved under rebin
- covariance remains symmetric and non-negative on the diagonal
- coarse envelopes agree with recomputed payloads

### Milestone 4: channel assembly

- add one path that builds coarse `ChannelIO` from fine `DistributionIO`
- persist adaptive-binning provenance with the channel

Verification:

- common binning shared across all processes in the channel
- observed data is rebinned only after edge choice is fixed
- resulting `ChannelIO` is consumable by existing fit/plot paths

## Risks

### Bias

Risk:

- choosing bins from observed data biases the analysis

Rule:

- derive adaptive edges from expected MC only

### Non-reproducibility

Risk:

- changing thresholds or reference processes silently changes the binning

Rule:

- persist policy name, version, thresholds, and reference source with the channel

### Inconsistent uncertainties

Risk:

- only rebinned nominal values are updated while detector/family payloads stay on the fine grid

Rule:

- every persisted uncertainty payload must use the same coarse edge map

## Open Questions

1. Should adaptive channel assembly live in `fit/` permanently, or does the repo want a separate final-region assembly module later?
2. Should the first policy be only `min_expected + max_frac_stat`, or also include an explicit maximum-bin-count constraint?
3. Should `ChannelIO` persist only the final coarse covariance, or also the compressed eigenmodes after rebinning?
4. Is a new `mk_channel` CLI acceptable, or should the first delivery stay as library code plus macros?

## Recommendation

Proceed with:

- fine `DistributionIO`
- adaptive edge derivation in `fit/`
- coarse persisted `ChannelIO`

Do not make `DistributionIO` adaptive at write time.
