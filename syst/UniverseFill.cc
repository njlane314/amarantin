#include "bits/Detail.hh"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

#include "TTree.h"
#include "TTreeFormula.h"

namespace
{
    const std::vector<std::string> &genie_knob_source_labels()
    {
        static const std::vector<std::string> labels = {
            "AGKYpT1pi_UBGenie",
            "AGKYxF1pi_UBGenie",
            "AhtBY_UBGenie",
            "AxFFCCQEshape_UBGenie",
            "BhtBY_UBGenie",
            "CV1uBY_UBGenie",
            "CV2uBY_UBGenie",
            "DecayAngMEC_UBGenie",
            "EtaNCEL_UBGenie",
            "FrAbs_N_UBGenie",
            "FrAbs_pi_UBGenie",
            "FrCEx_N_UBGenie",
            "FrCEx_pi_UBGenie",
            "FrInel_N_UBGenie",
            "FrInel_pi_UBGenie",
            "FrPiProd_N_UBGenie",
            "FrPiProd_pi_UBGenie",
            "FracDelta_CCMEC_UBGenie",
            "FracPN_CCMEC_UBGenie",
            "MFP_N_UBGenie",
            "MFP_pi_UBGenie",
            "MaCCQE_UBGenie",
            "MaCCRES_UBGenie",
            "MaNCEL_UBGenie",
            "MaNCRES_UBGenie",
            "MvCCRES_UBGenie",
            "MvNCRES_UBGenie",
            "NonRESBGvbarnCC1pi_UBGenie",
            "NonRESBGvbarnCC2pi_UBGenie",
            "NonRESBGvbarnNC1pi_UBGenie",
            "NonRESBGvbarnNC2pi_UBGenie",
            "NonRESBGvbarpCC1pi_UBGenie",
            "NonRESBGvbarpCC2pi_UBGenie",
            "NonRESBGvbarpNC1pi_UBGenie",
            "NonRESBGvbarpNC2pi_UBGenie",
            "NonRESBGvnCC1pi_UBGenie",
            "NonRESBGvnCC2pi_UBGenie",
            "NonRESBGvnNC1pi_UBGenie",
            "NonRESBGvnNC2pi_UBGenie",
            "NonRESBGvpCC1pi_UBGenie",
            "NonRESBGvpCC2pi_UBGenie",
            "NonRESBGvpNC1pi_UBGenie",
            "NonRESBGvpNC2pi_UBGenie",
            "NormCCMEC_UBGenie",
            "NormNCMEC_UBGenie",
            "RDecBR1eta_UBGenie",
            "RDecBR1gamma_UBGenie",
            "RPA_CCQE_UBGenie",
            "Theta_Delta2Npi_UBGenie",
            "TunedCentralValue_UBGenie",
            "VecFFCCQEshape_UBGenie",
            "XSecShape_CCMEC_UBGenie",
            "splines_general_Spline",
        };
        return labels;
    }

    double sanitise_universe_weight(double weight)
    {
        if (!std::isfinite(weight) || weight <= 0.0)
            return 1.0;
        return weight;
    }

    double decode_universe_weight(unsigned short raw_weight)
    {
        return sanitise_universe_weight(static_cast<double>(raw_weight) / 1000.0);
    }

    int find_bin(const syst::HistogramSpec &spec, double value)
    {
        if (!std::isfinite(value))
            return -1;
        if (value < spec.xmin || value > spec.xmax)
            return -1;
        if (value == spec.xmax)
            return spec.nbins - 1;

        const double width = (spec.xmax - spec.xmin) / static_cast<double>(spec.nbins);
        if (width <= 0.0)
            return -1;

        const int bin = static_cast<int>((value - spec.xmin) / width);
        if (bin < 0 || bin >= spec.nbins)
            return -1;
        return bin;
    }

    std::optional<syst::detail::UniverseAccumulator>
    make_universe_family(TTree *tree, const char *branch_name)
    {
        if (!tree || !branch_name || !tree->GetBranch(branch_name))
            return std::nullopt;

        syst::detail::UniverseAccumulator family;
        family.branch_name = branch_name;
        tree->SetBranchAddress(family.branch_name.c_str(), &family.raw);
        return family;
    }

    std::optional<syst::detail::UniverseAccumulator>
    make_flux_family(TTree *tree)
    {
        if (!tree)
            return std::nullopt;

        if (tree->GetBranch("weightsPPFX"))
            return make_universe_family(tree, "weightsPPFX");
        if (tree->GetBranch("weightsFlux"))
            return make_universe_family(tree, "weightsFlux");
        return std::nullopt;
    }

