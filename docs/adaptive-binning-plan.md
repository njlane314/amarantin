# Adaptive Binning Plan

## Goal

Add adaptive coarse binning as a reproducible downstream rebinning step.

The intended flow is:

1. keep `DistributionIO` as the stable fine-binned cache
2. derive one deterministic coarse binning from expected MC
3. rebin all processes and uncertainty payloads with the same edge map
4. consume the coarse result directly in downstream `fit/` or `plot/` code

This is explicitly not a plan for "every plot chooses its own bins on the fly".

## Design Anchors

From [`DESIGN.md`](../DESIGN.md):

- `io/` owns persistence only
- prefer plain data and namespace functions
- keep workflows in `app/`
- `plot/` is rendering only

That implies:

- `DistributionIO` may store enough binning metadata to make rebinned results
  reproducible, but it should not own the adaptive-binning policy
- the adaptive-binning algorithm should live in downstream code, not in `io/`
- there is no `ChannelIO` layer anymore, so adaptive coarse assembly should
  target plain downstream data in `fit/` or plotting helpers directly

## Current Direction

Current fine-cache layout notes:

- [`DistributionIO::Spec`](../io/DistributionIO.hh)
- [`io/bits/DERIVED`](../io/bits/DERIVED)

The first implementation should stay small:

- keep `DistributionIO` as the only persisted bin-wise cache surface
- derive coarse edges from expected MC only
- snap coarse edges to fine-bin boundaries
- rebin nominal, `sumw2`, detector, and family payloads with the same edge map
- build any final fit model as plain downstream data, likely centered on
  `fit::Channel`, not on a second persisted IO format

## Persistence

If persisted adaptive-binning provenance is needed, prefer a small additive
extension on `DistributionIO::Spec`, for example optional `bin_edges`, while
keeping `nbins/xmin/xmax` for compatibility and easy grepability.

Do not add a new channel-bundle persistence format just to hold rebinned views.

## Algorithm Placement

Put adaptive-binning and coarse-assembly code in `fit/`, not `io/` or `plot/`.

Reasoning:

- `fit/` already consumes cached systematic payloads
- adaptive binning is part of final model assembly, not persistence
- this keeps `io/` mechanical and avoids reintroducing a `ChannelIO`-style
  compatibility layer

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
}
```

Useful follow-on helpers:

- derive adaptive edges from one or more `DistributionIO::Spectrum` values
- rebin one `DistributionIO::Spectrum` onto a supplied coarse edge map
- assemble a coarse in-memory `fit::Channel` from rebinned cached entries

## Open Questions

1. Should optional `bin_edges` be persisted on `DistributionIO::Spec` in the
   first pass, or kept as downstream-only configuration until there is a
   concrete reproducibility need?
2. Should the first delivery live as library code only, or also get a thin CLI
   once the fit-side assembly settles?
