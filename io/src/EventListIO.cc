#include "EventListIO.hh"
#include "AnalysisChannels.hh"
#include "detail/RootUtils.hh"

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <TChain.h>
#include <TDirectory.h>
#include <TFile.h>
#include <TKey.h>
#include <TObject.h>
#include <TTree.h>
#include <TTreeFormula.h>

namespace
{
    constexpr const char *kEventWeightBranch = "__w__";
    constexpr const char *kEventWeightSquaredBranch = "__w2__";
    constexpr const char *kAnalysisChannelBranch = "__analysis_channel__";
    constexpr const char *kSignalBranch = "__is_signal__";

    const char *orthogonal_selection_for_sample(const DatasetIO::Sample &sample)
    {
        using Origin = DatasetIO::Sample::Origin;

        if (sample.origin == Origin::kOverlay)
            return "count_strange == 0";
        if (sample.origin == Origin::kEnriched)
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
                "EventListIO: sample origin requires count_strange for orthogonal filtering");
        }

        if (selection_expr.empty())
            return orthogonal_expr;

        return "(" + selection_expr + ") && (" + orthogonal_expr + ")";
    }

    std::vector<std::string> branch_names(TTree *tree)
    {
        std::vector<std::string> names;
        if (!tree) return names;

        TObjArray *branches = tree->GetListOfBranches();
        if (!branches) return names;

        const int n = branches->GetEntries();
        names.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            TObject *obj = branches->At(i);
            if (obj)
                names.emplace_back(obj->GetName());
        }
        return names;
    }

    std::unique_ptr<TTreeFormula> make_optional_formula(const char *name,
                                                        EventListSelection::Preset preset,
                                                        const DatasetIO::Sample &sample,
                                                        const std::vector<std::string> &columns,
                                                        const EventListSelection::Config &config,
                                                        TChain &chain)
    {
        try
        {
            const std::string expr = EventListSelection::expression(preset, sample, columns, config);
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
               sample.origin == DatasetIO::Sample::Origin::kEnriched;
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

    double compute_nominal_weight(const DatasetIO::Sample &sample,
                                  bool has_weight_spline_times_tune,
                                  float weight_spline_times_tune,
                                  float weight_spline,
                                  float weight_tune,
                                  bool has_ppfx_cv,
                                  float ppfx_cv,
                                  bool has_rootino_fix,
                                  double rootino_fix)
    {
        if (is_data_origin(sample))
            return 1.0;

        const double normalisation =
            (sample.normalisation > 0.0 && std::isfinite(sample.normalisation))
                ? sample.normalisation
                : 1.0;

        if (is_external_origin(sample))
            return normalisation;

        if (!is_mc_origin(sample))
            return normalisation;

        const double weight_cv =
            has_weight_spline_times_tune
                ? sanitise_weight(weight_spline_times_tune)
                : sanitise_weight(weight_spline) * sanitise_weight(weight_tune);

        double out = normalisation * weight_cv;

        if (sample.beam == DatasetIO::Sample::Beam::kNuMI)
            out *= has_ppfx_cv ? sanitise_weight(ppfx_cv) : 1.0;

        out *= has_rootino_fix ? sanitise_weight(rootino_fix) : 1.0;

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

    void write_sample_metadata(TDirectory *meta_dir, const DatasetIO::Sample &sample)
    {
        utils::write_named(meta_dir, "origin", DatasetIO::Sample::origin_name(sample.origin));
        utils::write_named(meta_dir, "variation", DatasetIO::Sample::variation_name(sample.variation));
        utils::write_named(meta_dir, "beam", DatasetIO::Sample::beam_name(sample.beam));
        utils::write_named(meta_dir, "polarity", DatasetIO::Sample::polarity_name(sample.polarity));

        utils::write_param<double>(meta_dir, "subrun_pot_sum", sample.subrun_pot_sum);
        utils::write_param<double>(meta_dir, "db_tortgt_pot_sum", sample.db_tortgt_pot_sum);
        utils::write_param<double>(meta_dir, "normalisation", sample.normalisation);
    }

    TDirectory *sample_dir_for(TFile *file, const std::string &sample_key, bool create)
    {
        TDirectory *samples = utils::must_dir(file, "samples", create);
        return utils::must_subdir(samples, sample_key, create, "samples");
    }

    TDirectory *systematics_dir_for(TFile *file, const std::string &sample_key, bool create)
    {
        TDirectory *sample_dir = sample_dir_for(file, sample_key, create);
        return utils::must_dir(sample_dir, "systematics", create);
    }

    TDirectory *systematics_cache_dir_for(TFile *file,
                                          const std::string &sample_key,
                                          const std::string &cache_key,
                                          bool create)
    {
        TDirectory *systematics_dir = systematics_dir_for(file, sample_key, create);
        return utils::must_subdir(systematics_dir, cache_key, create, "systematics");
    }

    TTree *copy_selected_tree(TDirectory *events_dir,
                              const DatasetIO::Sample &sample,
                              const std::string &event_tree_name,
                              const std::string &selection_expr,
                              const EventListSelection::Config &selection_config)
    {
        TChain chain(event_tree_name.c_str());
        for (const auto &path : sample.root_files)
            chain.Add(path.c_str());

        if (chain.GetNtrees() == 0)
            throw std::runtime_error("EventListIO: no input trees found for event tree " + event_tree_name);

        events_dir->cd();
        const std::string effective_selection_expr =
            build_selection_expression(sample, chain, selection_expr);
        std::unique_ptr<TTreeFormula> selection(new TTreeFormula("eventlist_selection",
                                                                 effective_selection_expr.c_str(),
                                                                 &chain));
        if (!selection || !selection->GetTree())
            throw std::runtime_error("EventListIO: failed to compile selection expression: " + effective_selection_expr);

        std::unique_ptr<TTree> selected(chain.CloneTree(0));
        if (!selected)
            throw std::runtime_error("EventListIO: failed to clone event tree structure");
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
                                  EventListSelection::Preset::kTrigger,
                                  sample,
                                  columns,
                                  selection_config,
                                  chain);
        std::unique_ptr<TTreeFormula> pass_slice_formula =
            make_optional_formula("eventlist_pass_slice",
                                  EventListSelection::Preset::kSlice,
                                  sample,
                                  columns,
                                  selection_config,
                                  chain);
        std::unique_ptr<TTreeFormula> pass_fiducial_formula =
            make_optional_formula("eventlist_pass_fiducial",
                                  EventListSelection::Preset::kFiducial,
                                  sample,
                                  columns,
                                  selection_config,
                                  chain);
        std::unique_ptr<TTreeFormula> pass_muon_formula =
            make_optional_formula("eventlist_pass_muon",
                                  EventListSelection::Preset::kMuon,
                                  sample,
                                  columns,
                                  selection_config,
                                  chain);

        float weight_spline = 1.0f;
        float weight_tune = 1.0f;
        float weight_spline_times_tune = 1.0f;
        float ppfx_cv = 1.0f;
        double rootino_fix = 1.0;
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

        double event_weight = 1.0;
        double event_weight_squared = 1.0;
        bool pass_trigger = false;
        bool pass_slice = false;
        bool pass_fiducial = false;
        bool pass_muon = false;
        int analysis_channel = AnalysisChannels::to_int(AnalysisChannels::Channel::kUnknown);
        bool is_signal = false;
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
        selected->Branch(EventListSelection::trigger_branch(), &pass_trigger,
                         (std::string(EventListSelection::trigger_branch()) + "/O").c_str());
        selected->Branch(EventListSelection::slice_branch(), &pass_slice,
                         (std::string(EventListSelection::slice_branch()) + "/O").c_str());
        selected->Branch(EventListSelection::fiducial_branch(), &pass_fiducial,
                         (std::string(EventListSelection::fiducial_branch()) + "/O").c_str());
        selected->Branch(EventListSelection::muon_branch(), &pass_muon,
                         (std::string(EventListSelection::muon_branch()) + "/O").c_str());

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
                    analysis_channel = AnalysisChannels::to_int(AnalysisChannels::Channel::kDataInclusive);
                    is_signal = false;
                }
                else if (is_external_origin(sample))
                {
                    analysis_channel = AnalysisChannels::to_int(AnalysisChannels::Channel::kExternal);
                    is_signal = false;
                }
                else if (is_mc_origin(sample))
                {
                    is_signal = AnalysisChannels::is_signal(is_nu_mu_cc,
                                                            int_ccnc,
                                                            truth_in_fiducial,
                                                            lambda_pdg,
                                                            mu_p,
                                                            proton_p,
                                                            pion_p,
                                                            lambda_decay_sep);
                    analysis_channel = AnalysisChannels::to_int(
                        AnalysisChannels::classify(truth_in_fiducial,
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
                    analysis_channel = AnalysisChannels::to_int(AnalysisChannels::Channel::kUnknown);
                    is_signal = false;
                }

                event_weight = compute_nominal_weight(sample,
                                                      has_weight_spline_times_tune,
                                                      weight_spline_times_tune,
                                                      weight_spline,
                                                      weight_tune,
                                                      has_ppfx_cv,
                                                      ppfx_cv,
                                                      has_rootino_fix,
                                                      rootino_fix);
                event_weight_squared = event_weight * event_weight;
                selected->Fill();
            }
        }

        selected->Write("selected", TObject::kOverwrite);
        return selected.release();
    }

    void copy_subrun_tree(TDirectory *subruns_dir,
                          const DatasetIO::Sample &sample,
                          const std::string &subrun_tree_name)
    {
        TChain chain(subrun_tree_name.c_str());
        for (const auto &path : sample.root_files)
            chain.Add(path.c_str());

        if (chain.GetNtrees() == 0)
            throw std::runtime_error("EventListIO: no input trees found for subrun tree " + subrun_tree_name);

        subruns_dir->cd();
        std::unique_ptr<TTree> copied(chain.CloneTree(-1, "fast"));
        if (!copied)
            throw std::runtime_error("EventListIO: failed to copy subrun tree");
        copied->SetName(subrun_tree_name.c_str());
        copied->Write(subrun_tree_name.c_str(), TObject::kOverwrite);
    }
}

