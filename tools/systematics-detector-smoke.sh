#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/amarantin-syst-detector.XXXXXX")"
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
require_library "Syst"

SOURCE="${TMP_DIR}/detector_smoke.cc"
BINARY="${TMP_DIR}/detector_smoke"
EVENTLIST_PATH="${TMP_DIR}/detector.eventlist.root"

cat >"${SOURCE}" <<'EOF'
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "DatasetIO.hh"
#include "EventListIO.hh"
#include "Systematics.hh"

#include "TTree.h"

namespace
{
    bool approx(double lhs, double rhs)
    {
        return std::fabs(lhs - rhs) < 1e-9;
    }

    TTree *make_selected_tree(double first, double second, double third = -1.0)
    {
        auto *tree = new TTree("selected", "selected");
        double x = 0.0;
        double weight = 1.0;
        tree->Branch("x", &x);
        tree->Branch("__w__", &weight);

        x = first;
        tree->Fill();
        x = second;
        tree->Fill();
        if (third >= 0.0)
        {
            x = third;
            tree->Fill();
        }
        return tree;
    }

    TTree *make_subrun_tree()
    {
        auto *tree = new TTree("SubRun", "SubRun");
        int run = 1;
        int subrun = 1;
        tree->Branch("run", &run);
        tree->Branch("subRun", &subrun);
        tree->Fill();
        return tree;
    }

    void write_sample(EventListIO &eventlist,
                      const std::string &sample_key,
                      DatasetIO::Sample::Variation variation,
                      const std::string &nominal_key,
                      TTree *selected_tree)
    {
        DatasetIO::Sample sample;
        sample.origin = DatasetIO::Sample::Origin::kData;
        sample.variation = variation;
        sample.beam = DatasetIO::Sample::Beam::kNuMI;
        sample.polarity = DatasetIO::Sample::Polarity::kFHC;
        sample.nominal = nominal_key;

        TTree *subrun_tree = make_subrun_tree();
        eventlist.write_sample(sample_key, sample, selected_tree, subrun_tree, "SubRun");
        delete subrun_tree;
        delete selected_tree;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
        throw std::runtime_error("detector_smoke: expected output eventlist path");

    {
        EventListIO eventlist(argv[1], EventListIO::Mode::kWrite);

        EventListIO::Metadata metadata;
        metadata.dataset_path = "synthetic.dataset.root";
        metadata.dataset_context = "smoke";
        metadata.event_tree_name = "EventSelectionFilter";
        metadata.subrun_tree_name = "SubRun";
        metadata.selection_name = "raw";
        metadata.selection_expr = "selected != 0";
        eventlist.write_metadata(metadata);

        write_sample(eventlist,
                     "beam",
                     DatasetIO::Sample::Variation::kNominal,
                     "",
                     make_selected_tree(0.25, 1.25));
        write_sample(eventlist,
                     "beam-sce",
                     DatasetIO::Sample::Variation::kDetector,
                     "beam",
                     make_selected_tree(0.25, 0.75, 0.75));

        eventlist.flush();
    }

    EventListIO eventlist(argv[1], EventListIO::Mode::kRead);

    syst::HistogramSpec spec;
    spec.branch_expr = "x";
    spec.nbins = 2;
    spec.xmin = 0.0;
    spec.xmax = 2.0;

    syst::SystematicsOptions options;
    options.enable_detector = true;
    options.detector_sample_keys = {"beam-sce"};

    const syst::SystematicsResult result = syst::evaluate(eventlist, "beam", spec, options);

    if (result.nominal.size() != 2 || !approx(result.nominal[0], 1.0) || !approx(result.nominal[1], 1.0))
        throw std::runtime_error("detector_smoke: unexpected nominal histogram");
    if (result.detector.down.size() != 2 || result.detector.up.size() != 2)
        throw std::runtime_error("detector_smoke: missing detector envelope");
    if (!approx(result.detector.down[0], 0.0) || !approx(result.detector.up[0], 3.0) ||
        !approx(result.detector.down[1], 0.0) || !approx(result.detector.up[1], 2.0))
        throw std::runtime_error("detector_smoke: unexpected detector envelope");
    if (result.total_up.size() != 2 || result.total_down.size() != 2)
        throw std::runtime_error("detector_smoke: missing total envelope");
    if (!approx(result.total_up[0], 3.0) || !approx(result.total_down[0], 0.0) ||
        !approx(result.total_up[1], 2.0) || !approx(result.total_down[1], 0.0))
        throw std::runtime_error("detector_smoke: unexpected total envelope");

    std::cout << "systematics_detector_smoke=ok\n";
    return 0;
}
EOF

read -r -a ROOT_CFLAGS <<<"$(root-config --cflags)"
read -r -a ROOT_LIBS <<<"$(root-config --libs)"

"${CXX:-c++}" \
  -std=c++17 \
  -I"${ROOT_DIR}/io" \
  -I"${ROOT_DIR}/syst" \
  "${ROOT_CFLAGS[@]}" \
  "${SOURCE}" \
  -L"${BUILD_DIR}/lib" \
  -Wl,-rpath,"${BUILD_DIR}/lib" \
  -lSyst \
  -lIO \
  "${ROOT_LIBS[@]}" \
  -o "${BINARY}"

export DYLD_LIBRARY_PATH="${BUILD_DIR}/lib${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}"
export LD_LIBRARY_PATH="${BUILD_DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

"${BINARY}" "${EVENTLIST_PATH}"
