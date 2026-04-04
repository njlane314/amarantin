#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/amarantin-syst-reweight.XXXXXX")"
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

SOURCE="${TMP_DIR}/reweight_smoke.cc"
BINARY="${TMP_DIR}/reweight_smoke"
EVENTLIST_PATH="${TMP_DIR}/reweight.eventlist.root"
OTHER_EVENTLIST_PATH="${TMP_DIR}/reweight-other.eventlist.root"
DIST_PATH="${TMP_DIR}/reweight.dists.root"

cat >"${SOURCE}" <<'EOF'
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DatasetIO.hh"
#include "DistributionIO.hh"
#include "EventListIO.hh"
#include "Systematics.hh"

#include "TTree.h"

namespace
{
    bool approx(double lhs, double rhs)
    {
        return std::fabs(lhs - rhs) < 1e-9;
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

    constexpr std::size_t kGenieKnobCount = 53;

    TTree *make_selected_tree(double second_weight_scale,
                              bool include_ppfx)
    {
        auto *tree = new TTree("selected", "selected");
        double x = 0.0;
        double weight = 1.0;
        std::vector<unsigned short> weights_genie;
        std::vector<unsigned short> weights_flux;
        std::vector<unsigned short> weights_ppfx;
        std::vector<unsigned short> weights_genie_up;
        std::vector<unsigned short> weights_genie_dn;
        tree->Branch("x", &x);
        tree->Branch("__w__", &weight);
        tree->Branch("weightsGenie", &weights_genie);
        tree->Branch("weightsFlux", &weights_flux);
        if (include_ppfx)
            tree->Branch("weightsPPFX", &weights_ppfx);
        tree->Branch("weightsGenieUp", &weights_genie_up);
        tree->Branch("weightsGenieDn", &weights_genie_dn);

        x = 0.25;
        weights_genie = {2000, 1000};
        weights_flux = {2000, 1000};
        weights_ppfx = {1500, 1000};
        weights_genie_up.assign(kGenieKnobCount, 1000);
        weights_genie_dn.assign(kGenieKnobCount, 1000);
        weights_genie_up[0] = 1200;
        weights_genie_dn[0] = 800;
        tree->Fill();

        x = 1.25;
        weights_genie = {1000, static_cast<unsigned short>(1000.0 * second_weight_scale)};
        weights_flux = {1000, 3000};
        weights_ppfx = {1000, 500};
        weights_genie_up.assign(kGenieKnobCount, 1000);
        weights_genie_dn.assign(kGenieKnobCount, 1000);
        weights_genie_up[1] = 1500;
        weights_genie_dn[1] = 500;
        tree->Fill();

        return tree;
    }

    void write_eventlist(const std::string &path, double second_weight_scale)
    {
        EventListIO eventlist(path, EventListIO::Mode::kWrite);

        EventListIO::Metadata metadata;
        metadata.dataset_path = "synthetic.dataset.root";
        metadata.dataset_context = "smoke";
        metadata.event_tree_name = "EventSelectionFilter";
        metadata.subrun_tree_name = "SubRun";
        metadata.selection_name = "raw";
        metadata.selection_expr = "selected != 0";
        eventlist.write_metadata(metadata);

        DatasetIO::Sample sample;
        sample.origin = DatasetIO::Sample::Origin::kOverlay;
        sample.variation = DatasetIO::Sample::Variation::kNominal;
        sample.beam = DatasetIO::Sample::Beam::kNuMI;
        sample.polarity = DatasetIO::Sample::Polarity::kFHC;

        TTree *selected_tree = make_selected_tree(second_weight_scale, true);
        TTree *subrun_tree = make_subrun_tree();
        eventlist.write_sample("beam_ppfx", sample, selected_tree, subrun_tree, "SubRun");
        delete selected_tree;
        delete subrun_tree;

        selected_tree = make_selected_tree(second_weight_scale, false);
        subrun_tree = make_subrun_tree();
        eventlist.write_sample("beam_flux", sample, selected_tree, subrun_tree, "SubRun");
        delete selected_tree;
        delete subrun_tree;

        eventlist.flush();
    }
}

int main(int argc, char **argv)
{
    if (argc != 4)
        throw std::runtime_error("reweight_smoke: expected eventlist, dist, and other-eventlist paths");

    write_eventlist(argv[1], 3.0);
    write_eventlist(argv[3], 4.0);

    syst::HistogramSpec spec;
    spec.branch_expr = "x";
    spec.nbins = 2;
    spec.xmin = 0.0;
    spec.xmax = 2.0;

    syst::SystematicsOptions options;
    options.enable_memory_cache = false;
    options.persistent_cache = syst::CachePolicy::kComputeIfMissing;
    options.cache_nbins = 4;
    options.enable_genie = true;
    options.enable_genie_knobs = true;
    options.enable_flux = true;
    options.build_full_covariance = true;
    options.retain_universe_histograms = true;
    options.enable_eigenmode_compression = false;

    {
        EventListIO eventlist(argv[1], EventListIO::Mode::kRead);
        DistributionIO distfile(argv[2], DistributionIO::Mode::kUpdate);
        const syst::SystematicsResult result =
            syst::evaluate(eventlist, distfile, "beam_ppfx", spec, options);
        const DistributionIO::Spectrum cached =
            distfile.read("beam_ppfx", syst::cache_key(spec, options));
        const syst::SystematicsResult flux_only =
            syst::evaluate(eventlist, distfile, "beam_flux", spec, options);

        if (result.loaded_from_persistent_cache)
            throw std::runtime_error("reweight_smoke: first evaluation unexpectedly loaded from cache");
        if (!result.genie)
            throw std::runtime_error("reweight_smoke: missing GENIE result");
        if (!result.flux)
            throw std::runtime_error("reweight_smoke: missing flux result");
        if (cached.genie.covariance.size() != 16)
            throw std::runtime_error("reweight_smoke: canonical family covariance was not persisted");
        if (cached.genie_knob_source_count != static_cast<int>(kGenieKnobCount))
            throw std::runtime_error("reweight_smoke: GENIE knob-pair source count was not persisted");
        if (cached.genie_knob_covariance.size() != 16)
            throw std::runtime_error("reweight_smoke: GENIE knob-pair covariance was not persisted");
        if (cached.genie_knob_source_labels.empty() ||
            cached.genie_knob_source_labels.front() != "AGKYpT1pi_UBGenie")
        {
            throw std::runtime_error("reweight_smoke: GENIE knob-pair labels were not persisted");
        }
        if (result.genie->universe_histograms.size() != 2)
            throw std::runtime_error("reweight_smoke: retained universe histograms missing");
        if (result.genie->covariance.size() != 4)
            throw std::runtime_error("reweight_smoke: expected rebinned covariance from canonical cache");
        if (result.genie_knob_source_count != static_cast<int>(kGenieKnobCount))
            throw std::runtime_error("reweight_smoke: missing GENIE knob-pair source count");
        if (!approx(result.genie_knobs.up[0], 1.2) ||
            !approx(result.genie_knobs.down[0], 0.8) ||
            !approx(result.genie_knobs.up[1], 1.5) ||
            !approx(result.genie_knobs.down[1], 0.5))
        {
            throw std::runtime_error("reweight_smoke: unexpected GENIE knob-pair envelope");
        }
        if (result.flux->branch_name != "weightsPPFX")
            throw std::runtime_error("reweight_smoke: PPFX branch was not preferred for the logical flux family");
        if (!flux_only.flux || flux_only.flux->branch_name != "weightsFlux")
            throw std::runtime_error("reweight_smoke: weightsFlux fallback was not used");
        if (!approx(result.flux->sigma[0], std::sqrt(0.125)) ||
            !approx(result.flux->sigma[1], std::sqrt(0.125)))
        {
            throw std::runtime_error("reweight_smoke: unexpected PPFX sigma values");
        }
        if (!approx(flux_only.flux->sigma[0], std::sqrt(0.5)) ||
            !approx(flux_only.flux->sigma[1], std::sqrt(2.0)))
        {
            throw std::runtime_error("reweight_smoke: unexpected weightsFlux sigma values");
        }
        if (!approx(result.genie->sigma[0], std::sqrt(0.5)) ||
            !approx(result.genie->sigma[1], std::sqrt(2.0)))
            throw std::runtime_error("reweight_smoke: unexpected sigma values");
        if (!approx(result.genie->covariance[0], 0.5) ||
            !approx(result.genie->covariance[1], 0.0) ||
            !approx(result.genie->covariance[2], 0.0) ||
            !approx(result.genie->covariance[3], 2.0))
            throw std::runtime_error("reweight_smoke: unexpected covariance values");
    }

    options.persistent_cache = syst::CachePolicy::kLoadOnly;

    {
        EventListIO eventlist(argv[1], EventListIO::Mode::kRead);
        DistributionIO distfile(argv[2], DistributionIO::Mode::kRead);
        const syst::SystematicsResult result =
            syst::evaluate(eventlist, distfile, "beam_flux", spec, options);

        if (!result.loaded_from_persistent_cache)
            throw std::runtime_error("reweight_smoke: cached evaluation did not load from persistent cache");
        if (!result.genie || result.genie->universe_histograms.size() != 2)
            throw std::runtime_error("reweight_smoke: cached universe family payload missing");
        if (result.genie_knob_source_count != static_cast<int>(kGenieKnobCount))
            throw std::runtime_error("reweight_smoke: cached GENIE knob-pair payload missing");
    }

    {
        EventListIO other_eventlist(argv[3], EventListIO::Mode::kRead);
        DistributionIO distfile(argv[2], DistributionIO::Mode::kRead);
        try
        {
            (void)syst::evaluate(other_eventlist, distfile, "beam_flux", spec, options);
            throw std::runtime_error("reweight_smoke: metadata mismatch was not rejected");
        }
        catch (const std::runtime_error &error)
        {
            const std::string message = error.what();
            if (message.find("does not match event list") == std::string::npos)
                throw;
        }
    }

    std::cout << "systematics_reweight_smoke=ok\n";
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

"${BINARY}" "${EVENTLIST_PATH}" "${DIST_PATH}" "${OTHER_EVENTLIST_PATH}"