EventListIO::EventListIO(const std::string &path, Mode mode)
    : path_(path), mode_(mode)
{
    const char *open_mode = "READ";
    if (mode_ == Mode::kWrite)
        open_mode = "RECREATE";
    else if (mode_ == Mode::kUpdate)
        open_mode = "UPDATE";
    file_ = TFile::Open(path_.c_str(), open_mode);
    if (!file_ || file_->IsZombie())
    {
        delete file_;
        file_ = nullptr;
        throw std::runtime_error("EventListIO: failed to open: " + path_);
    }
}

EventListIO::~EventListIO()
{
    if (file_)
    {
        file_->Close();
        delete file_;
        file_ = nullptr;
    }
}

void EventListIO::require_open_() const
{
    if (!file_)
        throw std::runtime_error("EventListIO: file is not open");
}

std::vector<std::string> EventListIO::sample_keys() const
{
    require_open_();
    TDirectory *samples = utils::must_dir(file_, "samples", false);
    return utils::list_keys(samples);
}

TTree *EventListIO::selected_tree(const std::string &sample_key) const
{
    require_open_();
    TDirectory *samples = utils::must_dir(file_, "samples", false);
    TDirectory *sample_dir = utils::must_subdir(samples, sample_key, false, "samples");
    TDirectory *events_dir = utils::must_dir(sample_dir, "events", false);
    TTree *tree = dynamic_cast<TTree *>(events_dir->Get("selected"));
    if (!tree)
        throw std::runtime_error("EventListIO: missing selected tree for sample: " + sample_key);
    return tree;
}

