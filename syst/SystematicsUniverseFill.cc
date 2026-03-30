#include "bits/SystematicsInternal.hh"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

#include "TTree.h"
#include "TTreeFormula.h"

namespace
{
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

        if (options.enable_genie && tree->GetBranch("weightsGenie"))
        {
            UniverseAccumulator family;
            family.branch_name = "weightsGenie";
            tree->SetBranchAddress(family.branch_name.c_str(), &family.raw);
            out.genie = family;
        }
        if (options.enable_flux && tree->GetBranch("weightsPPFX"))
        {
            UniverseAccumulator family;
            family.branch_name = "weightsPPFX";
            tree->SetBranchAddress(family.branch_name.c_str(), &family.raw);
            out.flux = family;
        }
        if (options.enable_reint && tree->GetBranch("weightsReint"))
        {
            UniverseAccumulator family;
            family.branch_name = "weightsReint";
            tree->SetBranchAddress(family.branch_name.c_str(), &family.raw);
            out.reint = family;
        }

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
