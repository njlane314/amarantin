#include "DistributionIO.hh"
#include "bits/RootUtils.hh"

#include <stdexcept>
#include <string>
#include <vector>

#include <TDirectory.h>
#include <TFile.h>
#include <TObject.h>
#include <TTree.h>

namespace
{
    TDirectory *sample_dir_for(TFile *file, const std::string &sample_key, bool create)
    {
        TDirectory *samples = utils::must_dir(file, "samples", create);
        return utils::must_subdir(samples, sample_key, create, "samples");
    }

    TDirectory *dists_dir_for(TFile *file, const std::string &sample_key, bool create)
    {
        TDirectory *sample_dir = sample_dir_for(file, sample_key, create);
        return utils::must_dir(sample_dir, "dists", create);
    }

    TDirectory *dist_dir_for(TFile *file,
                             const std::string &sample_key,
                             const std::string &cache_key,
                             bool create)
    {
        TDirectory *dists_dir = dists_dir_for(file, sample_key, create);
        return utils::must_subdir(dists_dir, cache_key, create, "dists");
    }

    void write_entry_meta(TDirectory *meta_dir,
                          const std::string &sample_key,
                          const std::string &cache_key,
                          const DistributionIO::Entry &entry)
    {
        utils::write_named(meta_dir, "sample_key", sample_key);
        utils::write_named(meta_dir, "branch_expr", entry.spec.branch_expr);
        utils::write_named(meta_dir, "selection_expr", entry.spec.selection_expr);
        utils::write_named(meta_dir, "cache_key", cache_key);
        utils::write_param<int>(meta_dir, "nbins", entry.spec.nbins);
        utils::write_param<double>(meta_dir, "xmin", entry.spec.xmin);
        utils::write_param<double>(meta_dir, "xmax", entry.spec.xmax);
    }
}

DistributionIO::DistributionIO(const std::string &path, Mode mode)
    : path_(path), mode_(mode)
{
    const char *open_mode = "READ";
    if (mode_ == Mode::kWrite)
        open_mode = "RECREATE";
    else if (mode_ == Mode::kUpdate)
        open_mode = "UPDATE";

    file_ = TFile::Open(path_.c_str(), open_mode);
    if ((!file_ || file_->IsZombie()) && mode_ == Mode::kUpdate)
    {
        delete file_;
        file_ = TFile::Open(path_.c_str(), "RECREATE");
    }
    if (!file_ || file_->IsZombie())
    {
        delete file_;
        file_ = nullptr;
        throw std::runtime_error("DistributionIO: failed to open: " + path_);
    }
}

DistributionIO::~DistributionIO()
{
    if (file_)
    {
        file_->Close();
        delete file_;
        file_ = nullptr;
    }
}

void DistributionIO::require_open_() const
{
    if (!file_)
        throw std::runtime_error("DistributionIO: file is not open");
}

DistributionIO::Metadata DistributionIO::metadata() const
{
    require_open_();
    TDirectory *meta_dir = utils::must_dir(file_, "meta", false);

    Metadata metadata;
    metadata.eventlist_path = utils::read_named(meta_dir, "eventlist_path");
    metadata.build_version = utils::read_param<int>(meta_dir, "build_version");
    return metadata;
}

void DistributionIO::write_metadata(const Metadata &metadata)
{
    require_open_();
    if (mode_ == Mode::kRead)
        throw std::runtime_error("DistributionIO: write_metadata requires write or update mode");

    TDirectory *meta_dir = utils::must_dir(file_, "meta", true);
    utils::write_named(meta_dir, "eventlist_path", metadata.eventlist_path);
    utils::write_param<int>(meta_dir, "build_version", metadata.build_version);
}

void DistributionIO::flush()
{
    require_open_();
    if (mode_ == Mode::kRead)
        return;
    file_->Write(nullptr, TObject::kOverwrite);
}

std::vector<std::string> DistributionIO::sample_keys() const
{
    require_open_();
    TDirectory *samples = utils::must_dir(file_, "samples", false);
    return utils::list_keys(samples);
}

