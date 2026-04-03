# ccnumu_hyperon

Living analysis brief for the Semi-Inclusive Charged-Current Muon-Neutrino
Neutral-Hyperon Analysis.

This file is analysis-facing working memory. Update it whenever the sample
rules, signal definition, fit structure, or training-snapshot needs change.

`DESIGN.md` remains the source of truth for repository architecture and coding
style.

## Identity
- slug: `ccnumu_hyperon`
- title: `Semi-Inclusive Charged-Current Muon-Neutrino Neutral-Hyperon Analysis`
- goal:
  - single-parameter cross-section measurement
  - signal-selection study
  - sideband/background-constrained fit

## Inputs
- beam:
  - `numi`
- polarities:
  - `fhc`
  - `rhc`
- run periods currently in scope:
  - `run1` FHC and RHC
  - `run2a` FHC
  - `run2b` RHC
  - `run3b` RHC
  - `run4a` RHC
  - `run4b` RHC
  - `run4c` FHC
  - `run4d` FHC
  - `run5` FHC
- authoritative external sample inventory:
  - `../searchingforstrangeness/SAMPLES`
- upstream production context:
  - shard-level ntuple jobs produced from the campaign XML workflows in
    `../searchingforstrangeness`
- upstream inference boundary:
  - CNN inference is run upstream, not in this repo
  - the scalar CNN scores are written back onto the ntuples before this repo
    consumes them
- include these sample families when available:
  - beam-on data
  - beam-off / ext
  - dirt
  - `nu` overlay
  - strangeness overlay
  - detector variations
- explicitly out of scope:
  - `nue` overlay
  - `CCNCPi0` overlay

## Logical Samples
- build these logical sample keys:
  - `data`
  - `ext`
  - `dirt`
  - `overlay`
  - `signal`
- intended roles:
  - `data`: beam-on observed events
  - `ext`: beam-off / external background sample
  - `dirt`: dirt background MC
  - `overlay`: nominal inclusive neutrino interaction MC after strange-final-
    state removal
  - `signal`: broad strange-final-state MC stream
- orthogonality rule:
  - `signal` contains all events with a strange hadron in the final state
  - `overlay` excludes all events with strange hadrons in the final state
  - `signal` and `overlay` must stay orthogonal at sample-construction time
- workflow implication:
  - this repo owns the sample-orthogonality logic and should apply it before
    downstream dataset / event-list / training-snapshot production

## Measurement Signal Definition
- logical `signal` is broader than the measurement signal
- the narrower measurement signal is defined later inside `signal`
- truth-variable anchor:
  - `../searchingforstrangeness/AnalysisTools/SignalAnalysis_tool.cc`
- written physics-definition anchor:
  - `../internal-note/sections/03-signal.tex`
- existing truth branches of interest include:
  - `truth_has_strange_fs`
  - `truth_has_fs_lambda0`
  - `truth_has_fs_sigma0`
  - `truth_has_g4_lambda0`
  - `truth_has_g4_lambda0_to_ppi`
  - `truth_has_g4_lambda0_from_sigma0`
  - `truth_n_fs_lambda0`
  - `truth_n_fs_sigma0`
- current measurement-level definition to preserve:
  - muon-flavour neutrino or antineutrino
  - charged-current
  - true vertex inside the internal-note fiducial volume
  - at least one exit-level `Lambda0` / `Sigma0`
  - momentum gates from the internal note:
    - `p_Lambda >= 0.42 GeV/c`
    - `p_Sigma >= 0.80 GeV/c`
  - inclusive in the rest of the hadronic final state

## Selection And Fit Design
- baseline event selection before ML:
  - quality cuts from software / beam trigger gates
  - fiducial volume cut from `../internal-note/sections/03-signal.tex`
- scalar CNN discriminants carried on the ntuples:
  - `beam vs ext/dirt`
  - `numu_cc vs other beam`
  - `signal vs other numu_cc`