TTree *EventListIO::subrun_tree(const std::string &sample_key) const
{
    require_open_();
    TDirectory *meta_dir = utils::must_dir(file_, "meta", false);
    const std::string subrun_tree_name = utils::read_named(meta_dir, "subrun_tree_name");

    TDirectory *samples = utils::must_dir(file_, "samples", false);
    TDirectory *sample_dir = utils::must_subdir(samples, sample_key, false, "samples");
    TDirectory *subruns_dir = utils::must_dir(sample_dir, "subruns", false);
    TTree *tree = dynamic_cast<TTree *>(subruns_dir->Get(subrun_tree_name.c_str()));
    if (!tree)
        throw std::runtime_error("EventListIO: missing subrun tree for sample: " + sample_key);
    return tree;
}

std::vector<std::string> EventListIO::systematics_cache_keys(const std::string &sample_key) const
{
    require_open_();
    TDirectory *systematics_dir = systematics_dir_for(file_, sample_key, false);
    return utils::list_keys(systematics_dir);
}

bool EventListIO::has_systematics_cache(const std::string &sample_key, const std::string &cache_key) const
{
    require_open_();
    try
    {
        (void)systematics_cache_dir_for(file_, sample_key, cache_key, false);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

EventListIO::SystematicsCacheEntry EventListIO::read_systematics_cache(const std::string &sample_key,
                                                                       const std::string &cache_key) const
{
    require_open_();

    TDirectory *cache_dir = systematics_cache_dir_for(file_, sample_key, cache_key, false);
    TTree *payload = dynamic_cast<TTree *>(cache_dir->Get("payload"));
    if (!payload)
        throw std::runtime_error("EventListIO: missing systematics payload for sample/cache: " +
                                 sample_key + "/" + cache_key);

    int version = 0;
    int nbins = 0;
    double xmin = 0.0;
    double xmax = 0.0;
    int detector_template_count = 0;

    std::string *branch_expr = nullptr;
    std::string *selection_expr = nullptr;
    std::vector<std::string> *detector_sample_keys = nullptr;

    std::vector<double> *nominal = nullptr;
    std::vector<double> *detector_down = nullptr;
    std::vector<double> *detector_up = nullptr;
    std::vector<double> *detector_templates = nullptr;
    std::vector<double> *total_down = nullptr;
    std::vector<double> *total_up = nullptr;

    std::string *genie_branch_name = nullptr;
    long long genie_n_variations = 0;
    int genie_eigen_rank = 0;
    std::vector<double> *genie_sigma = nullptr;
    std::vector<double> *genie_covariance = nullptr;
    std::vector<double> *genie_eigenvalues = nullptr;
    std::vector<double> *genie_eigenmodes = nullptr;

    std::string *flux_branch_name = nullptr;
    long long flux_n_variations = 0;
    int flux_eigen_rank = 0;
    std::vector<double> *flux_sigma = nullptr;
    std::vector<double> *flux_covariance = nullptr;
    std::vector<double> *flux_eigenvalues = nullptr;
    std::vector<double> *flux_eigenmodes = nullptr;

    std::string *reint_branch_name = nullptr;
    long long reint_n_variations = 0;
    int reint_eigen_rank = 0;
    std::vector<double> *reint_sigma = nullptr;
    std::vector<double> *reint_covariance = nullptr;
    std::vector<double> *reint_eigenvalues = nullptr;
    std::vector<double> *reint_eigenmodes = nullptr;

    payload->SetBranchAddress("version", &version);
    payload->SetBranchAddress("branch_expr", &branch_expr);
    payload->SetBranchAddress("selection_expr", &selection_expr);
    payload->SetBranchAddress("nbins", &nbins);
    payload->SetBranchAddress("xmin", &xmin);
    payload->SetBranchAddress("xmax", &xmax);
    payload->SetBranchAddress("detector_sample_keys", &detector_sample_keys);
    payload->SetBranchAddress("detector_template_count", &detector_template_count);
    payload->SetBranchAddress("nominal", &nominal);
    payload->SetBranchAddress("detector_down", &detector_down);
    payload->SetBranchAddress("detector_up", &detector_up);
    payload->SetBranchAddress("detector_templates", &detector_templates);
    payload->SetBranchAddress("total_down", &total_down);
    payload->SetBranchAddress("total_up", &total_up);

    payload->SetBranchAddress("genie_branch_name", &genie_branch_name);
    payload->SetBranchAddress("genie_n_variations", &genie_n_variations);
    payload->SetBranchAddress("genie_eigen_rank", &genie_eigen_rank);
    payload->SetBranchAddress("genie_sigma", &genie_sigma);
    payload->SetBranchAddress("genie_covariance", &genie_covariance);
    payload->SetBranchAddress("genie_eigenvalues", &genie_eigenvalues);
    payload->SetBranchAddress("genie_eigenmodes", &genie_eigenmodes);

    payload->SetBranchAddress("flux_branch_name", &flux_branch_name);
    payload->SetBranchAddress("flux_n_variations", &flux_n_variations);
    payload->SetBranchAddress("flux_eigen_rank", &flux_eigen_rank);
    payload->SetBranchAddress("flux_sigma", &flux_sigma);
    payload->SetBranchAddress("flux_covariance", &flux_covariance);
    payload->SetBranchAddress("flux_eigenvalues", &flux_eigenvalues);
    payload->SetBranchAddress("flux_eigenmodes", &flux_eigenmodes);

    payload->SetBranchAddress("reint_branch_name", &reint_branch_name);
    payload->SetBranchAddress("reint_n_variations", &reint_n_variations);
    payload->SetBranchAddress("reint_eigen_rank", &reint_eigen_rank);
    payload->SetBranchAddress("reint_sigma", &reint_sigma);
    payload->SetBranchAddress("reint_covariance", &reint_covariance);
    payload->SetBranchAddress("reint_eigenvalues", &reint_eigenvalues);
    payload->SetBranchAddress("reint_eigenmodes", &reint_eigenmodes);

    if (payload->GetEntries() <= 0)
        throw std::runtime_error("EventListIO: empty systematics payload for sample/cache: " +
                                 sample_key + "/" + cache_key);
    payload->GetEntry(0);

    SystematicsCacheEntry entry;
    entry.version = version;
    entry.branch_expr = branch_expr ? *branch_expr : std::string();
    entry.selection_expr = selection_expr ? *selection_expr : std::string();
    entry.nbins = nbins;
    entry.xmin = xmin;
    entry.xmax = xmax;
    entry.detector_sample_keys = detector_sample_keys ? *detector_sample_keys : std::vector<std::string>{};
    entry.detector_template_count = detector_template_count;
    entry.nominal = nominal ? *nominal : std::vector<double>{};
    entry.detector_down = detector_down ? *detector_down : std::vector<double>{};
    entry.detector_up = detector_up ? *detector_up : std::vector<double>{};
    entry.detector_templates = detector_templates ? *detector_templates : std::vector<double>{};
    entry.total_down = total_down ? *total_down : std::vector<double>{};
    entry.total_up = total_up ? *total_up : std::vector<double>{};

    entry.genie.branch_name = genie_branch_name ? *genie_branch_name : std::string();
    entry.genie.n_variations = genie_n_variations;
    entry.genie.eigen_rank = genie_eigen_rank;
    entry.genie.sigma = genie_sigma ? *genie_sigma : std::vector<double>{};
    entry.genie.covariance = genie_covariance ? *genie_covariance : std::vector<double>{};
    entry.genie.eigenvalues = genie_eigenvalues ? *genie_eigenvalues : std::vector<double>{};
    entry.genie.eigenmodes = genie_eigenmodes ? *genie_eigenmodes : std::vector<double>{};

    entry.flux.branch_name = flux_branch_name ? *flux_branch_name : std::string();
    entry.flux.n_variations = flux_n_variations;
    entry.flux.eigen_rank = flux_eigen_rank;
    entry.flux.sigma = flux_sigma ? *flux_sigma : std::vector<double>{};
    entry.flux.covariance = flux_covariance ? *flux_covariance : std::vector<double>{};
    entry.flux.eigenvalues = flux_eigenvalues ? *flux_eigenvalues : std::vector<double>{};
    entry.flux.eigenmodes = flux_eigenmodes ? *flux_eigenmodes : std::vector<double>{};

    entry.reint.branch_name = reint_branch_name ? *reint_branch_name : std::string();
    entry.reint.n_variations = reint_n_variations;
    entry.reint.eigen_rank = reint_eigen_rank;
    entry.reint.sigma = reint_sigma ? *reint_sigma : std::vector<double>{};
    entry.reint.covariance = reint_covariance ? *reint_covariance : std::vector<double>{};
    entry.reint.eigenvalues = reint_eigenvalues ? *reint_eigenvalues : std::vector<double>{};
    entry.reint.eigenmodes = reint_eigenmodes ? *reint_eigenmodes : std::vector<double>{};

    return entry;
}

void EventListIO::write_systematics_cache(const std::string &sample_key,
                                          const std::string &cache_key,
                                          const SystematicsCacheEntry &entry)
{
    require_open_();
    if (mode_ == Mode::kRead)
        throw std::runtime_error("EventListIO: write_systematics_cache requires write or update mode");

    TDirectory *cache_dir = systematics_cache_dir_for(file_, sample_key, cache_key, true);
    cache_dir->cd();

    int version = entry.version;
    std::string branch_expr = entry.branch_expr;
    std::string selection_expr = entry.selection_expr;
    int nbins = entry.nbins;
    double xmin = entry.xmin;
    double xmax = entry.xmax;
    std::vector<std::string> detector_sample_keys = entry.detector_sample_keys;
    int detector_template_count = entry.detector_template_count;
    std::vector<double> nominal = entry.nominal;
    std::vector<double> detector_down = entry.detector_down;
    std::vector<double> detector_up = entry.detector_up;
    std::vector<double> detector_templates = entry.detector_templates;
    std::vector<double> total_down = entry.total_down;
    std::vector<double> total_up = entry.total_up;

    std::string genie_branch_name = entry.genie.branch_name;
    long long genie_n_variations = entry.genie.n_variations;
    int genie_eigen_rank = entry.genie.eigen_rank;
    std::vector<double> genie_sigma = entry.genie.sigma;
    std::vector<double> genie_covariance = entry.genie.covariance;
    std::vector<double> genie_eigenvalues = entry.genie.eigenvalues;
    std::vector<double> genie_eigenmodes = entry.genie.eigenmodes;

    std::string flux_branch_name = entry.flux.branch_name;
    long long flux_n_variations = entry.flux.n_variations;
    int flux_eigen_rank = entry.flux.eigen_rank;
    std::vector<double> flux_sigma = entry.flux.sigma;
    std::vector<double> flux_covariance = entry.flux.covariance;
    std::vector<double> flux_eigenvalues = entry.flux.eigenvalues;
    std::vector<double> flux_eigenmodes = entry.flux.eigenmodes;

    std::string reint_branch_name = entry.reint.branch_name;
    long long reint_n_variations = entry.reint.n_variations;
    int reint_eigen_rank = entry.reint.eigen_rank;
    std::vector<double> reint_sigma = entry.reint.sigma;
    std::vector<double> reint_covariance = entry.reint.covariance;
    std::vector<double> reint_eigenvalues = entry.reint.eigenvalues;
    std::vector<double> reint_eigenmodes = entry.reint.eigenmodes;

    TTree payload("payload", "Persistent systematics cache");
    payload.Branch("version", &version, "version/I");
    payload.Branch("branch_expr", &branch_expr);
    payload.Branch("selection_expr", &selection_expr);
    payload.Branch("nbins", &nbins, "nbins/I");
    payload.Branch("xmin", &xmin, "xmin/D");
    payload.Branch("xmax", &xmax, "xmax/D");
    payload.Branch("detector_sample_keys", &detector_sample_keys);
    payload.Branch("detector_template_count", &detector_template_count, "detector_template_count/I");
    payload.Branch("nominal", &nominal);
    payload.Branch("detector_down", &detector_down);
    payload.Branch("detector_up", &detector_up);
    payload.Branch("detector_templates", &detector_templates);
    payload.Branch("total_down", &total_down);
    payload.Branch("total_up", &total_up);

    payload.Branch("genie_branch_name", &genie_branch_name);
    payload.Branch("genie_n_variations", &genie_n_variations, "genie_n_variations/L");
    payload.Branch("genie_eigen_rank", &genie_eigen_rank, "genie_eigen_rank/I");
    payload.Branch("genie_sigma", &genie_sigma);
    payload.Branch("genie_covariance", &genie_covariance);
    payload.Branch("genie_eigenvalues", &genie_eigenvalues);
    payload.Branch("genie_eigenmodes", &genie_eigenmodes);

    payload.Branch("flux_branch_name", &flux_branch_name);
    payload.Branch("flux_n_variations", &flux_n_variations, "flux_n_variations/L");
    payload.Branch("flux_eigen_rank", &flux_eigen_rank, "flux_eigen_rank/I");
    payload.Branch("flux_sigma", &flux_sigma);
    payload.Branch("flux_covariance", &flux_covariance);
    payload.Branch("flux_eigenvalues", &flux_eigenvalues);
    payload.Branch("flux_eigenmodes", &flux_eigenmodes);

    payload.Branch("reint_branch_name", &reint_branch_name);
    payload.Branch("reint_n_variations", &reint_n_variations, "reint_n_variations/L");
    payload.Branch("reint_eigen_rank", &reint_eigen_rank, "reint_eigen_rank/I");
    payload.Branch("reint_sigma", &reint_sigma);
    payload.Branch("reint_covariance", &reint_covariance);
    payload.Branch("reint_eigenvalues", &reint_eigenvalues);
    payload.Branch("reint_eigenmodes", &reint_eigenmodes);

    payload.Fill();
    payload.Write("payload", TObject::kOverwrite);
    file_->Write(nullptr, TObject::kOverwrite);
}

void EventListIO::skim(const DatasetIO &ds,
                       const std::string &event_tree_name,
                       const std::string &subrun_tree_name,
                       const std::string &selection_expr,
                       const std::string &selection_name,
                       const EventListSelection::Config &selection_config)
{
    if (mode_ != Mode::kWrite)
        throw std::runtime_error("EventListIO: skim requires write mode");
    if (event_tree_name.empty())
        throw std::runtime_error("EventListIO: event_tree_name must not be empty");
    if (subrun_tree_name.empty())
        throw std::runtime_error("EventListIO: subrun_tree_name must not be empty");
    if (selection_expr.empty() && selection_name.empty())
        throw std::runtime_error("EventListIO: selection_expr must not be empty");

    try
    {
        TDirectory *meta_dir = utils::must_dir(file_, "meta", true);
        utils::write_named(meta_dir, "dataset_path", ds.path());
        utils::write_named(meta_dir, "dataset_context", ds.context());
        utils::write_named(meta_dir, "event_tree_name", event_tree_name);
        utils::write_named(meta_dir, "subrun_tree_name", subrun_tree_name);
        utils::write_named(meta_dir, "selection_name", selection_name);
        utils::write_named(meta_dir, "selection_expr", selection_expr);
        utils::write_param<int>(meta_dir, "slice_required_count", selection_config.slice_required_count);
        utils::write_param<double>(meta_dir, "slice_min_topology_score", selection_config.slice_min_topology_score);
        utils::write_param<int>(meta_dir, "numi_run_boundary", selection_config.numi_run_boundary);

        TDirectory *samples_root = utils::must_dir(file_, "samples", true);
        for (const auto &key : ds.sample_keys())
        {
            const DatasetIO::Sample sample = ds.sample(key);

            TDirectory *sample_dir = utils::must_subdir(samples_root, key, true, "samples");
            TDirectory *sample_meta_dir = utils::must_dir(sample_dir, "meta", true);
            TDirectory *events_dir = utils::must_dir(sample_dir, "events", true);
            TDirectory *subruns_dir = utils::must_dir(sample_dir, "subruns", true);

            write_sample_metadata(sample_meta_dir, sample);
            TChain preview_chain(event_tree_name.c_str());
            for (const auto &path : sample.root_files)
                preview_chain.Add(path.c_str());
            if (preview_chain.GetNtrees() == 0)
                throw std::runtime_error("EventListIO: no input trees found for event tree " + event_tree_name);

            std::string effective_selection_expr = selection_expr;
            if (!selection_name.empty() && selection_name != "raw")
            {
                TTree *preview_tree = preview_chain.GetTree();
                if (!preview_tree)
                {
                    preview_chain.LoadTree(0);
                    preview_tree = preview_chain.GetTree();
                }
                effective_selection_expr = EventListSelection::expression(
                    EventListSelection::preset_from_string(selection_name),
                    sample,
                    branch_names(preview_tree),
                    selection_config);
            }
            copy_selected_tree(events_dir, sample, event_tree_name, effective_selection_expr, selection_config);
            copy_subrun_tree(subruns_dir, sample, subrun_tree_name);
        }

        file_->Write();
    }
    catch (...)
    {
        file_->Close();
        throw;
    }
}
