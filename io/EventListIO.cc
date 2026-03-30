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
        utils::write_named(meta_dir, "origin", DatasetIO::Sample::origin_name(sample.origin));
        utils::write_named(meta_dir, "variation", DatasetIO::Sample::variation_name(sample.variation));
        utils::write_named(meta_dir, "beam", DatasetIO::Sample::beam_name(sample.beam));
        utils::write_named(meta_dir, "polarity", DatasetIO::Sample::polarity_name(sample.polarity));

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
    sample.origin = DatasetIO::Sample::origin_from(utils::read_named(meta_dir, "origin"));
    sample.variation = DatasetIO::Sample::variation_from(utils::read_named(meta_dir, "variation"));
    sample.beam = DatasetIO::Sample::beam_from(utils::read_named(meta_dir, "beam"));
    sample.polarity = DatasetIO::Sample::polarity_from(utils::read_named(meta_dir, "polarity"));
    sample.subrun_pot_sum = utils::read_param<double>(meta_dir, "subrun_pot_sum");
    sample.db_tortgt_pot_sum = utils::read_param<double>(meta_dir, "db_tortgt_pot_sum");
    sample.normalisation = utils::read_param<double>(meta_dir, "normalisation");

    sample.nominal = utils::read_named_or(meta_dir, "nominal", utils::read_named_or(meta_dir, "nominal_key"));
    sample.tag = utils::read_named_or(meta_dir, "tag", utils::read_named_or(meta_dir, "variant_name"));
    sample.role = utils::read_named_or(meta_dir, "role", utils::read_named_or(meta_dir, "workflow_role"));
    sample.defname = utils::read_named_or(meta_dir, "defname", utils::read_named_or(meta_dir, "source_def"));
    sample.campaign = utils::read_named_or(meta_dir, "campaign");

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
    flush();
}