- current fit intent:
  - one global simultaneous 3-score tensor
  - signal region and control regions are interpreted as slabs / corners of the
    same tensor rather than separate disconnected workflows
- prototype tensor target:
  - `4 x 4 x 6`
- tensor-status note:
  - treat `4 x 4 x 6` as a prototype target, not a frozen measurement baseline
  - final binning still depends on occupancy and fit-stability checks
- energy handling:
  - add energy now
  - do not use a global 4D tensor everywhere
  - use the internal-note visible-energy proxy as the first energy-like
    observable
  - nest the energy axis only inside the signal-like part of the 3-score space
- current statistical intent:
  - the final measurement parameter is `kappa`
  - the fit should support a stable `kappa` extraction with validation plots

## Systematics
- include all major uncertainty families
- event-weight implementation anchor:
  - `../searchingforstrangeness/AnalysisTools/EventWeightAnalysis_tool.cc`
- mandatory detector / weight families:
  - detector variations from alternate samples / detvars
  - `weightsGenie`
  - `weightsFlux`
  - `weightsPPFX`
  - `weightsReint`
  - `weightsGenieUp`
  - `weightsGenieDn`
  - `weightSpline`
  - `weightTune`
  - `weightSplineTimesTune`
  - `ppfx_cv`
- dedicated GENIE knob families currently exposed there include at least:
  - `RPA_CCQE_UBGenie`
  - `XSecShape_CCMEC_UBGenie`
  - `AxFFCCQEshape_UBGenie`
  - `VecFFCCQEshape_UBGenie`
  - `DecayAngMEC_UBGenie`
  - `Theta_Delta2Npi_UBGenie`
  - `ThetaDelta2NRad_UBGenie`
  - `NormCCCOH_UBGenie`
  - `NormNCCOH_UBGenie`
  - SCC knobs where present
  - `RootinoFix_UBGenie` where relevant
- analysis-specific uncertainty / validation needs:
  - exposure / normalisation treatment for data-driven components such as `ext`
  - CNN score sim-to-data mismodelling checks
  - run-by-run score-shape stability checks
  - detector-variation span checks against observed run-period differences

## Outputs Needed
- full chain outputs are required:
  - logical `SampleIO` files
  - `DatasetIO` files
  - `EventListIO` files
  - training snapshots with the columns needed upstream
  - `DistributionIO` caches for score / energy observables
  - covariance exports
  - fit manifests
  - fit reports
  - validation and diagnostic plots
  - final `kappa` result or limit output

## Acceptance Criteria
- a stable `kappa` result exists with validation plots

## Current Blockers
- logical samples have not been built yet
- shard-to-logical-sample aggregation is still missing
- training samples are not yet piped through this repo
- this repo still needs an explicit workflow that:
  - processes the samples
  - enforces signal / overlay orthogonality
  - snapshots the columns needed for upstream training

## Expected Workflow
- shard ntuples with attached CNN scores arrive from the upstream campaign /
  inference workflows
- build shard lists / manifests
- aggregate logical samples:
  - `data`
  - `ext`
  - `dirt`
  - `overlay`
  - `signal`
- assemble datasets by run / polarity / campaign
- build event lists
- produce training snapshots for the upstream training workflows
- build score / energy caches
- export covariance products
- run the fit
- produce validation and result plots

## Assumptions And Open Questions
- fixed assumptions for now:
  - `signal` is the broad all-strange-final-state logical stream
  - `overlay` excludes all strange-final-state events
  - the measured neutral-hyperon signal is a narrower subset inside `signal`
  - upstream inference writes the CNN scores back onto the ntuples before this
    repo consumes them
  - the current fit prototype is a global 3-score tensor with nested energy in
    the signal-like region
- still open:
  - exact training snapshot column list
  - final tensor binning after occupancy checks
  - sparsity fallback if `4 x 4 x 6` proves too fine
