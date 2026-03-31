#include "EventListBuild.hh"

#include "Cuts.hh"
#include "Channels.hh"

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
    constexpr const char *kEventWeightNormalisationBranch = "__w_norm__";
    constexpr const char *kEventWeightCentralValueBranch = "__w_cv__";
    constexpr const char *kEventWeightBranch = "__w__";
    constexpr const char *kEventWeightSquaredBranch = "__w2__";
    constexpr const char *kAnalysisChannelBranch = "__analysis_channel__";
    constexpr const char *kSignalBranch = "__is_signal__";
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

    std::unique_ptr<TTree> copy_selected_tree(const std::string &sample_key,
                                              const DatasetIO::Sample &sample,
                                              const std::string &event_tree_name,
                                              const std::string &selection_expr,
                                              const cuts::Config &cuts_config)
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
        float mu_p = std::numeric_limits<float>::quiet_NaN();

        std::vector<int> *prim_pdg = nullptr;
        std::vector<int> *g4_lambda_pdg = nullptr;
        std::vector<float> *g4_lambda_p_p = nullptr;
        std::vector<float> *g4_lambda_pi_p = nullptr;
        std::vector<float> *g4_lambda_decay_sep = nullptr;

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
        const bool has_mu_p = chain.GetBranch("mu_p") != nullptr;
        const bool has_prim_pdg = chain.GetBranch("prim_pdg") != nullptr;
        const bool has_g4_lambda_pdg = chain.GetBranch("g4_lambda_pdg") != nullptr;
        const bool has_g4_lambda_p_p = chain.GetBranch("g4_lambda_p_p") != nullptr;
        const bool has_g4_lambda_pi_p = chain.GetBranch("g4_lambda_pi_p") != nullptr;
        const bool has_g4_lambda_decay_sep = chain.GetBranch("g4_lambda_decay_sep") != nullptr;

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
        if (has_mu_p)
            chain.SetBranchAddress("mu_p", &mu_p);
        if (has_prim_pdg)
            chain.SetBranchAddress("prim_pdg", &prim_pdg);
        if (has_g4_lambda_pdg)
            chain.SetBranchAddress("g4_lambda_pdg", &g4_lambda_pdg);
        if (has_g4_lambda_p_p)
            chain.SetBranchAddress("g4_lambda_p_p", &g4_lambda_p_p);
        if (has_g4_lambda_pi_p)
            chain.SetBranchAddress("g4_lambda_pi_p", &g4_lambda_pi_p);
        if (has_g4_lambda_decay_sep)
            chain.SetBranchAddress("g4_lambda_decay_sep", &g4_lambda_decay_sep);

        double event_weight_normalisation = 1.0;
        double event_weight_central_value = 1.0;
        double event_weight = 1.0;
        double event_weight_squared = 1.0;
        bool pass_trigger = false;
        bool pass_slice = false;
        bool pass_fiducial = false;
        bool pass_muon = false;
        int analysis_channel = channels::to_int(channels::Channel::kUnknown);
        bool is_signal = false;
        selected->Branch(kEventWeightNormalisationBranch,
                         &event_weight_normalisation,
                         (std::string(kEventWeightNormalisationBranch) + "/D").c_str());
        selected->Branch(kEventWeightCentralValueBranch,
                         &event_weight_central_value,
                         (std::string(kEventWeightCentralValueBranch) + "/D").c_str());
        selected->Branch(kEventWeightBranch, &event_weight, (std::string(kEventWeightBranch) + "/D").c_str());
        selected->Branch(kEventWeightSquaredBranch,
                         &event_weight_squared,
                         (std::string(kEventWeightSquaredBranch) + "/D").c_str());
        selected->Branch(kAnalysisChannelBranch,
                         &analysis_channel,
                         (std::string(kAnalysisChannelBranch) + "/I").c_str());
        selected->Branch(kSignalBranch,
                         &is_signal,
                         (std::string(kSignalBranch) + "/O").c_str());
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
                const int lambda_pdg = first_or_default(g4_lambda_pdg, 0);
                const float proton_p = first_or_default(g4_lambda_p_p, std::numeric_limits<float>::quiet_NaN());
                const float pion_p = first_or_default(g4_lambda_pi_p, std::numeric_limits<float>::quiet_NaN());
                const float lambda_decay_sep =
                    first_or_default(g4_lambda_decay_sep, std::numeric_limits<float>::quiet_NaN());

                if (is_data_origin(sample))
                {
                    analysis_channel = channels::to_int(channels::Channel::kDataInclusive);
                    is_signal = false;
                }
                else if (is_external_origin(sample))
                {
                    analysis_channel = channels::to_int(channels::Channel::kExternal);
                    is_signal = false;
                }
                else if (is_mc_origin(sample))
                {
                    is_signal = channels::is_signal(is_nu_mu_cc,
                                                    int_ccnc,
                                                    truth_in_fiducial,
                                                    lambda_pdg,
                                                    mu_p,
                                                    proton_p,
                                                    pion_p,
                                                    lambda_decay_sep);
                    analysis_channel = channels::to_int(
                        channels::classify(truth_in_fiducial,
                                           nu_pdg,
                                           int_ccnc,
                                           n_protons,
                                           n_pi_minus,
                                           n_pi_plus,
                                           n_pi0,
                                           n_gamma,
                                           n_k0,
                                           n_sigma0,
                                           is_nu_mu_cc,
                                           lambda_pdg,
                                           mu_p,
                                           proton_p,
                                           pion_p,
                                           lambda_decay_sep));
                }
                else
                {
                    analysis_channel = channels::to_int(channels::Channel::kUnknown);
                    is_signal = false;
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

        EventListIO::Metadata metadata;
        metadata.dataset_path = dataset.path();
        metadata.dataset_context = dataset.context();
        metadata.event_tree_name = config.event_tree_name;
        metadata.subrun_tree_name = config.subrun_tree_name;
        metadata.selection_name = config.selection_name;
        metadata.selection_expr = config.selection_expr;
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
                                   cuts_config);
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