    std::optional<syst::detail::PairedShiftAccumulator>
    make_genie_knob_pairs(TTree *tree)
    {
        if (!tree ||
            !tree->GetBranch("weightsGenieUp") ||
            !tree->GetBranch("weightsGenieDn"))
        {
            return std::nullopt;
        }

        syst::detail::PairedShiftAccumulator paired;
        paired.up_branch_name = "weightsGenieUp";
        paired.down_branch_name = "weightsGenieDn";
        paired.source_labels = genie_knob_source_labels();
        tree->SetBranchAddress(paired.up_branch_name.c_str(), &paired.raw_up);
        tree->SetBranchAddress(paired.down_branch_name.c_str(), &paired.raw_down);
        return paired;
    }
}

namespace syst::detail
{
    void UniverseAccumulator::ensure_size(int nbins)
    {
        if (!raw)
            return;
        if (n_universes == 0)
        {
            n_universes = raw->size();
            histograms.assign(static_cast<std::size_t>(nbins) * n_universes, 0.0);
        }
    }

    void UniverseAccumulator::accumulate(int bin, int nbins, double base_weight)
    {
        ensure_size(nbins);
        if (n_universes == 0 || !raw)
            return;

        const std::size_t offset = static_cast<std::size_t>(bin) * n_universes;
        const std::size_t n = std::min(n_universes, raw->size());
        for (std::size_t universe = 0; universe < n; ++universe)
            histograms[offset + universe] += base_weight * decode_universe_weight((*raw)[universe]);
    }

    void PairedShiftAccumulator::ensure_size(int nbins)
    {
        if (source_labels.empty())
            return;
        if (shift_vectors.empty())
        {
            shift_vectors.assign(static_cast<std::size_t>(nbins) * source_labels.size(), 0.0);
        }
    }

    void PairedShiftAccumulator::accumulate(int bin, int nbins, double base_weight)
    {
        ensure_size(nbins);
        if (!raw_up || !raw_down || source_labels.empty())
            return;
        if (raw_up->size() != source_labels.size() || raw_down->size() != source_labels.size())
        {
            throw std::runtime_error(
                "SystematicsEngine: GENIE knob-pair payload size does not match the reviewed local knob contract");
        }

        for (std::size_t source = 0; source < source_labels.size(); ++source)
        {
            const double up_weight = decode_universe_weight((*raw_up)[source]);
            const double down_weight = decode_universe_weight((*raw_down)[source]);
            const double shift = 0.5 * base_weight * (up_weight - down_weight);
            shift_vectors[static_cast<std::size_t>(source * nbins + bin)] += shift;
        }
    }

    SampleComputation compute_sample(TTree *tree,
                                     const HistogramSpec &spec,
                                     const SystematicsOptions &options)
    {
        if (!tree)
            throw std::runtime_error("SystematicsEngine: missing selected tree");
        if (spec.branch_expr.empty())
            throw std::runtime_error("SystematicsEngine: branch_expr is required");
        if (spec.nbins <= 0)
            throw std::runtime_error("SystematicsEngine: nbins must be positive");
        if (!(spec.xmax > spec.xmin))
            throw std::runtime_error("SystematicsEngine: invalid histogram range");

        SampleComputation out;
        out.nominal.assign(static_cast<std::size_t>(spec.nbins), 0.0);
        out.sumw2.assign(static_cast<std::size_t>(spec.nbins), 0.0);

        double central_weight = 1.0;
        tree->SetBranchAddress(kCentralWeightBranch, &central_weight);

        TTreeFormula observable("systematics_observable", spec.branch_expr.c_str(), tree);
        std::unique_ptr<TTreeFormula> selection;
        if (!spec.selection_expr.empty())
            selection.reset(new TTreeFormula("systematics_selection", spec.selection_expr.c_str(), tree));

        if (options.enable_genie_knobs)
            out.genie_knobs = make_genie_knob_pairs(tree);
        if (options.enable_genie)
            out.genie = make_universe_family(tree, "weightsGenie");
        if (options.enable_flux)
            out.flux = make_flux_family(tree);
        if (options.enable_reint)
            out.reint = make_universe_family(tree, "weightsReint");

        const Long64_t n_entries = tree->GetEntries();
        for (Long64_t entry = 0; entry < n_entries; ++entry)
        {
            tree->GetEntry(entry);

            if (selection && selection->EvalInstance() == 0.0)
                continue;

            const double value = observable.EvalInstance();
            const int bin = find_bin(spec, value);
            if (bin < 0)
                continue;

            out.nominal[static_cast<std::size_t>(bin)] += central_weight;
            out.sumw2[static_cast<std::size_t>(bin)] += central_weight * central_weight;

            if (out.genie_knobs)
                out.genie_knobs->accumulate(bin, spec.nbins, central_weight);
            if (out.genie)
                out.genie->accumulate(bin, spec.nbins, central_weight);
            if (out.flux)
                out.flux->accumulate(bin, spec.nbins, central_weight);
            if (out.reint)
                out.reint->accumulate(bin, spec.nbins, central_weight);
        }

        return out;
    }
}
