#include "bits/Detail.hh"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "TTree.h"
#include "TTreeFormula.h"

namespace
{
    constexpr const char *kCentralWeightBranch = "__w__";

    class CheckedBranchBindings
    {
    public:
        explicit CheckedBranchBindings(TTree &tree) : tree_(tree) {}

        ~CheckedBranchBindings()
        {
            for (TBranch *branch : bound_branches_)
                tree_.ResetBranchAddress(branch);
        }

        CheckedBranchBindings(const CheckedBranchBindings &) = delete;
        CheckedBranchBindings &operator=(const CheckedBranchBindings &) = delete;

        template <typename Address>
        int bind_branch(const std::string &branch_name, Address address)
        {
            TBranch *bound_branch = nullptr;
            const int binding_status =
                tree_.SetBranchAddress(branch_name.c_str(), address, &bound_branch);
            if (binding_status < 0)
            {
                throw std::runtime_error(
                    "syst: tree " + std::string(tree_.GetName()) + ", branch " + branch_name +
                    " has an incompatible persisted type for calculation binding (ROOT status " +
                    std::to_string(binding_status) + ")");
            }
            if (!bound_branch)
            {
                throw std::runtime_error(
                    "syst: ROOT did not return the bound branch " + branch_name +
                    " from tree " + tree_.GetName());
            }

            bound_branches_.push_back(bound_branch);
            return binding_status;
        }

        template <typename Address>
        void bind_exact_branch(const std::string &branch_name, Address address)
        {
            const int binding_status = bind_branch(branch_name, address);
            if (binding_status != TTree::kMatch)
            {
                throw std::runtime_error(
                    "syst: tree " + std::string(tree_.GetName()) + ", branch " + branch_name +
                    " must exactly match its packed calculation type (ROOT status " +
                    std::to_string(binding_status) + ")");
            }
        }

    private:
        TTree &tree_;
        std::vector<TBranch *> bound_branches_;
    };

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

    void require_valid_formula(TTreeFormula &formula,
                               const std::string &label,
                               const std::string &expression)
    {
        if (formula.GetTree() && formula.GetNdim() > 0)
            return;

        throw std::runtime_error("syst: failed to compile " + label +
                                 " expression: " + expression);
    }

    std::optional<syst::detail::UniverseAccumulator>
    make_universe_family(TTree *tree, const char *branch_name)
    {
        if (!tree || !branch_name || !tree->GetBranch(branch_name))
            return std::nullopt;

        syst::detail::UniverseAccumulator accumulator;
        accumulator.branch_name = branch_name;
        return accumulator;
    }

    void bind_universe_weights(CheckedBranchBindings &branch_bindings,
                               const syst::detail::UniverseAccumulator &accumulator,
                               std::vector<unsigned short> *&universe_weights)
    {
        universe_weights = nullptr;
        branch_bindings.bind_exact_branch(accumulator.branch_name,
                                          &universe_weights);
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

        syst::detail::PairedShiftAccumulator knob_pairs;
        knob_pairs.up_branch_name = "weightsGenieUp";
        knob_pairs.down_branch_name = "weightsGenieDn";
        knob_pairs.source_labels = genie_knob_source_labels();
        return knob_pairs;
    }

    void bind_genie_knob_weights(CheckedBranchBindings &branch_bindings,
                                 const syst::detail::PairedShiftAccumulator &knob_pairs,
                                 std::vector<unsigned short> *&up_weights,
                                 std::vector<unsigned short> *&down_weights)
    {
        up_weights = nullptr;
        down_weights = nullptr;
        branch_bindings.bind_exact_branch(knob_pairs.up_branch_name, &up_weights);
        branch_bindings.bind_exact_branch(knob_pairs.down_branch_name,
                                          &down_weights);
    }
}

namespace syst::detail
{
    void UniverseAccumulator::accumulate(
        int bin,
        int nbins,
        double base_weight,
        const std::vector<unsigned short> *universe_weights)
    {
        if (!universe_weights)
            return;
        if (n_universes == 0)
        {
            n_universes = universe_weights->size();
            histograms.assign(static_cast<std::size_t>(nbins) * n_universes, 0.0);
        }
        if (n_universes == 0)
            return;
        if (universe_weights->size() != n_universes)
        {
            throw std::runtime_error(
                "syst: universe family " + branch_name +
                " changed size across entries");
        }

        const std::size_t offset = static_cast<std::size_t>(bin) * n_universes;
        for (std::size_t universe = 0; universe < n_universes; ++universe)
            histograms[offset + universe] +=
                base_weight * decode_universe_weight((*universe_weights)[universe]);
    }

