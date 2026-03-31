#include "EventListBuild.hh"

#include "Cuts.hh"
#include "EventCategory.hh"
#include "SignalDefinition.hh"

#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <TChain.h>
#include <TObjArray.h>
#include <TTree.h>
#include <TTreeFormula.h>

namespace
{
    using RunSubrunKey = std::pair<int, int>;

    cuts::Config cuts_config_for(const ana::BuildConfig &config)
    {
        cuts::Config cuts_config;
        cuts_config.slice_required_count = config.slice_required_count;
        cuts_config.slice_min_topology_score = config.slice_min_topology_score;
        cuts_config.numi_run_boundary = config.numi_run_boundary;
        return cuts_config;
    }

    const char *orthogonal_selection_for_sample(const DatasetIO::Sample &sample)
    {
        using Origin = DatasetIO::Sample::Origin;

        if (sample.origin == Origin::kOverlay)
            return "count_strange == 0";
        if (sample.origin == Origin::kSignal)
            return "count_strange > 0";
        return nullptr;
    }

    std::string build_selection_expression(const DatasetIO::Sample &sample,
                                           TChain &chain,
                                           const std::string &selection_expr)
    {
        const char *orthogonal_expr = orthogonal_selection_for_sample(sample);
        if (!orthogonal_expr)
            return selection_expr;

        if (!chain.GetBranch("count_strange"))
        {
            throw std::runtime_error(
                "ana::build_event_list: sample origin requires count_strange for orthogonal filtering");
        }

        if (selection_expr.empty())
            return orthogonal_expr;

        return "(" + selection_expr + ") && (" + orthogonal_expr + ")";
    }

