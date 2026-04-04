# ccnumu_hyperon

Living analysis brief for the Semi-Inclusive Charged-Current Muon-Flavour
Neutral-Hyperon Analysis.

This file is analysis-facing working memory. Update it whenever the sample
rules, signal definition, fit structure, or training-snapshot needs change.

`DESIGN.md` remains the source of truth for repository architecture and coding
style.

## Identity
- slug: `ccnumu_hyperon`
- title: `Semi-Inclusive Charged-Current Muon-Flavour Neutral-Hyperon Analysis`
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
- preferred logical sample keys:
  - `data`
  - `ext`
  - `dirt`
  - `overlay`
  - `strange`
- compatibility note:
  - `signal` remains a supported legacy alias for the broad logical
    all-strange stream until the repo can take a deliberate rename pass
- intended roles:
  - `data`: beam-on observed events
  - `ext`: beam-off / external background sample
  - `dirt`: dirt background MC
  - `overlay`: nominal inclusive neutrino interaction MC after strange-final-
    state removal
  - `strange`: broad strange-final-state MC stream used for sample
    construction, not the POI itself
- orthogonality rule:
  - `strange` contains all events with a strange hadron in the final state
  - `overlay` excludes all events with strange hadrons in the final state
  - orthogonality is enforced event-by-event from truth, not from upstream
    sample labels
  - missing truth fields required for orthogonality are a hard failure
- workflow implication:
  - this repo owns the sample-orthogonality logic and should apply it before
    downstream dataset / event-list / training-snapshot production

## Measurement Contract
- operational contract file:
  - `ana/ccnumu_hyperon_measurement_contract.json`
- provenance anchors:
  - `../searchingforstrangeness/AnalysisTools/SignalAnalysis_tool.cc`
  - `../internal-note/sections/03-signal.tex`
- observable:
  - event-level phase-space cross section for `CC muon-flavour + >= 1
    qualifying neutral hyperon`
- parameter of interest:
  - `kappa = sigma_phase_space / sigma_phase_space_nominal`
- counting rule:
  - events with multiple qualifying neutral hyperons count once
- flavour scope frozen for now:
  - combined `nu_mu` + `nubar_mu`
  - the reported observable is explicitly flux-averaged over the included beam
    compositions
- scaling rule:
  - `kappa` scales only `measurement_signal`
  - out-of-phase-space strange events remain background
- per-channel per-bin fit model:
  - `N_pred = N_ext + N_dirt + N_nonstrange_overlay +
    N_other_strange_background + kappa * N_measurement_signal`

## Truth Categorization And Ancestry
- required mutually exclusive truth labels:
  - `measurement_signal`
  - `neutral_hyperon_out_of_phase_space`
  - `other_strange_background`
  - `detector_secondary_hyperon_background`
  - `nonstrange_overlay`
- truth inputs required by the operational contract include:
  - `truth_has_strange_fs`
  - `truth_has_fs_lambda0`
  - `truth_has_fs_sigma0`
  - `truth_has_g4_lambda0`
  - `truth_has_g4_lambda0_from_sigma0`
  - `truth_fs_lambda0_p`
  - `truth_fs_sigma0_p`
- ancestry rule:
  - direct exit-state `Lambda0` counts as signal if it satisfies phase space
  - direct exit-state `Sigma0` counts as signal if it satisfies phase space,
    including cases represented downstream as a G4 `Lambda0` descendant from
    that `Sigma0`
  - pure detector-secondary `Lambda0` with no qualifying exit-state `Lambda0`
    or `Sigma0` ancestor is background
- fiducial-volume rule:
  - true FV defines the signal
  - reco FV defines the event selection
  - FV boundary migrations are modeled explicitly

## Selection And Fit Design
- baseline event selection before ML:
  - quality cuts from software / beam trigger gates
  - reco fiducial selection
- scalar CNN discriminants carried on the ntuples:
  - `beam vs ext/dirt`
  - `muon_flavour_cc vs other beam`
  - `measurement_signal vs other muon_flavour_cc`
- simultaneous fit channels are:
  - `run_period x polarity x control_or_signal_region`
- region semantics:
  - low `beam` score: `ext/dirt` control
  - high `beam`, low `muon_flavour_cc` score: other-beam control
  - high `beam`, high `muon_flavour_cc`, low `measurement_signal` score:
    non-signal CC control
  - high all three scores: measurement-signal-enriched region with nested
    visible energy
- implementation rule:
  - nested visible-energy bins are explicit subchannels, not a ragged global
    4D tensor
- binning rule:
  - start from score-quantile or logit-space bins
  - merge bins until occupancy, MC-stat, and fit-stability criteria are met
  - include per-bin MC statistical nuisances
- current fit intent:
  - keep one simultaneous SR/CR model with a shared `kappa`
  - do not let the broad logical `strange` sample masquerade as the fit POI
  - treat `other_strange_background` as an explicit fit component, not generic
    overlay
  - prefer covariance-first family payloads at the fit boundary; stored
    eigenmodes are optional derived views, not the only fit-ready contract
  - sigma-only family fallback is a diagonal approximation and should be
    surfaced explicitly as lower-fidelity than covariance-backed payloads

## Systematics
- include all major uncertainty families
- event-weight implementation anchor:
  - `../searchingforstrangeness/AnalysisTools/EventWeightAnalysis_tool.cc`
- canonical nominal weight recipe:
  - `__w_norm__`
  - `weightSplineTimesTune`, or `weightSpline * weightTune` when the combined
    branch is absent
  - `ppfx_cv` for NuMI when present
  - `RootinoFix` when present
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
    by run/polarity exposure or trigger ratio, not POT
  - a dedicated nuisance for `other_strange_background`
  - CNN score sim-to-data mismodelling checks
  - run-by-run score-shape stability checks
  - detector-variation span checks against observed run-period differences
  - validate detector-CV compatibility before any fit path that recenters
    detector source shifts around the analysis nominal

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

## Training Snapshot Contract
- snapshots are produced only after orthogonality and baseline event cleaning
- each row must include:
  - stable event UID
  - run period
  - polarity
  - truth label
  - nominal weight
  - split label
  - CNN model tag
- train / validation / test split is deterministic from event identity to
  prevent leakage across reruns

## Acceptance Criteria
- Asimov closure for injected `kappa`
- pseudoexperiment pull mean and width within predefined tolerances
- stability under run-split and rebinning tests
- no pathological nuisance pulls or near-singular covariance structure
- acceptable post-fit agreement in all control regions
- predefined result / limit procedure frozen before unblinding

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
  - `strange` (`signal` remains a supported legacy alias)
- assemble datasets by run / polarity / campaign
- build event lists
- produce training snapshots for the upstream training workflows
- build score / energy caches
- export covariance products
- run the fit
- produce validation and result plots

## Assumptions And Open Questions
- fixed assumptions for now:
  - `strange` is the broad all-strange-final-state logical stream
  - `overlay` excludes all strange-final-state events
  - the measured neutral-hyperon signal is a narrower subset inside `strange`
  - upstream inference writes the CNN scores back onto the ntuples before this
    repo consumes them
  - the simultaneous fit is built from explicit SR/CR channels with nested
    energy in the signal-like region
- still open:
  - final tensor binning after occupancy checks
  - sparsity fallback if `4 x 4 x 6` proves too fine
