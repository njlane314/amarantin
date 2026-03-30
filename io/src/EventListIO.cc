#include "EventListIO.hh"
#include "detail/RootUtils.hh"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

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

    TTree *copy_selected_tree(TDirectory *events_dir,
                              const DatasetIO::Sample &sample,
                              const std::string &event_tree_name,
                              const std::string &selection_expr)
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

        float weight_spline = 1.0f;
        float weight_tune = 1.0f;
        float weight_spline_times_tune = 1.0f;
        float ppfx_cv = 1.0f;
        double rootino_fix = 1.0;

        const bool has_weight_spline = chain.GetBranch("weightSpline") != nullptr;
        const bool has_weight_tune = chain.GetBranch("weightTune") != nullptr;
        const bool has_weight_spline_times_tune = chain.GetBranch("weightSplineTimesTune") != nullptr;
        const bool has_ppfx_cv = chain.GetBranch("ppfx_cv") != nullptr;
        const bool has_rootino_fix = chain.GetBranch("RootinoFix") != nullptr;

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

        double event_weight = 1.0;
        double event_weight_squared = 1.0;
        selected->Branch(kEventWeightBranch, &event_weight, (std::string(kEventWeightBranch) + "/D").c_str());
        selected->Branch(kEventWeightSquaredBranch,
                         &event_weight_squared,
                         (std::string(kEventWeightSquaredBranch) + "/D").c_str());

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
            }

            chain.GetEntry(i);
            if (selection->EvalInstance() != 0.0)
            {
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
    const char *open_mode = mode_ == Mode::kRead ? "READ" : "RECREATE";
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

            copy_selected_tree(events_dir, sample, event_tree_name, effective_selection_expr);
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