    std::vector<std::string> branch_names(TTree *tree)
    {
        std::vector<std::string> names;
        if (!tree)
            return names;

        TObjArray *branches = tree->GetListOfBranches();
        if (!branches)
            return names;

        const int n = branches->GetEntries();
        names.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            TObject *obj = branches->At(i);
            if (obj)
                names.emplace_back(obj->GetName());
        }
        return names;
    }

    std::unique_ptr<TTreeFormula> make_optional_formula(const char *name,
                                                        cuts::Preset preset,
                                                        const DatasetIO::Sample &sample,
                                                        const std::vector<std::string> &columns,
                                                        const cuts::Config &config,
                                                        TChain &chain)
    {
        try
        {
            const std::string expr = cuts::expression(preset, sample, columns, config);
            if (expr.empty())
                return nullptr;
            return std::unique_ptr<TTreeFormula>(new TTreeFormula(name, expr.c_str(), &chain));
        }
        catch (...)
        {
            return nullptr;
        }
    }

    bool is_mc_origin(const DatasetIO::Sample &sample)
    {
        return sample.origin == DatasetIO::Sample::Origin::kOverlay ||
               sample.origin == DatasetIO::Sample::Origin::kDirt ||
               sample.origin == DatasetIO::Sample::Origin::kSignal;
    }

    bool is_external_origin(const DatasetIO::Sample &sample)
    {
        return sample.origin == DatasetIO::Sample::Origin::kExternal;
    }

    bool is_data_origin(const DatasetIO::Sample &sample)
    {
        return sample.origin == DatasetIO::Sample::Origin::kData;
    }

    double sanitise_weight(double w)
    {
        if (!std::isfinite(w) || w <= 0.0)
            return 1.0;
        return w;
    }

    std::string sample_context(const std::string &sample_key,
                               const DatasetIO::Sample &sample)
    {
        if (!sample.sample.empty() && sample.sample != sample_key)
            return sample_key + " (" + sample.sample + ")";
        if (!sample_key.empty())
            return sample_key;
        if (!sample.sample.empty())
            return sample.sample;
        return "<unknown sample>";
    }

    std::map<RunSubrunKey, DatasetIO::RunSubrunNormalisation>
    build_normalisation_lookup(const std::string &sample_key,
                               const DatasetIO::Sample &sample)
    {
        if (sample.run_subrun_normalisations.empty())
        {
            throw std::runtime_error("ana::build_event_list: sample " +
                                     sample_context(sample_key, sample) +
                                     " is missing the embedded run/subrun normalisation surface");
        }

        std::map<RunSubrunKey, DatasetIO::RunSubrunNormalisation> lookup;
        for (const auto &entry : sample.run_subrun_normalisations)
        {
            const RunSubrunKey key{entry.run, entry.subrun};
            const auto inserted = lookup.emplace(key, entry);
            if (!inserted.second)
            {
                throw std::runtime_error("ana::build_event_list: sample " +
                                         sample_context(sample_key, sample) +
                                         " has duplicate run/subrun normalisation entries for (" +
                                         std::to_string(entry.run) + ", " +
                                         std::to_string(entry.subrun) + ")");
            }
        }

        return lookup;
    }

    double resolved_normalisation(const std::string &sample_key,
                                  const DatasetIO::Sample &sample,
                                  const std::map<RunSubrunKey, DatasetIO::RunSubrunNormalisation> &lookup,
                                  int run,
                                  int subrun)
    {
        if (is_data_origin(sample))
            return 1.0;

        const auto it = lookup.find(RunSubrunKey{run, subrun});
        if (it == lookup.end())
        {
            throw std::runtime_error("ana::build_event_list: sample " +
                                     sample_context(sample_key, sample) +
                                     " is missing run/subrun normalisation for (" +
                                     std::to_string(run) + ", " +
                                     std::to_string(subrun) + ")");
        }

        const double value = it->second.normalisation;
        if (!std::isfinite(value) || value < 0.0)
        {
            throw std::runtime_error("ana::build_event_list: sample " +
                                     sample_context(sample_key, sample) +
                                     " has invalid run/subrun normalisation for (" +
                                     std::to_string(run) + ", " +
                                     std::to_string(subrun) + ")");
        }
        return value;
    }

    double compute_central_value_weight(const DatasetIO::Sample &sample,
                                        bool has_weight_spline_times_tune,
                                        float weight_spline_times_tune,
                                        float weight_spline,
                                        float weight_tune,
                                        bool has_ppfx_cv,
                                        float ppfx_cv,
                                        bool has_rootino_fix,
                                        double rootino_fix)
    {
        if (is_data_origin(sample) || is_external_origin(sample) || !is_mc_origin(sample))
            return 1.0;

        const double weight_cv =
            has_weight_spline_times_tune
                ? sanitise_weight(weight_spline_times_tune)
                : sanitise_weight(weight_spline) * sanitise_weight(weight_tune);

        double out = weight_cv;

        if (sample.beam == DatasetIO::Sample::Beam::kNuMI)
            out *= has_ppfx_cv ? sanitise_weight(ppfx_cv) : 1.0;

        out *= has_rootino_fix ? sanitise_weight(rootino_fix) : 1.0;

        if (!std::isfinite(out) || out < 0.0)
            return 0.0;
        return out;
    }

    double compute_nominal_weight(double event_weight_normalisation,
                                  double event_weight_central_value)
    {
        const double out = event_weight_normalisation * event_weight_central_value;
        if (!std::isfinite(out) || out < 0.0)
            return 0.0;
        return out;
    }

    int count_abs_pdg(const std::vector<int> *pdgs, int pdg)
    {
        if (!pdgs)
            return 0;

        int count = 0;
        for (const int value : *pdgs)
        {
            if (std::abs(value) == pdg)
                ++count;
        }
        return count;
    }

    int count_exact_pdg(const std::vector<int> *pdgs, int pdg)
    {
        if (!pdgs)
            return 0;

        int count = 0;
        for (const int value : *pdgs)
        {
            if (value == pdg)
                ++count;
        }
        return count;
    }

    int count_k0(const std::vector<int> *pdgs)
    {
        if (!pdgs)
            return 0;

        int count = 0;
        for (const int value : *pdgs)
        {
            const int abs_pdg = std::abs(value);
            if (abs_pdg == 130 || abs_pdg == 310 || abs_pdg == 311)
                ++count;
        }
        return count;
    }

    template <class T>
    T first_or_default(const std::vector<T> *values, T fallback)
    {
        if (!values || values->empty())
            return fallback;
        return values->front();
    }

    template <class T>
    T at_or_default(const std::vector<T> *values, std::size_t index, T fallback)
    {
        if (!values || index >= values->size())
            return fallback;
        return values->at(index);
    }

    bool any_nonzero(const std::vector<int> *values)
    {
        if (!values)
            return false;
        for (const int value : *values)
        {
            if (value != 0)
                return true;
        }
        return false;
    }

    ana::SignalDefinition::TruthInput make_truth_input(bool is_nu_mu_cc,
                                                       int ccnc,
                                                       bool truth_in_fiducial,
                                                       float truth_vtx_x,
                                                       float truth_vtx_y,
                                                       float truth_vtx_z,
                                                       float mu_p,
                                                       float contained_fraction)
    {
        ana::SignalDefinition::TruthInput truth;
        truth.is_nu_mu_cc = is_nu_mu_cc;
        truth.ccnc = ccnc;
        truth.truth_in_fiducial = truth_in_fiducial;
        truth.truth_vtx_x = truth_vtx_x;
        truth.truth_vtx_y = truth_vtx_y;
        truth.truth_vtx_z = truth_vtx_z;
        truth.mu_p = mu_p;
        truth.contained_fraction = contained_fraction;
        return truth;
    }

    ana::SignalDefinition::LambdaTruthCandidate legacy_lambda_candidate(
        const std::vector<int> *g4_lambda_pdg,
        const std::vector<float> *g4_lambda_p_mag,
        const std::vector<float> *g4_lambda_p_p,
        const std::vector<float> *g4_lambda_pi_p,
        const std::vector<float> *g4_lambda_decay_sep,
        const std::vector<float> *g4_lambda_endx,
        const std::vector<float> *g4_lambda_endy,
        const std::vector<float> *g4_lambda_endz)
    {
        ana::SignalDefinition::LambdaTruthCandidate cand;
        cand.valid = g4_lambda_pdg && !g4_lambda_pdg->empty();
        cand.has_ppi_decay = cand.valid;
        cand.lambda_pdg = first_or_default(g4_lambda_pdg, 0);
        cand.lambda_p = first_or_default(g4_lambda_p_mag, std::numeric_limits<float>::quiet_NaN());
        cand.proton_p = first_or_default(g4_lambda_p_p, std::numeric_limits<float>::quiet_NaN());
        cand.pion_p = first_or_default(g4_lambda_pi_p, std::numeric_limits<float>::quiet_NaN());
        cand.decay_sep = first_or_default(g4_lambda_decay_sep, std::numeric_limits<float>::quiet_NaN());
        cand.decay_x = first_or_default(g4_lambda_endx, std::numeric_limits<float>::quiet_NaN());
        cand.decay_y = first_or_default(g4_lambda_endy, std::numeric_limits<float>::quiet_NaN());
        cand.decay_z = first_or_default(g4_lambda_endz, std::numeric_limits<float>::quiet_NaN());
        return cand;
    }

    ana::SignalDefinition::LambdaTruthCandidate first_passing_lambda_candidate(
        const ana::SignalDefinition &signal_definition,
        const ana::SignalDefinition::TruthInput &truth,
        const std::vector<int> *g4_all_lambda_pdg,
        const std::vector<int> *g4_all_lambda_has_ppi_decay,
        const std::vector<int> *g4_all_lambda_has_sigma0_ancestor,
        const std::vector<float> *g4_all_lambda_p_mag,
        const std::vector<float> *g4_all_lambda_p_p,
        const std::vector<float> *g4_all_lambda_pi_p,
        const std::vector<float> *g4_all_lambda_decay_sep,
        const std::vector<float> *g4_all_lambda_endx,
        const std::vector<float> *g4_all_lambda_endy,
        const std::vector<float> *g4_all_lambda_endz,
        const std::vector<float> *g4_all_lambda_p_endx,
        const std::vector<float> *g4_all_lambda_p_endy,
        const std::vector<float> *g4_all_lambda_p_endz,
        const std::vector<float> *g4_all_lambda_pi_endx,
        const std::vector<float> *g4_all_lambda_pi_endy,
        const std::vector<float> *g4_all_lambda_pi_endz)
    {
        ana::SignalDefinition::LambdaTruthCandidate best;
        if (!g4_all_lambda_pdg)
            return best;

        for (std::size_t i = 0; i < g4_all_lambda_pdg->size(); ++i)
        {
            ana::SignalDefinition::LambdaTruthCandidate cand;
            cand.valid = true;
            cand.lambda_pdg = at_or_default(g4_all_lambda_pdg, i, 0);
            cand.has_ppi_decay = at_or_default(g4_all_lambda_has_ppi_decay, i, 0) != 0;
            cand.has_sigma0_ancestor =
                at_or_default(g4_all_lambda_has_sigma0_ancestor, i, 0) != 0;
            cand.lambda_p =
                at_or_default(g4_all_lambda_p_mag, i, std::numeric_limits<float>::quiet_NaN());
            cand.proton_p =
                at_or_default(g4_all_lambda_p_p, i, std::numeric_limits<float>::quiet_NaN());
            cand.pion_p =
                at_or_default(g4_all_lambda_pi_p, i, std::numeric_limits<float>::quiet_NaN());
            cand.decay_sep = at_or_default(g4_all_lambda_decay_sep, i,
                                           std::numeric_limits<float>::quiet_NaN());
            cand.decay_x =
                at_or_default(g4_all_lambda_endx, i, std::numeric_limits<float>::quiet_NaN());
            cand.decay_y =
                at_or_default(g4_all_lambda_endy, i, std::numeric_limits<float>::quiet_NaN());
            cand.decay_z =
                at_or_default(g4_all_lambda_endz, i, std::numeric_limits<float>::quiet_NaN());
            cand.proton_end_x = at_or_default(g4_all_lambda_p_endx, i,
                                              std::numeric_limits<float>::quiet_NaN());
            cand.proton_end_y = at_or_default(g4_all_lambda_p_endy, i,
                                              std::numeric_limits<float>::quiet_NaN());
            cand.proton_end_z = at_or_default(g4_all_lambda_p_endz, i,
                                              std::numeric_limits<float>::quiet_NaN());
            cand.pion_end_x = at_or_default(g4_all_lambda_pi_endx, i,
                                            std::numeric_limits<float>::quiet_NaN());
            cand.pion_end_y = at_or_default(g4_all_lambda_pi_endy, i,
                                            std::numeric_limits<float>::quiet_NaN());
            cand.pion_end_z = at_or_default(g4_all_lambda_pi_endz, i,
                                            std::numeric_limits<float>::quiet_NaN());

            if (signal_definition.passes(truth, cand))
            {
                return cand;
            }
        }

        return best;
    }

    std::unique_ptr<TTree> copy_selected_tree(const std::string &sample_key,
                                              const DatasetIO::Sample &sample,
                                              const std::string &event_tree_name,
                                              const std::string &selection_expr,
                                              const cuts::Config &cuts_config,
                                              const ana::SignalDefinition &signal_definition)
    {
        TChain chain(event_tree_name.c_str());
        for (const auto &path : sample.root_files)
            chain.Add(path.c_str());

        if (chain.GetNtrees() == 0)
            throw std::runtime_error("ana::build_event_list: no input trees found for event tree " + event_tree_name);

        const std::string effective_selection_expr =
            build_selection_expression(sample, chain, selection_expr);
        std::unique_ptr<TTreeFormula> selection(new TTreeFormula("eventlist_selection",
                                                                 effective_selection_expr.c_str(),
                                                                 &chain));
        if (!selection || !selection->GetTree())
            throw std::runtime_error("ana::build_event_list: failed to compile selection expression: " + effective_selection_expr);

        std::unique_ptr<TTree> selected(chain.CloneTree(0));
        if (!selected)
            throw std::runtime_error("ana::build_event_list: failed to clone event tree structure");
        selected->SetDirectory(nullptr);
        selected->SetName("selected");
        selected->SetTitle("Selected event list");

        TTree *first_tree = chain.GetTree();
        if (!first_tree)
        {
            chain.LoadTree(0);
            first_tree = chain.GetTree();
        }
        const std::vector<std::string> columns = branch_names(first_tree);
        std::unique_ptr<TTreeFormula> pass_trigger_formula =
            make_optional_formula("eventlist_pass_trigger",
                                  cuts::Preset::kTrigger,
                                  sample,
                                  columns,
                                  cuts_config,
                                  chain);
        std::unique_ptr<TTreeFormula> pass_slice_formula =
            make_optional_formula("eventlist_pass_slice",
                                  cuts::Preset::kSlice,
                                  sample,
                                  columns,
                                  cuts_config,
                                  chain);
        std::unique_ptr<TTreeFormula> pass_fiducial_formula =
            make_optional_formula("eventlist_pass_fiducial",
                                  cuts::Preset::kFiducial,
                                  sample,
                                  columns,
                                  cuts_config,
                                  chain);
        std::unique_ptr<TTreeFormula> pass_muon_formula =
            make_optional_formula("eventlist_pass_muon",
                                  cuts::Preset::kMuon,
                                  sample,
                                  columns,
                                  cuts_config,
                                  chain);
        const auto normalisation_lookup = build_normalisation_lookup(sample_key, sample);

        float weight_spline = 1.0f;
        float weight_tune = 1.0f;
        float weight_spline_times_tune = 1.0f;
        float ppfx_cv = 1.0f;
        double rootino_fix = 1.0;
        Int_t run = 0;
        Int_t subrun = 0;
        int nu_pdg = 0;
        int int_ccnc = -1;
        bool is_nu_mu_cc = false;
        bool truth_in_fiducial = false;
        float truth_vtx_x = std::numeric_limits<float>::quiet_NaN();
        float truth_vtx_y = std::numeric_limits<float>::quiet_NaN();
        float truth_vtx_z = std::numeric_limits<float>::quiet_NaN();
        float mu_p = std::numeric_limits<float>::quiet_NaN();
        float contained_fraction = std::numeric_limits<float>::quiet_NaN();

        std::vector<int> *prim_pdg = nullptr;
        std::vector<int> *g4_lambda_pdg = nullptr;
        std::vector<float> *g4_lambda_p_mag = nullptr;
        std::vector<float> *g4_lambda_p_p = nullptr;
        std::vector<float> *g4_lambda_pi_p = nullptr;
        std::vector<float> *g4_lambda_decay_sep = nullptr;
        std::vector<float> *g4_lambda_endx = nullptr;
        std::vector<float> *g4_lambda_endy = nullptr;
        std::vector<float> *g4_lambda_endz = nullptr;
        std::vector<int> *g4_all_lambda_pdg = nullptr;
        std::vector<int> *g4_all_lambda_has_ppi_decay = nullptr;
        std::vector<int> *g4_all_lambda_has_sigma0_ancestor = nullptr;
        std::vector<float> *g4_all_lambda_p_mag = nullptr;
        std::vector<float> *g4_all_lambda_p_p = nullptr;
        std::vector<float> *g4_all_lambda_pi_p = nullptr;
        std::vector<float> *g4_all_lambda_decay_sep = nullptr;
        std::vector<float> *g4_all_lambda_endx = nullptr;
        std::vector<float> *g4_all_lambda_endy = nullptr;
        std::vector<float> *g4_all_lambda_endz = nullptr;
        std::vector<float> *g4_all_lambda_p_endx = nullptr;
        std::vector<float> *g4_all_lambda_p_endy = nullptr;
        std::vector<float> *g4_all_lambda_p_endz = nullptr;
        std::vector<float> *g4_all_lambda_pi_endx = nullptr;
        std::vector<float> *g4_all_lambda_pi_endy = nullptr;
        std::vector<float> *g4_all_lambda_pi_endz = nullptr;

        const bool has_weight_spline = chain.GetBranch("weightSpline") != nullptr;
        const bool has_weight_tune = chain.GetBranch("weightTune") != nullptr;
        const bool has_weight_spline_times_tune = chain.GetBranch("weightSplineTimesTune") != nullptr;
        const bool has_ppfx_cv = chain.GetBranch("ppfx_cv") != nullptr;
        const bool has_rootino_fix = chain.GetBranch("RootinoFix") != nullptr;
        const bool has_run = chain.GetBranch("run") != nullptr;
        const bool has_subrun = chain.GetBranch("subRun") != nullptr;
        const bool has_nu_pdg = chain.GetBranch("nu_pdg") != nullptr;
        const bool has_int_ccnc = chain.GetBranch("int_ccnc") != nullptr;
        const bool has_is_nu_mu_cc = chain.GetBranch("is_nu_mu_cc") != nullptr;
        const bool has_truth_in_fiducial = chain.GetBranch("nu_vtx_in_fv") != nullptr;
        const bool has_truth_vtx_x = chain.GetBranch("nu_vtx_x") != nullptr;
        const bool has_truth_vtx_y = chain.GetBranch("nu_vtx_y") != nullptr;
        const bool has_truth_vtx_z = chain.GetBranch("nu_vtx_z") != nullptr;
        const bool has_mu_p = chain.GetBranch("mu_p") != nullptr;
        const bool has_contained_fraction = chain.GetBranch("contained_fraction") != nullptr;
        const bool has_prim_pdg = chain.GetBranch("prim_pdg") != nullptr;
        const bool has_g4_lambda_pdg = chain.GetBranch("g4_lambda_pdg") != nullptr;
        const bool has_g4_lambda_p_mag = chain.GetBranch("g4_lambda_p_mag") != nullptr;
        const bool has_g4_lambda_p_p = chain.GetBranch("g4_lambda_p_p") != nullptr;
        const bool has_g4_lambda_pi_p = chain.GetBranch("g4_lambda_pi_p") != nullptr;
        const bool has_g4_lambda_decay_sep = chain.GetBranch("g4_lambda_decay_sep") != nullptr;
        const bool has_g4_lambda_endx = chain.GetBranch("g4_lambda_endx") != nullptr;
        const bool has_g4_lambda_endy = chain.GetBranch("g4_lambda_endy") != nullptr;
        const bool has_g4_lambda_endz = chain.GetBranch("g4_lambda_endz") != nullptr;
        const bool has_g4_all_lambda_pdg = chain.GetBranch("g4_all_lambda_pdg") != nullptr;
        const bool has_g4_all_lambda_has_ppi_decay =
            chain.GetBranch("g4_all_lambda_has_ppi_decay") != nullptr;
        const bool has_g4_all_lambda_has_sigma0_ancestor =
            chain.GetBranch("g4_all_lambda_has_sigma0_ancestor") != nullptr;
        const bool has_g4_all_lambda_p_mag = chain.GetBranch("g4_all_lambda_p_mag") != nullptr;
        const bool has_g4_all_lambda_p_p = chain.GetBranch("g4_all_lambda_p_p") != nullptr;
        const bool has_g4_all_lambda_pi_p = chain.GetBranch("g4_all_lambda_pi_p") != nullptr;
        const bool has_g4_all_lambda_decay_sep =
            chain.GetBranch("g4_all_lambda_decay_sep") != nullptr;
        const bool has_g4_all_lambda_endx = chain.GetBranch("g4_all_lambda_endx") != nullptr;
        const bool has_g4_all_lambda_endy = chain.GetBranch("g4_all_lambda_endy") != nullptr;
        const bool has_g4_all_lambda_endz = chain.GetBranch("g4_all_lambda_endz") != nullptr;
        const bool has_g4_all_lambda_p_endx =
            chain.GetBranch("g4_all_lambda_p_endx") != nullptr;
        const bool has_g4_all_lambda_p_endy =
            chain.GetBranch("g4_all_lambda_p_endy") != nullptr;
        const bool has_g4_all_lambda_p_endz =
            chain.GetBranch("g4_all_lambda_p_endz") != nullptr;
        const bool has_g4_all_lambda_pi_endx =
            chain.GetBranch("g4_all_lambda_pi_endx") != nullptr;
        const bool has_g4_all_lambda_pi_endy =
            chain.GetBranch("g4_all_lambda_pi_endy") != nullptr;
        const bool has_g4_all_lambda_pi_endz =
            chain.GetBranch("g4_all_lambda_pi_endz") != nullptr;

        if (!has_run || !has_subrun)
        {
            throw std::runtime_error("ana::build_event_list: event tree " + event_tree_name +
                                     " for sample " + sample_context(sample_key, sample) +
                                     " must expose run and subRun branches");
        }

        if (has_weight_spline)
            chain.SetBranchAddress("weightSpline", &weight_spline);
        if (has_weight_tune)
            chain.SetBranchAddress("weightTune", &weight_tune);
        if (has_weight_spline_times_tune)
            chain.SetBranchAddress("weightSplineTimesTune", &weight_spline_times_tune);
        if (has_ppfx_cv)
            chain.SetBranchAddress("ppfx_cv", &ppfx_cv);
        if (has_rootino_fix)
            chain.SetBranchAddress("RootinoFix", &rootino_fix);
        chain.SetBranchAddress("run", &run);
        chain.SetBranchAddress("subRun", &subrun);
        if (has_nu_pdg)
            chain.SetBranchAddress("nu_pdg", &nu_pdg);
        if (has_int_ccnc)
            chain.SetBranchAddress("int_ccnc", &int_ccnc);
        if (has_is_nu_mu_cc)
            chain.SetBranchAddress("is_nu_mu_cc", &is_nu_mu_cc);
        if (has_truth_in_fiducial)
            chain.SetBranchAddress("nu_vtx_in_fv", &truth_in_fiducial);
        if (has_truth_vtx_x)
            chain.SetBranchAddress("nu_vtx_x", &truth_vtx_x);
        if (has_truth_vtx_y)
            chain.SetBranchAddress("nu_vtx_y", &truth_vtx_y);
        if (has_truth_vtx_z)
            chain.SetBranchAddress("nu_vtx_z", &truth_vtx_z);
        if (has_mu_p)
            chain.SetBranchAddress("mu_p", &mu_p);
        if (has_contained_fraction)
            chain.SetBranchAddress("contained_fraction", &contained_fraction);
        if (has_prim_pdg)
            chain.SetBranchAddress("prim_pdg", &prim_pdg);
        if (has_g4_lambda_pdg)
            chain.SetBranchAddress("g4_lambda_pdg", &g4_lambda_pdg);
        if (has_g4_lambda_p_mag)
            chain.SetBranchAddress("g4_lambda_p_mag", &g4_lambda_p_mag);
        if (has_g4_lambda_p_p)
            chain.SetBranchAddress("g4_lambda_p_p", &g4_lambda_p_p);
        if (has_g4_lambda_pi_p)
            chain.SetBranchAddress("g4_lambda_pi_p", &g4_lambda_pi_p);
        if (has_g4_lambda_decay_sep)
            chain.SetBranchAddress("g4_lambda_decay_sep", &g4_lambda_decay_sep);
        if (has_g4_lambda_endx)
            chain.SetBranchAddress("g4_lambda_endx", &g4_lambda_endx);
        if (has_g4_lambda_endy)
            chain.SetBranchAddress("g4_lambda_endy", &g4_lambda_endy);
        if (has_g4_lambda_endz)
            chain.SetBranchAddress("g4_lambda_endz", &g4_lambda_endz);
        if (has_g4_all_lambda_pdg)
            chain.SetBranchAddress("g4_all_lambda_pdg", &g4_all_lambda_pdg);
        if (has_g4_all_lambda_has_ppi_decay)
            chain.SetBranchAddress("g4_all_lambda_has_ppi_decay",
                                   &g4_all_lambda_has_ppi_decay);
        if (has_g4_all_lambda_has_sigma0_ancestor)
            chain.SetBranchAddress("g4_all_lambda_has_sigma0_ancestor",
                                   &g4_all_lambda_has_sigma0_ancestor);
        if (has_g4_all_lambda_p_mag)
            chain.SetBranchAddress("g4_all_lambda_p_mag", &g4_all_lambda_p_mag);
        if (has_g4_all_lambda_p_p)
            chain.SetBranchAddress("g4_all_lambda_p_p", &g4_all_lambda_p_p);
        if (has_g4_all_lambda_pi_p)
            chain.SetBranchAddress("g4_all_lambda_pi_p", &g4_all_lambda_pi_p);
        if (has_g4_all_lambda_decay_sep)
            chain.SetBranchAddress("g4_all_lambda_decay_sep",
                                   &g4_all_lambda_decay_sep);
        if (has_g4_all_lambda_endx)
            chain.SetBranchAddress("g4_all_lambda_endx", &g4_all_lambda_endx);
        if (has_g4_all_lambda_endy)
            chain.SetBranchAddress("g4_all_lambda_endy", &g4_all_lambda_endy);
        if (has_g4_all_lambda_endz)
            chain.SetBranchAddress("g4_all_lambda_endz", &g4_all_lambda_endz);
        if (has_g4_all_lambda_p_endx)
            chain.SetBranchAddress("g4_all_lambda_p_endx", &g4_all_lambda_p_endx);
        if (has_g4_all_lambda_p_endy)
            chain.SetBranchAddress("g4_all_lambda_p_endy", &g4_all_lambda_p_endy);
        if (has_g4_all_lambda_p_endz)
            chain.SetBranchAddress("g4_all_lambda_p_endz", &g4_all_lambda_p_endz);
        if (has_g4_all_lambda_pi_endx)
            chain.SetBranchAddress("g4_all_lambda_pi_endx", &g4_all_lambda_pi_endx);
        if (has_g4_all_lambda_pi_endy)
            chain.SetBranchAddress("g4_all_lambda_pi_endy", &g4_all_lambda_pi_endy);
        if (has_g4_all_lambda_pi_endz)
            chain.SetBranchAddress("g4_all_lambda_pi_endz", &g4_all_lambda_pi_endz);

        double event_weight_normalisation = 1.0;
        double event_weight_central_value = 1.0;
        double event_weight = 1.0;
        double event_weight_squared = 1.0;
        bool pass_trigger = false;
        bool pass_slice = false;
        bool pass_fiducial = false;
        bool pass_muon = false;
        int event_category_code = event_category::to_int(event_category::EventCategory::kUnknown);
        bool passes_signal_definition = false;
        selected->Branch(EventListIO::event_weight_normalisation_branch_name(),
                         &event_weight_normalisation,
                         (std::string(EventListIO::event_weight_normalisation_branch_name()) + "/D").c_str());
        selected->Branch(EventListIO::event_weight_central_value_branch_name(),
                         &event_weight_central_value,
                         (std::string(EventListIO::event_weight_central_value_branch_name()) + "/D").c_str());
        selected->Branch(EventListIO::event_weight_branch_name(),
                         &event_weight,
                         (std::string(EventListIO::event_weight_branch_name()) + "/D").c_str());
        selected->Branch(EventListIO::event_weight_squared_branch_name(),
                         &event_weight_squared,
                         (std::string(EventListIO::event_weight_squared_branch_name()) + "/D").c_str());
        selected->Branch(EventListIO::event_category_branch_name(),
                         &event_category_code,
                         (std::string(EventListIO::event_category_branch_name()) + "/I").c_str());
        selected->Branch(EventListIO::passes_signal_definition_branch_name(),
                         &passes_signal_definition,
                         (std::string(EventListIO::passes_signal_definition_branch_name()) + "/O").c_str());
        selected->Branch(cuts::trigger_branch(), &pass_trigger,
                         (std::string(cuts::trigger_branch()) + "/O").c_str());
        selected->Branch(cuts::slice_branch(), &pass_slice,
                         (std::string(cuts::slice_branch()) + "/O").c_str());
        selected->Branch(cuts::fiducial_branch(), &pass_fiducial,
                         (std::string(cuts::fiducial_branch()) + "/O").c_str());
        selected->Branch(cuts::muon_branch(), &pass_muon,
                         (std::string(cuts::muon_branch()) + "/O").c_str());

        int current_tree_number = -1;
        const Long64_t n_entries = chain.GetEntries();
        for (Long64_t i = 0; i < n_entries; ++i)
        {
            const Long64_t local_entry = chain.LoadTree(i);
            if (local_entry < 0)
                break;

            if (chain.GetTreeNumber() != current_tree_number)
            {
                current_tree_number = chain.GetTreeNumber();
                selection->UpdateFormulaLeaves();
                if (pass_trigger_formula) pass_trigger_formula->UpdateFormulaLeaves();
                if (pass_slice_formula) pass_slice_formula->UpdateFormulaLeaves();
                if (pass_fiducial_formula) pass_fiducial_formula->UpdateFormulaLeaves();
                if (pass_muon_formula) pass_muon_formula->UpdateFormulaLeaves();
            }

            chain.GetEntry(i);
            pass_trigger = pass_trigger_formula ? (pass_trigger_formula->EvalInstance() != 0.0) : false;
            pass_slice = pass_slice_formula ? (pass_slice_formula->EvalInstance() != 0.0) : false;
            pass_fiducial = pass_fiducial_formula ? (pass_fiducial_formula->EvalInstance() != 0.0) : false;
            pass_muon = pass_muon_formula ? (pass_muon_formula->EvalInstance() != 0.0) : false;
            if (selection->EvalInstance() != 0.0)
            {
                const int n_protons = count_abs_pdg(prim_pdg, 2212);
                const int n_pi_minus = count_exact_pdg(prim_pdg, -211);
                const int n_pi_plus = count_exact_pdg(prim_pdg, 211);
                const int n_pi0 = count_abs_pdg(prim_pdg, 111);
                const int n_gamma = count_abs_pdg(prim_pdg, 22);
                const int n_k0 = count_k0(prim_pdg);
                const int n_sigma0 = count_abs_pdg(prim_pdg, 3212);
                const ana::SignalDefinition::TruthInput truth =
                    make_truth_input(is_nu_mu_cc,
                                     int_ccnc,
                                     truth_in_fiducial,
                                     truth_vtx_x,
                                     truth_vtx_y,
                                     truth_vtx_z,
                                     mu_p,
                                     contained_fraction);
                const ana::SignalDefinition::LambdaTruthCandidate lambda_candidate =
                    has_g4_all_lambda_pdg
                        ? first_passing_lambda_candidate(
                              signal_definition,
                              truth,
                              g4_all_lambda_pdg,
                              g4_all_lambda_has_ppi_decay,
                              g4_all_lambda_has_sigma0_ancestor,
                              g4_all_lambda_p_mag,
                              g4_all_lambda_p_p,
                              g4_all_lambda_pi_p,
                              g4_all_lambda_decay_sep,
                              g4_all_lambda_endx,
                              g4_all_lambda_endy,
                              g4_all_lambda_endz,
                              g4_all_lambda_p_endx,
                              g4_all_lambda_p_endy,
                              g4_all_lambda_p_endz,
                              g4_all_lambda_pi_endx,
                              g4_all_lambda_pi_endy,
                              g4_all_lambda_pi_endz)
                        : legacy_lambda_candidate(g4_lambda_pdg,
                                                  g4_lambda_p_mag,
                                                  g4_lambda_p_p,
                                                  g4_lambda_pi_p,
                                                  g4_lambda_decay_sep,
                                                  g4_lambda_endx,
                                                  g4_lambda_endy,
                                                  g4_lambda_endz);
                const bool has_sigma0_lambda_ancestor =
                    has_g4_all_lambda_has_sigma0_ancestor &&
                    any_nonzero(g4_all_lambda_has_sigma0_ancestor);

                if (is_data_origin(sample))
                {
                    event_category_code = event_category::to_int(event_category::EventCategory::kDataInclusive);
                    passes_signal_definition = false;
                }
                else if (is_external_origin(sample))
                {
                    event_category_code = event_category::to_int(event_category::EventCategory::kExternal);
                    passes_signal_definition = false;
                }
                else if (is_mc_origin(sample))
                {
                    passes_signal_definition = signal_definition.passes(truth, lambda_candidate);
                    event_category_code = event_category::to_int(
                        event_category::classify(truth_in_fiducial,
                                                 nu_pdg,
                                                 int_ccnc,
                                                 n_protons,
                                                 n_pi_minus,
                                                 n_pi_plus,
                                                 n_pi0,
                                                 n_gamma,
                                                 n_k0,
                                                 n_sigma0,
                                                 has_sigma0_lambda_ancestor,
                                                 passes_signal_definition));
                }
                else
                {
                    event_category_code = event_category::to_int(event_category::EventCategory::kUnknown);
                    passes_signal_definition = false;
                }

                event_weight_normalisation = resolved_normalisation(sample_key,
                                                                    sample,
                                                                    normalisation_lookup,
                                                                    static_cast<int>(run),
                                                                    static_cast<int>(subrun));
                event_weight_central_value = compute_central_value_weight(sample,
                                                                          has_weight_spline_times_tune,
                                                                          weight_spline_times_tune,
                                                                          weight_spline,
                                                                          weight_tune,
                                                                          has_ppfx_cv,
                                                                          ppfx_cv,
                                                                          has_rootino_fix,
                                                                          rootino_fix);
                event_weight = compute_nominal_weight(event_weight_normalisation,
                                                      event_weight_central_value);
                event_weight_squared = event_weight * event_weight;
                selected->Fill();
            }
        }

        return selected;
    }

    std::unique_ptr<TTree> copy_subrun_tree(const DatasetIO::Sample &sample,
                                            const std::string &subrun_tree_name)
    {
        TChain chain(subrun_tree_name.c_str());
        for (const auto &path : sample.root_files)
            chain.Add(path.c_str());

        if (chain.GetNtrees() == 0)
            throw std::runtime_error("ana::build_event_list: no input trees found for subrun tree " + subrun_tree_name);

        std::unique_ptr<TTree> copied(chain.CloneTree(-1, "fast"));
        if (!copied)
            throw std::runtime_error("ana::build_event_list: failed to copy subrun tree");
        copied->SetDirectory(nullptr);
        copied->SetName(subrun_tree_name.c_str());
        return copied;
    }
}

