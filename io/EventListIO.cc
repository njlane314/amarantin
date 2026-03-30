#include "EventListIO.hh"
#include "bits/RootUtils.hh"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include <TDirectory.h>
#include <TFile.h>
#include <TObject.h>
#include <TTree.h>

namespace
{
    std::string nominal_or_key(const std::string &key, const DatasetIO::Sample &sample)
    {
        return sample.nominal.empty() ? key : sample.nominal;
    }

    void write_sample_metadata(TDirectory *meta_dir, const DatasetIO::Sample &sample)
    {
        utils::write_named(meta_dir, "sample", sample.sample);
        utils::write_named(meta_dir, "origin", DatasetIO::Sample::origin_name(sample.origin));
        utils::write_named(meta_dir, "variation", DatasetIO::Sample::variation_name(sample.variation));
        utils::write_named(meta_dir, "beam", DatasetIO::Sample::beam_name(sample.beam));
        utils::write_named(meta_dir, "polarity", DatasetIO::Sample::polarity_name(sample.polarity));
        utils::write_named(meta_dir, "normalisation_mode", sample.normalisation_mode);

        utils::write_param<double>(meta_dir, "subrun_pot_sum", sample.subrun_pot_sum);
        utils::write_param<double>(meta_dir, "db_tortgt_pot_sum", sample.db_tortgt_pot_sum);
        utils::write_param<double>(meta_dir, "normalisation", sample.normalisation);

        utils::write_named(meta_dir, "nominal", sample.nominal);
        utils::write_named(meta_dir, "tag", sample.tag);
        utils::write_named(meta_dir, "role", sample.role);
        utils::write_named(meta_dir, "defname", sample.defname);
        utils::write_named(meta_dir, "campaign", sample.campaign);
    }