    void PairedShiftAccumulator::accumulate(
        int bin,
        int nbins,
        double base_weight,
        const std::vector<unsigned short> *up_weights,
        const std::vector<unsigned short> *down_weights)
    {
        if (!up_weights || !down_weights || source_labels.empty())
            return;

        const std::size_t up_size = up_weights->size();
        const std::size_t down_size = down_weights->size();
        if (up_size == 0 && down_size == 0)
            return;
        if (shift_vectors.empty())
            shift_vectors.assign(static_cast<std::size_t>(nbins) * source_labels.size(), 0.0);
        if (up_size != source_labels.size() || down_size != source_labels.size())
        {
            throw std::runtime_error(
                "syst: GENIE knob-pair payload size does not match the reviewed local knob contract");
        }

        for (std::size_t source = 0; source < source_labels.size(); ++source)
        {
            const double up_weight = decode_universe_weight((*up_weights)[source]);
            const double down_weight = decode_universe_weight((*down_weights)[source]);
            const double shift = 0.5 * base_weight * (up_weight - down_weight);
            shift_vectors[static_cast<std::size_t>(source * nbins + bin)] += shift;
        }
    }

    ComputedSample compute_sample(TTree *tree,
                                  const HistogramSpec &spec,
                                  const SystematicsOptions &options)
    {
        if (!tree)
            throw std::runtime_error("syst: missing selected tree");
        if (spec.branch_expr.empty())
            throw std::runtime_error("syst: branch_expr is required");
        if (spec.nbins <= 0)
            throw std::runtime_error("syst: nbins must be positive");
        if (!(spec.xmax > spec.xmin))
            throw std::runtime_error("syst: invalid histogram range");

        ComputedSample result;
        result.nominal.assign(static_cast<std::size_t>(spec.nbins), 0.0);
        result.sumw2.assign(static_cast<std::size_t>(spec.nbins), 0.0);

        double central_weight = 1.0;
        if (!tree->GetBranch(kCentralWeightBranch))
        {
            throw std::runtime_error(
                std::string("syst: missing required selected-tree branch ") +
                kCentralWeightBranch);
        }
        CheckedBranchBindings branch_bindings(*tree);
        branch_bindings.bind_branch(kCentralWeightBranch, &central_weight);

        std::vector<unsigned short> *genie_universe_weights = nullptr;
        std::vector<unsigned short> *flux_universe_weights = nullptr;
        std::vector<unsigned short> *reint_universe_weights = nullptr;
        std::vector<unsigned short> *genie_knob_up_weights = nullptr;
        std::vector<unsigned short> *genie_knob_down_weights = nullptr;

        TTreeFormula observable("systematics_observable", spec.branch_expr.c_str(), tree);
        require_valid_formula(observable, "observable", spec.branch_expr);
        std::unique_ptr<TTreeFormula> selection;
        if (!spec.selection_expr.empty())
        {
            selection.reset(new TTreeFormula("systematics_selection", spec.selection_expr.c_str(), tree));
            require_valid_formula(*selection, "selection", spec.selection_expr);
        }

        if (options.enable_genie_knobs)
        {
            result.genie_knobs = make_genie_knob_pairs(tree);
            if (result.genie_knobs)
            {
                bind_genie_knob_weights(branch_bindings,
                                        *result.genie_knobs,
                                        genie_knob_up_weights,
                                        genie_knob_down_weights);
            }
        }
        if (options.enable_genie)
        {
            result.genie = make_universe_family(tree, "weightsGenie");
            if (result.genie)
                bind_universe_weights(branch_bindings, *result.genie, genie_universe_weights);
        }
        if (options.enable_flux)
        {
            result.flux = make_flux_family(tree);
            if (result.flux)
                bind_universe_weights(branch_bindings, *result.flux, flux_universe_weights);
        }
        if (options.enable_reint)
        {
            result.reint = make_universe_family(tree, "weightsReint");
            if (result.reint)
                bind_universe_weights(branch_bindings, *result.reint, reint_universe_weights);
        }

        const Long64_t n_entries = tree->GetEntries();
        for (Long64_t entry = 0; entry < n_entries; ++entry)
        {
            const int bytes_read = tree->GetEntry(entry);
            if (bytes_read < 0)
            {
                throw std::runtime_error(
                    "syst: failed to read entry " + std::to_string(entry) + " from tree " +
                    tree->GetName() + " (ROOT status " + std::to_string(bytes_read) + ")");
            }

            if (selection && selection->EvalInstance() == 0.0)
                continue;

            const double value = observable.EvalInstance();
            const int bin = find_bin(spec, value);
            if (bin < 0)
                continue;

            result.nominal[static_cast<std::size_t>(bin)] += central_weight;
            result.sumw2[static_cast<std::size_t>(bin)] += central_weight * central_weight;

            if (result.genie_knobs)
            {
                result.genie_knobs->accumulate(bin,
                                               spec.nbins,
                                               central_weight,
                                               genie_knob_up_weights,
                                               genie_knob_down_weights);
            }
            if (result.genie)
                result.genie->accumulate(bin,
                                         spec.nbins,
                                         central_weight,
                                         genie_universe_weights);
            if (result.flux)
                result.flux->accumulate(bin,
                                        spec.nbins,
                                        central_weight,
                                        flux_universe_weights);
            if (result.reint)
                result.reint->accumulate(bin,
                                         spec.nbins,
                                         central_weight,
                                         reint_universe_weights);
        }

        return result;
    }
}
