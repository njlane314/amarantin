#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR_INPUT="${1:-${AMARANTIN_BUILD_DIR:-build}}"
if [[ "${BUILD_DIR_INPUT}" = /* ]]; then
  BUILD_DIR="${BUILD_DIR_INPUT}"
else
  BUILD_DIR="${ROOT_DIR}/${BUILD_DIR_INPUT}"
fi

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/amarantin-macro-smoke.XXXXXX")"
trap 'rm -rf "${TMP_DIR}"' EXIT

require_library() {
  local stem=$1
  if [[ ! -e "${BUILD_DIR}/lib/lib${stem}.so" && ! -e "${BUILD_DIR}/lib/lib${stem}.dylib" ]]; then
    printf 'missing %s/lib/lib%s.{so,dylib}; build the target first\n' "${BUILD_DIR}" "${stem}" >&2
    exit 1
  fi
}

if ! command -v root-config >/dev/null 2>&1; then
  printf 'root-config not found on PATH\n' >&2
  exit 1
fi

require_library "IO"
require_library "Ana"
require_library "Plot"
require_library "Syst"
require_library "Fit"

SOURCE="${TMP_DIR}/macro_fixture.cc"
BINARY="${TMP_DIR}/macro_fixture"
EVENTLIST_PATH="${TMP_DIR}/macro-smoke.eventlist.root"
DIST_PATH="${TMP_DIR}/macro-smoke.dists.root"
WEIGHTS_LOG="${TMP_DIR}/inspect_weights.log"
SYSTEMATICS_LOG="${TMP_DIR}/inspect_systematics.log"

cat >"${SOURCE}" <<'EOF'
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "DatasetIO.hh"
#include "DistributionIO.hh"
#include "EventListIO.hh"

#include "TTree.h"

namespace
{
    struct SelectedRow
    {
        int run = 0;
        int subrun = 0;
        int evt = 0;
        int selected = 1;
        double topological_score = 0.0;
        double w_norm = 1.0;
        double w_cv = 1.0;
        double w = 1.0;
        double w2 = 1.0;
    };

    TTree *make_selected_tree(const std::vector<SelectedRow> &rows)
    {
        auto *tree = new TTree("selected", "selected");
        int run = 0;
        int subRun = 0;
        int evt = 0;
        int selected = 0;
        double topological_score = 0.0;
        double w_norm = 1.0;
        double w_cv = 1.0;
        double w = 1.0;
        double w2 = 1.0;
        tree->Branch("run", &run);
        tree->Branch("subRun", &subRun);
        tree->Branch("evt", &evt);
        tree->Branch("selected", &selected);
        tree->Branch("topological_score", &topological_score);
        tree->Branch("__w_norm__", &w_norm);
        tree->Branch("__w_cv__", &w_cv);
        tree->Branch("__w__", &w);
        tree->Branch("__w2__", &w2);

        for (const auto &row : rows)
        {
            run = row.run;
            subRun = row.subrun;
            evt = row.evt;
            selected = row.selected;
            topological_score = row.topological_score;
            w_norm = row.w_norm;
            w_cv = row.w_cv;
            w = row.w;
            w2 = row.w2;
            tree->Fill();
        }
        return tree;
    }

    TTree *make_subrun_tree()
    {
        auto *tree = new TTree("SubRun", "SubRun");
        int run = 1;
        int subRun = 1;
        tree->Branch("run", &run);
        tree->Branch("subRun", &subRun);
        tree->Fill();
        return tree;
    }

    void write_eventlist(const std::string &path)
    {
        EventListIO eventlist(path, EventListIO::Mode::kWrite);

        EventListIO::Metadata metadata;
        metadata.dataset_path = "synthetic.dataset.root";
        metadata.dataset_context = "macro-smoke";
        metadata.event_tree_name = "EventSelectionFilter";
        metadata.subrun_tree_name = "SubRun";
        metadata.selection_name = "synthetic";
        metadata.selection_expr = "selected != 0";
        eventlist.write_metadata(metadata);

        DatasetIO::Sample nominal;
        nominal.origin = DatasetIO::Sample::Origin::kOverlay;
        nominal.variation = DatasetIO::Sample::Variation::kNominal;
        nominal.beam = DatasetIO::Sample::Beam::kNuMI;
        nominal.polarity = DatasetIO::Sample::Polarity::kFHC;
        nominal.sample = "beam";

        DatasetIO::Sample detector = nominal;
        detector.variation = DatasetIO::Sample::Variation::kDetector;
        detector.sample = "beam-sce";
        detector.nominal = "beam";

        TTree *beam_selected = make_selected_tree({
            {1, 1, 101, 1, 0.20, 0.50,  1.0,  0.50,   0.2500},
            {1, 1, 102, 1, 0.55, 1.00,  2.0,  2.00,   4.0000},
            {1, 1, 103, 1, 0.85, 0.25, -1.0, -0.25,   0.0625},
        });
        TTree *beam_subrun = make_subrun_tree();
        eventlist.write_sample("beam", nominal, beam_selected, beam_subrun, "SubRun");
        delete beam_selected;
        delete beam_subrun;

        TTree *detector_selected = make_selected_tree({
            {1, 1, 201, 1, 0.25, 1.00, 1.2, 1.20, 1.4400},
            {1, 1, 202, 1, 0.65, 1.00, 1.2, 1.20, 1.4400},
        });
        TTree *detector_subrun = make_subrun_tree();
        eventlist.write_sample("beam-sce", detector, detector_selected, detector_subrun, "SubRun");
        delete detector_selected;
        delete detector_subrun;

        eventlist.flush();
    }

    void write_dist(const std::string &path)
    {
        DistributionIO dist(path, DistributionIO::Mode::kWrite);

        DistributionIO::Metadata metadata;
        metadata.eventlist_path = "synthetic.eventlist.root";
        metadata.build_version = 1;
        dist.write_metadata(metadata);

        DistributionIO::Spectrum spectrum;
        spectrum.spec.sample_key = "beam";
        spectrum.spec.branch_expr = "topological_score";
        spectrum.spec.selection_expr = "__pass_muon__";
        spectrum.spec.nbins = 2;
        spectrum.spec.xmin = 0.0;
        spectrum.spec.xmax = 1.0;
        spectrum.spec.cache_key = "muon_region";
        spectrum.nominal = {1.0, 2.0};
        spectrum.sumw2 = {1.0, 4.0};

        spectrum.detector_source_labels = {"sce", "wire"};
        spectrum.detector_sample_keys = {"beam-sce", "beam-wire"};
        spectrum.detector_source_count = 2;
        spectrum.detector_shift_vectors = {
            0.20, 0.10,
           -0.10, 0.30
        };
        spectrum.detector_covariance = {
            0.04, 0.00,
            0.00, 0.09
        };

        spectrum.genie_knob_source_labels = {"agky"};
        spectrum.genie_knob_source_count = 1;
        spectrum.genie_knob_shift_vectors = {
            0.05, 0.02
        };
        spectrum.genie_knob_covariance = {
            0.0025, 0.0000,
            0.0000, 0.0004
        };

        spectrum.genie.branch_name = "weightsGenie";
        spectrum.genie.n_variations = 3;
        spectrum.genie.eigen_rank = 2;
        spectrum.genie.sigma = {0.10, 0.20};
        spectrum.genie.covariance = {
            0.01, 0.00,
            0.00, 0.04
        };
        spectrum.genie.eigenvalues = {1.5, 0.5};
        spectrum.genie.eigenmodes = {
            0.10, 0.00,
            0.00, 0.20
        };
        spectrum.genie.universe_histograms = {
            1.1, 0.9, 1.0,
            2.2, 1.8, 2.0
        };

        spectrum.flux.branch_name = "weightsPPFX";
        spectrum.flux.n_variations = 2;
        spectrum.flux.sigma = {0.30, 0.40};
        spectrum.flux.universe_histograms = {
            1.2, 0.8,
            2.3, 1.7
        };

        spectrum.reint.branch_name = "weightsReint";
        spectrum.reint.n_variations = 1;
        spectrum.reint.sigma = {0.05, 0.07};
        spectrum.reint.universe_histograms = {
            0.95,
            2.05
        };

        spectrum.total_down = {0.8, 1.7};
        spectrum.total_up = {1.2, 2.4};

        dist.write("beam", "muon_region", spectrum);
        dist.flush();
    }
}

int main(int argc, char **argv)
{
    if (argc != 3)
        throw std::runtime_error("expected eventlist and dist paths");

    write_eventlist(argv[1]);
    write_dist(argv[2]);
    return 0;
}
EOF

read -r -a ROOT_CFLAGS <<<"$(root-config --cflags)"
read -r -a ROOT_LIBS <<<"$(root-config --libs)"

"${CXX:-c++}" \
  -std=c++17 \
  -I"${ROOT_DIR}/io" \
  "${ROOT_CFLAGS[@]}" \
  "${SOURCE}" \
  -L"${BUILD_DIR}/lib" \
  -Wl,-rpath,"${BUILD_DIR}/lib" \
  -lIO \
  "${ROOT_LIBS[@]}" \
  -o "${BINARY}"

export DYLD_LIBRARY_PATH="${BUILD_DIR}/lib${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}"
export LD_LIBRARY_PATH="${BUILD_DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

"${BINARY}" "${EVENTLIST_PATH}" "${DIST_PATH}"

(
  cd "${TMP_DIR}"
  AMARANTIN_BUILD_DIR="${BUILD_DIR}" bash "${ROOT_DIR}/tools/run-macro" inspect_weights "${EVENTLIST_PATH}"
) >"${WEIGHTS_LOG}" 2>&1

grep -F "sample=beam origin=overlay variation=nominal nominal=- entries=3" "${WEIGHTS_LOG}" >/dev/null
grep -F "sum_w=2.250000" "${WEIGHTS_LOG}" >/dev/null
grep -F "negative_w=1 nonfinite_w=0 inconsistent_w=0" "${WEIGHTS_LOG}" >/dev/null
grep -F "sample=beam-sce origin=overlay variation=detector nominal=beam entries=2" "${WEIGHTS_LOG}" >/dev/null

(
  cd "${TMP_DIR}"
  AMARANTIN_BUILD_DIR="${BUILD_DIR}" bash "${ROOT_DIR}/tools/run-macro" inspect_systematics "${DIST_PATH}" beam muon_region
) >"${SYSTEMATICS_LOG}" 2>&1

grep -F "sample=beam cache_key=muon_region nbins=2 branch=topological_score selection=__pass_muon__ yield=3.000000" "${SYSTEMATICS_LOG}" >/dev/null
grep -F "detector_source_count=2 detector_template_count=0 detector_covariance_bins=4 detector_envelope=absent detector_source_labels=sce,wire detector_sample_keys=beam-sce,beam-wire" "${SYSTEMATICS_LOG}" >/dev/null
grep -F "genie_knob_source_count=1 genie_knob_covariance_bins=4 genie_knob_source_labels=agky" "${SYSTEMATICS_LOG}" >/dev/null
grep -F "genie branch=weightsGenie universes=3 eigen_rank=2 sigma_bins=2 covariance_bins=4 eigenvalues=2 eigenmodes=4 universe_hist_bins=6" "${SYSTEMATICS_LOG}" >/dev/null
grep -F "flux branch=weightsPPFX universes=2 eigen_rank=0 sigma_bins=2 covariance_bins=0 eigenvalues=0 eigenmodes=0 universe_hist_bins=4" "${SYSTEMATICS_LOG}" >/dev/null
grep -F "reint branch=weightsReint universes=1 eigen_rank=0 sigma_bins=2 covariance_bins=0 eigenvalues=0 eigenmodes=0 universe_hist_bins=2" "${SYSTEMATICS_LOG}" >/dev/null
grep -F "total_envelope=present" "${SYSTEMATICS_LOG}" >/dev/null

printf 'macro_analysis_smoke=ok\n'