    TDirectory *sample_dir_for(TFile *file, const std::string &sample_key, bool create)
    {
        TDirectory *samples = utils::must_dir(file, "samples", create);
        return utils::must_subdir(samples, sample_key, create, "samples");
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

EventListIO::Metadata EventListIO::metadata() const
{
    require_open_();
    TDirectory *meta_dir = utils::must_dir(file_, "meta", false);

    Metadata metadata;
    metadata.dataset_path = utils::read_named(meta_dir, "dataset_path");
    metadata.dataset_context = utils::read_named(meta_dir, "dataset_context");
    metadata.event_tree_name = utils::read_named(meta_dir, "event_tree_name");
    metadata.subrun_tree_name = utils::read_named(meta_dir, "subrun_tree_name");
    metadata.selection_name = utils::read_named(meta_dir, "selection_name");
    metadata.selection_expr = utils::read_named(meta_dir, "selection_expr");
    metadata.slice_required_count = utils::read_param<int>(meta_dir, "slice_required_count");
    metadata.slice_min_topology_score = utils::read_param<double>(meta_dir, "slice_min_topology_score");
    metadata.numi_run_boundary = utils::read_param<int>(meta_dir, "numi_run_boundary");
    return metadata;
}

void EventListIO::write_metadata(const Metadata &metadata)
{
    require_open_();
    if (mode_ == Mode::kRead)
        throw std::runtime_error("EventListIO: write_metadata requires write or update mode");

    TDirectory *meta_dir = utils::must_dir(file_, "meta", true);
    utils::write_named(meta_dir, "dataset_path", metadata.dataset_path);
    utils::write_named(meta_dir, "dataset_context", metadata.dataset_context);
    utils::write_named(meta_dir, "event_tree_name", metadata.event_tree_name);
    utils::write_named(meta_dir, "subrun_tree_name", metadata.subrun_tree_name);
    utils::write_named(meta_dir, "selection_name", metadata.selection_name);
    utils::write_named(meta_dir, "selection_expr", metadata.selection_expr);
    utils::write_param<int>(meta_dir, "slice_required_count", metadata.slice_required_count);
    utils::write_param<double>(meta_dir, "slice_min_topology_score", metadata.slice_min_topology_score);
    utils::write_param<int>(meta_dir, "numi_run_boundary", metadata.numi_run_boundary);
}

void EventListIO::write_sample(const std::string &sample_key,
                               const DatasetIO::Sample &sample,
                               TTree *selected_tree,
                               TTree *subrun_tree,
                               const std::string &subrun_tree_name)
{
    require_open_();
    if (mode_ == Mode::kRead)
        throw std::runtime_error("EventListIO: write_sample requires write or update mode");
    if (sample_key.empty())
        throw std::runtime_error("EventListIO: sample_key must not be empty");
    if (!selected_tree)
        throw std::runtime_error("EventListIO: selected_tree must not be null");
    if (!subrun_tree)
        throw std::runtime_error("EventListIO: subrun_tree must not be null");
    if (subrun_tree_name.empty())
        throw std::runtime_error("EventListIO: subrun_tree_name must not be empty");

    TDirectory *sample_dir = sample_dir_for(file_, sample_key, true);
    TDirectory *sample_meta_dir = utils::must_dir(sample_dir, "meta", true);
    TDirectory *events_dir = utils::must_dir(sample_dir, "events", true);
    TDirectory *subruns_dir = utils::must_dir(sample_dir, "subruns", true);

    write_sample_metadata(sample_meta_dir, sample);

    events_dir->cd();
    selected_tree->SetName("selected");
    selected_tree->SetTitle("Selected event list");
    selected_tree->Write("selected", TObject::kOverwrite);

    subruns_dir->cd();
    subrun_tree->SetName(subrun_tree_name.c_str());
    subrun_tree->Write(subrun_tree_name.c_str(), TObject::kOverwrite);
}

void EventListIO::flush()
{
    require_open_();
    if (mode_ == Mode::kRead)
        return;
    file_->Write(nullptr, TObject::kOverwrite);
}

std::vector<std::string> EventListIO::sample_keys() const
{
    require_open_();
    TDirectory *samples = utils::must_dir(file_, "samples", false);
    return utils::list_keys(samples);
}

DatasetIO::Sample EventListIO::sample(const std::string &sample_key) const
{
    require_open_();

    TDirectory *sample_dir = sample_dir_for(file_, sample_key, false);
    TDirectory *meta_dir = utils::must_dir(sample_dir, "meta", false);

    DatasetIO::Sample sample;
    sample.sample = utils::read_named_or(meta_dir, "sample");
    sample.origin = DatasetIO::Sample::origin_from(utils::read_named(meta_dir, "origin"));
    sample.variation = DatasetIO::Sample::variation_from(utils::read_named(meta_dir, "variation"));
    sample.beam = DatasetIO::Sample::beam_from(utils::read_named(meta_dir, "beam"));
    sample.polarity = DatasetIO::Sample::polarity_from(utils::read_named(meta_dir, "polarity"));
    sample.normalisation_mode = utils::read_named_or(meta_dir, "normalisation_mode");
    sample.subrun_pot_sum = utils::read_param<double>(meta_dir, "subrun_pot_sum");
    sample.db_tortgt_pot_sum = utils::read_param<double>(meta_dir, "db_tortgt_pot_sum");
    sample.normalisation = utils::read_param<double>(meta_dir, "normalisation");

    sample.nominal = utils::read_named_or(meta_dir, "nominal", utils::read_named_or(meta_dir, "nominal_key"));
    sample.tag = utils::read_named_or(meta_dir, "tag", utils::read_named_or(meta_dir, "variant_name"));
    sample.role = utils::read_named_or(meta_dir, "role", utils::read_named_or(meta_dir, "workflow_role"));
    sample.defname = utils::read_named_or(meta_dir, "defname", utils::read_named_or(meta_dir, "source_def"));
    sample.campaign = utils::read_named_or(meta_dir, "campaign");
    if (sample.normalisation_mode.empty() && sample.normalisation > 0.0)
        sample.normalisation_mode = "scalar";

    return sample;
}

std::vector<std::string> EventListIO::detector_mates(const std::string &sample_key) const
{
    require_open_();

    const DatasetIO::Sample seed = sample(sample_key);
    const std::string seed_nominal = nominal_or_key(sample_key, seed);

    std::vector<std::string> out;
    for (const auto &key : sample_keys())
    {
        if (key == sample_key)
            continue;

        const DatasetIO::Sample candidate = sample(key);
        if (candidate.variation != DatasetIO::Sample::Variation::kDetector)
            continue;
        if (nominal_or_key(key, candidate) != seed_nominal)
            continue;
        out.push_back(key);
    }

    std::sort(out.begin(), out.end());
    return out;
}

TTree *EventListIO::selected_tree(const std::string &sample_key) const
{
    require_open_();
    TDirectory *sample_dir = sample_dir_for(file_, sample_key, false);
    TDirectory *events_dir = utils::must_dir(sample_dir, "events", false);
    TTree *tree = dynamic_cast<TTree *>(events_dir->Get("selected"));
    if (!tree)
        throw std::runtime_error("EventListIO: missing selected tree for sample: " + sample_key);
    return tree;
}

TTree *EventListIO::subrun_tree(const std::string &sample_key) const
{
    require_open_();
    const std::string subrun_tree_name = metadata().subrun_tree_name;

    TDirectory *sample_dir = sample_dir_for(file_, sample_key, false);
    TDirectory *subruns_dir = utils::must_dir(sample_dir, "subruns", false);
    TTree *tree = dynamic_cast<TTree *>(subruns_dir->Get(subrun_tree_name.c_str()));
    if (!tree)
        throw std::runtime_error("EventListIO: missing subrun tree for sample: " + sample_key);
    return tree;
}