namespace ana
{
    void build_event_list(const DatasetIO &dataset,
                          EventListIO &event_list,
                          const BuildConfig &config)
    {
        if (config.event_tree_name.empty())
            throw std::runtime_error("ana::build_event_list: event_tree_name must not be empty");
        if (config.subrun_tree_name.empty())
            throw std::runtime_error("ana::build_event_list: subrun_tree_name must not be empty");
        if (config.selection_expr.empty() && config.selection_name.empty())
            throw std::runtime_error("ana::build_event_list: selection_expr must not be empty");

        const cuts::Config cuts_config = cuts_config_for(config);
        const SignalDefinition &signal_definition = SignalDefinition::canonical();

        EventListIO::Metadata metadata;
        metadata.dataset_path = dataset.path();
        metadata.dataset_context = dataset.context();
        metadata.event_tree_name = config.event_tree_name;
        metadata.subrun_tree_name = config.subrun_tree_name;
        metadata.selection_name = config.selection_name;
        metadata.selection_expr = config.selection_expr;
        metadata.signal_definition = signal_definition.describe();
        metadata.slice_required_count = cuts_config.slice_required_count;
        metadata.slice_min_topology_score = cuts_config.slice_min_topology_score;
        metadata.numi_run_boundary = cuts_config.numi_run_boundary;
        event_list.write_metadata(metadata);

        for (const auto &key : dataset.sample_keys())
        {
            const DatasetIO::Sample sample = dataset.sample(key);

            TChain preview_chain(config.event_tree_name.c_str());
            for (const auto &path : sample.root_files)
                preview_chain.Add(path.c_str());
            if (preview_chain.GetNtrees() == 0)
                throw std::runtime_error("ana::build_event_list: no input trees found for event tree " + config.event_tree_name);

            std::string effective_selection_expr = config.selection_expr;
            if (!config.selection_name.empty() && config.selection_name != "raw")
            {
                TTree *preview_tree = preview_chain.GetTree();
                if (!preview_tree)
                {
                    preview_chain.LoadTree(0);
                    preview_tree = preview_chain.GetTree();
                }
                effective_selection_expr = cuts::expression(cuts::preset_from_string(config.selection_name),
                                                            sample,
                                                            branch_names(preview_tree),
                                                            cuts_config);
            }

            std::unique_ptr<TTree> selected =
                copy_selected_tree(key,
                                   sample,
                                   config.event_tree_name,
                                   effective_selection_expr,
                                   cuts_config,
                                   signal_definition);
            std::unique_ptr<TTree> subruns =
                copy_subrun_tree(sample, config.subrun_tree_name);

            event_list.write_sample(key,
                                    sample,
                                    selected.get(),
                                    subruns.get(),
                                    config.subrun_tree_name);
        }

        event_list.flush();
    }
}