std::vector<std::string> DistributionIO::dist_keys(const std::string &sample_key) const
{
    require_open_();
    TDirectory *dists_dir = dists_dir_for(file_, sample_key, false);
    return utils::list_keys(dists_dir);
}

bool DistributionIO::has(const std::string &sample_key, const std::string &cache_key) const
{
    require_open_();
    try
    {
        (void)dist_dir_for(file_, sample_key, cache_key, false);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

DistributionIO::Entry DistributionIO::read(const std::string &sample_key,
                                           const std::string &cache_key) const
{
    require_open_();

    TDirectory *entry_dir = dist_dir_for(file_, sample_key, cache_key, false);
    TDirectory *meta_dir = utils::must_dir(entry_dir, "meta", false);
    TTree *payload = dynamic_cast<TTree *>(entry_dir->Get("payload"));
    if (!payload)
        throw std::runtime_error("DistributionIO: missing payload for sample/cache: " +
                                 sample_key + "/" + cache_key);

    Entry entry;
    entry.spec.sample_key = utils::read_named(meta_dir, "sample_key");
    entry.spec.branch_expr = utils::read_named(meta_dir, "branch_expr");
    entry.spec.selection_expr = utils::read_named_or(meta_dir, "selection_expr");
    entry.spec.cache_key = utils::read_named(meta_dir, "cache_key");
    entry.spec.nbins = utils::read_param<int>(meta_dir, "nbins");
    entry.spec.xmin = utils::read_param<double>(meta_dir, "xmin");
    entry.spec.xmax = utils::read_param<double>(meta_dir, "xmax");

    std::vector<double> *nominal = nullptr;
    std::vector<double> *sumw2 = nullptr;
    std::vector<std::string> *detector_sample_keys = nullptr;
    std::vector<double> *detector_down = nullptr;
    std::vector<double> *detector_up = nullptr;
    std::vector<double> *detector_templates = nullptr;
    int detector_template_count = 0;
    std::vector<double> *total_down = nullptr;
    std::vector<double> *total_up = nullptr;

    std::string *genie_branch_name = nullptr;
    long long genie_n_variations = 0;
    int genie_eigen_rank = 0;
    std::vector<double> *genie_sigma = nullptr;
    std::vector<double> *genie_covariance = nullptr;
    std::vector<double> *genie_eigenvalues = nullptr;
    std::vector<double> *genie_eigenmodes = nullptr;
    std::vector<double> *genie_universe_histograms = nullptr;

    std::string *flux_branch_name = nullptr;
    long long flux_n_variations = 0;
    int flux_eigen_rank = 0;
    std::vector<double> *flux_sigma = nullptr;
    std::vector<double> *flux_covariance = nullptr;
    std::vector<double> *flux_eigenvalues = nullptr;
    std::vector<double> *flux_eigenmodes = nullptr;
    std::vector<double> *flux_universe_histograms = nullptr;

    std::string *reint_branch_name = nullptr;
    long long reint_n_variations = 0;
    int reint_eigen_rank = 0;
    std::vector<double> *reint_sigma = nullptr;
    std::vector<double> *reint_covariance = nullptr;
    std::vector<double> *reint_eigenvalues = nullptr;
    std::vector<double> *reint_eigenmodes = nullptr;
    std::vector<double> *reint_universe_histograms = nullptr;

    payload->SetBranchAddress("nominal", &nominal);
    payload->SetBranchAddress("sumw2", &sumw2);
    payload->SetBranchAddress("detector_sample_keys", &detector_sample_keys);
    payload->SetBranchAddress("detector_template_count", &detector_template_count);
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
    if (payload->GetBranch("genie_universe_histograms"))
        payload->SetBranchAddress("genie_universe_histograms", &genie_universe_histograms);

    payload->SetBranchAddress("flux_branch_name", &flux_branch_name);
    payload->SetBranchAddress("flux_n_variations", &flux_n_variations);
    payload->SetBranchAddress("flux_eigen_rank", &flux_eigen_rank);
    payload->SetBranchAddress("flux_sigma", &flux_sigma);
    payload->SetBranchAddress("flux_covariance", &flux_covariance);
    payload->SetBranchAddress("flux_eigenvalues", &flux_eigenvalues);
    payload->SetBranchAddress("flux_eigenmodes", &flux_eigenmodes);
    if (payload->GetBranch("flux_universe_histograms"))
        payload->SetBranchAddress("flux_universe_histograms", &flux_universe_histograms);

    payload->SetBranchAddress("reint_branch_name", &reint_branch_name);
    payload->SetBranchAddress("reint_n_variations", &reint_n_variations);
    payload->SetBranchAddress("reint_eigen_rank", &reint_eigen_rank);
    payload->SetBranchAddress("reint_sigma", &reint_sigma);
    payload->SetBranchAddress("reint_covariance", &reint_covariance);
    payload->SetBranchAddress("reint_eigenvalues", &reint_eigenvalues);
    payload->SetBranchAddress("reint_eigenmodes", &reint_eigenmodes);
    if (payload->GetBranch("reint_universe_histograms"))
        payload->SetBranchAddress("reint_universe_histograms", &reint_universe_histograms);

    if (payload->GetEntries() <= 0)
        throw std::runtime_error("DistributionIO: empty payload for sample/cache: " +
                                 sample_key + "/" + cache_key);
    payload->GetEntry(0);

    entry.nominal = nominal ? *nominal : std::vector<double>{};
    entry.sumw2 = sumw2 ? *sumw2 : std::vector<double>{};
    entry.detector_sample_keys = detector_sample_keys ? *detector_sample_keys : std::vector<std::string>{};
    entry.detector_template_count = detector_template_count;
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
    entry.genie.universe_histograms = genie_universe_histograms ? *genie_universe_histograms : std::vector<double>{};

    entry.flux.branch_name = flux_branch_name ? *flux_branch_name : std::string();
    entry.flux.n_variations = flux_n_variations;
    entry.flux.eigen_rank = flux_eigen_rank;
    entry.flux.sigma = flux_sigma ? *flux_sigma : std::vector<double>{};
    entry.flux.covariance = flux_covariance ? *flux_covariance : std::vector<double>{};
    entry.flux.eigenvalues = flux_eigenvalues ? *flux_eigenvalues : std::vector<double>{};
    entry.flux.eigenmodes = flux_eigenmodes ? *flux_eigenmodes : std::vector<double>{};
    entry.flux.universe_histograms = flux_universe_histograms ? *flux_universe_histograms : std::vector<double>{};

    entry.reint.branch_name = reint_branch_name ? *reint_branch_name : std::string();
    entry.reint.n_variations = reint_n_variations;
    entry.reint.eigen_rank = reint_eigen_rank;
    entry.reint.sigma = reint_sigma ? *reint_sigma : std::vector<double>{};
    entry.reint.covariance = reint_covariance ? *reint_covariance : std::vector<double>{};
    entry.reint.eigenvalues = reint_eigenvalues ? *reint_eigenvalues : std::vector<double>{};
    entry.reint.eigenmodes = reint_eigenmodes ? *reint_eigenmodes : std::vector<double>{};
    entry.reint.universe_histograms = reint_universe_histograms ? *reint_universe_histograms : std::vector<double>{};

    return entry;
}

void DistributionIO::write(const std::string &sample_key,
                           const std::string &cache_key,
                           const Entry &entry)
{
    require_open_();
    if (mode_ == Mode::kRead)
        throw std::runtime_error("DistributionIO: write requires write or update mode");
    if (sample_key.empty())
        throw std::runtime_error("DistributionIO: sample_key must not be empty");
    if (cache_key.empty())
        throw std::runtime_error("DistributionIO: cache_key must not be empty");

    TDirectory *entry_dir = dist_dir_for(file_, sample_key, cache_key, true);
    TDirectory *meta_dir = utils::must_dir(entry_dir, "meta", true);
    write_entry_meta(meta_dir, sample_key, cache_key, entry);

    std::vector<double> nominal = entry.nominal;
    std::vector<double> sumw2 = entry.sumw2;
    std::vector<std::string> detector_sample_keys = entry.detector_sample_keys;
    int detector_template_count = entry.detector_template_count;
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
    std::vector<double> genie_universe_histograms = entry.genie.universe_histograms;

    std::string flux_branch_name = entry.flux.branch_name;
    long long flux_n_variations = entry.flux.n_variations;
    int flux_eigen_rank = entry.flux.eigen_rank;
    std::vector<double> flux_sigma = entry.flux.sigma;
    std::vector<double> flux_covariance = entry.flux.covariance;
    std::vector<double> flux_eigenvalues = entry.flux.eigenvalues;
    std::vector<double> flux_eigenmodes = entry.flux.eigenmodes;
    std::vector<double> flux_universe_histograms = entry.flux.universe_histograms;

    std::string reint_branch_name = entry.reint.branch_name;
    long long reint_n_variations = entry.reint.n_variations;
    int reint_eigen_rank = entry.reint.eigen_rank;
    std::vector<double> reint_sigma = entry.reint.sigma;
    std::vector<double> reint_covariance = entry.reint.covariance;
    std::vector<double> reint_eigenvalues = entry.reint.eigenvalues;
    std::vector<double> reint_eigenmodes = entry.reint.eigenmodes;
    std::vector<double> reint_universe_histograms = entry.reint.universe_histograms;

    entry_dir->cd();
    TTree payload("payload", "Distribution payload");
    payload.Branch("nominal", &nominal);
    payload.Branch("sumw2", &sumw2);
    payload.Branch("detector_sample_keys", &detector_sample_keys);
    payload.Branch("detector_template_count", &detector_template_count);
    payload.Branch("detector_down", &detector_down);
    payload.Branch("detector_up", &detector_up);
    payload.Branch("detector_templates", &detector_templates);
    payload.Branch("total_down", &total_down);
    payload.Branch("total_up", &total_up);

    payload.Branch("genie_branch_name", &genie_branch_name);
    payload.Branch("genie_n_variations", &genie_n_variations);
    payload.Branch("genie_eigen_rank", &genie_eigen_rank);
    payload.Branch("genie_sigma", &genie_sigma);
    payload.Branch("genie_covariance", &genie_covariance);
    payload.Branch("genie_eigenvalues", &genie_eigenvalues);
    payload.Branch("genie_eigenmodes", &genie_eigenmodes);
    payload.Branch("genie_universe_histograms", &genie_universe_histograms);

    payload.Branch("flux_branch_name", &flux_branch_name);
    payload.Branch("flux_n_variations", &flux_n_variations);
    payload.Branch("flux_eigen_rank", &flux_eigen_rank);
    payload.Branch("flux_sigma", &flux_sigma);
    payload.Branch("flux_covariance", &flux_covariance);
    payload.Branch("flux_eigenvalues", &flux_eigenvalues);
    payload.Branch("flux_eigenmodes", &flux_eigenmodes);
    payload.Branch("flux_universe_histograms", &flux_universe_histograms);

    payload.Branch("reint_branch_name", &reint_branch_name);
    payload.Branch("reint_n_variations", &reint_n_variations);
    payload.Branch("reint_eigen_rank", &reint_eigen_rank);
    payload.Branch("reint_sigma", &reint_sigma);
    payload.Branch("reint_covariance", &reint_covariance);
    payload.Branch("reint_eigenvalues", &reint_eigenvalues);
    payload.Branch("reint_eigenmodes", &reint_eigenmodes);
    payload.Branch("reint_universe_histograms", &reint_universe_histograms);

    payload.Fill();
    payload.Write("payload", TObject::kOverwrite);
}

std::string default_distribution_path(const std::string &eventlist_path)
{
    if (eventlist_path.size() >= 5 &&
        eventlist_path.substr(eventlist_path.size() - 5) == ".root")
    {
        return eventlist_path.substr(0, eventlist_path.size() - 5) + ".dists.root";
    }
    return eventlist_path + ".dists.root";
}
