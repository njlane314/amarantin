#include "ChannelIO.hh"
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
    using Family = ChannelIO::Family;
    using Process = ChannelIO::Process;

    TDirectory *channels_dir_for(TFile *file, bool create)
    {
        return utils::must_dir(file, "channels", create);
    }

    TDirectory *channel_dir_for(TFile *file, const std::string &channel_key, bool create)
    {
        TDirectory *channels = channels_dir_for(file, create);
        return utils::must_subdir(channels, channel_key, create, "channels");
    }

    TDirectory *proc_root_for(TDirectory *channel_dir, bool create)
    {
        return utils::must_dir(channel_dir, "proc", create);
    }

    void write_channel_meta(TDirectory *meta_dir,
                            const std::string &channel_key,
                            const ChannelIO::Channel &channel)
    {
        utils::write_named(meta_dir, "channel_key", channel_key);
        utils::write_named(meta_dir, "branch_expr", channel.spec.branch_expr);
        utils::write_named(meta_dir, "selection_expr", channel.spec.selection_expr);
        utils::write_param<int>(meta_dir, "nbins", channel.spec.nbins);
        utils::write_param<double>(meta_dir, "xmin", channel.spec.xmin);
        utils::write_param<double>(meta_dir, "xmax", channel.spec.xmax);
    }

    void write_process_meta(TDirectory *meta_dir, const Process &process)
    {
        utils::write_named(meta_dir, "name", process.name);
        utils::write_named(meta_dir, "kind", ChannelIO::process_kind_name(process.kind));
    }

    void branch_family(TTree &payload,
                       const char *prefix,
                       std::string &branch_name,
                       long long &n_variations,
                       int &eigen_rank,
                       std::vector<double> &sigma,
                       std::vector<double> &covariance,
                       std::vector<double> &eigenvalues,
                       std::vector<double> &eigenmodes)
    {
        const std::string stem(prefix);
        payload.Branch((stem + "_branch_name").c_str(), &branch_name);
        payload.Branch((stem + "_n_variations").c_str(), &n_variations);
        payload.Branch((stem + "_eigen_rank").c_str(), &eigen_rank);
        payload.Branch((stem + "_sigma").c_str(), &sigma);
        payload.Branch((stem + "_covariance").c_str(), &covariance);
        payload.Branch((stem + "_eigenvalues").c_str(), &eigenvalues);
        payload.Branch((stem + "_eigenmodes").c_str(), &eigenmodes);
    }

    void set_family_addresses(TTree *payload,
                              const char *prefix,
                              std::string **branch_name,
                              long long *n_variations,
                              int *eigen_rank,
                              std::vector<double> **sigma,
                              std::vector<double> **covariance,
                              std::vector<double> **eigenvalues,
                              std::vector<double> **eigenmodes)
    {
        const std::string stem(prefix);
        payload->SetBranchAddress((stem + "_branch_name").c_str(), branch_name);
        payload->SetBranchAddress((stem + "_n_variations").c_str(), n_variations);
        payload->SetBranchAddress((stem + "_eigen_rank").c_str(), eigen_rank);
        payload->SetBranchAddress((stem + "_sigma").c_str(), sigma);
        payload->SetBranchAddress((stem + "_covariance").c_str(), covariance);
        payload->SetBranchAddress((stem + "_eigenvalues").c_str(), eigenvalues);
        payload->SetBranchAddress((stem + "_eigenmodes").c_str(), eigenmodes);
    }

    void fill_family(Family &out,
                     std::string *branch_name,
                     long long n_variations,
                     int eigen_rank,
                     std::vector<double> *sigma,
                     std::vector<double> *covariance,
                     std::vector<double> *eigenvalues,
                     std::vector<double> *eigenmodes)
    {
        out.branch_name = branch_name ? *branch_name : std::string();
        out.n_variations = n_variations;
        out.eigen_rank = eigen_rank;
        out.sigma = sigma ? *sigma : std::vector<double>{};
        out.covariance = covariance ? *covariance : std::vector<double>{};
        out.eigenvalues = eigenvalues ? *eigenvalues : std::vector<double>{};
        out.eigenmodes = eigenmodes ? *eigenmodes : std::vector<double>{};
    }

    void write_process_payload(TDirectory *proc_dir, const Process &entry)
    {
        std::vector<std::string> source_keys = entry.source_keys;
        std::vector<std::string> detector_sample_keys = entry.detector_sample_keys;
        std::vector<double> nominal = entry.nominal;
        std::vector<double> sumw2 = entry.sumw2;
        std::vector<double> detector_down = entry.detector_down;
        std::vector<double> detector_up = entry.detector_up;
        std::vector<double> detector_templates = entry.detector_templates;
        int detector_template_count = entry.detector_template_count;
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

        proc_dir->cd();
        TTree payload("payload", "Channel process payload");
        payload.Branch("source_keys", &source_keys);
        payload.Branch("detector_sample_keys", &detector_sample_keys);
        payload.Branch("nominal", &nominal);
        payload.Branch("sumw2", &sumw2);
        payload.Branch("detector_template_count", &detector_template_count);
        payload.Branch("detector_down", &detector_down);
        payload.Branch("detector_up", &detector_up);
        payload.Branch("detector_templates", &detector_templates);
        payload.Branch("total_down", &total_down);
        payload.Branch("total_up", &total_up);

        branch_family(payload,
                      "genie",
                      genie_branch_name,
                      genie_n_variations,
                      genie_eigen_rank,
                      genie_sigma,
                      genie_covariance,
                      genie_eigenvalues,
                      genie_eigenmodes);
        branch_family(payload,
                      "flux",
                      flux_branch_name,
                      flux_n_variations,
                      flux_eigen_rank,
                      flux_sigma,
                      flux_covariance,
                      flux_eigenvalues,
                      flux_eigenmodes);
        branch_family(payload,
                      "reint",
                      reint_branch_name,
                      reint_n_variations,
                      reint_eigen_rank,
                      reint_sigma,
                      reint_covariance,
                      reint_eigenvalues,
                      reint_eigenmodes);

        payload.Fill();
        payload.Write("payload", TObject::kOverwrite);
    }

    Process read_process(TDirectory *proc_dir)
    {
        TDirectory *meta_dir = utils::must_dir(proc_dir, "meta", false);
        TTree *payload = dynamic_cast<TTree *>(proc_dir->Get("payload"));
        if (!payload)
            throw std::runtime_error("ChannelIO: missing process payload");

        Process process;
        process.name = utils::read_named(meta_dir, "name");
        process.kind = ChannelIO::process_kind_from(utils::read_named(meta_dir, "kind"));

        std::vector<std::string> *source_keys = nullptr;
        std::vector<std::string> *detector_sample_keys = nullptr;
        std::vector<double> *nominal = nullptr;
        std::vector<double> *sumw2 = nullptr;
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

        payload->SetBranchAddress("source_keys", &source_keys);
        if (payload->GetBranch("detector_sample_keys"))
            payload->SetBranchAddress("detector_sample_keys", &detector_sample_keys);
        payload->SetBranchAddress("nominal", &nominal);
        payload->SetBranchAddress("sumw2", &sumw2);
        payload->SetBranchAddress("detector_template_count", &detector_template_count);
        payload->SetBranchAddress("detector_down", &detector_down);
        payload->SetBranchAddress("detector_up", &detector_up);
        payload->SetBranchAddress("detector_templates", &detector_templates);
        payload->SetBranchAddress("total_down", &total_down);
        payload->SetBranchAddress("total_up", &total_up);

        set_family_addresses(payload,
                             "genie",
                             &genie_branch_name,
                             &genie_n_variations,
                             &genie_eigen_rank,
                             &genie_sigma,
                             &genie_covariance,
                             &genie_eigenvalues,
                             &genie_eigenmodes);
        set_family_addresses(payload,
                             "flux",
                             &flux_branch_name,
                             &flux_n_variations,
                             &flux_eigen_rank,
                             &flux_sigma,
                             &flux_covariance,
                             &flux_eigenvalues,
                             &flux_eigenmodes);
        set_family_addresses(payload,
                             "reint",
                             &reint_branch_name,
                             &reint_n_variations,
                             &reint_eigen_rank,
                             &reint_sigma,
                             &reint_covariance,
                             &reint_eigenvalues,
                             &reint_eigenmodes);

        if (payload->GetEntries() <= 0)
            throw std::runtime_error("ChannelIO: empty process payload");
        payload->GetEntry(0);

        process.source_keys = source_keys ? *source_keys : std::vector<std::string>{};
        process.detector_sample_keys =
            detector_sample_keys ? *detector_sample_keys : std::vector<std::string>{};
        process.nominal = nominal ? *nominal : std::vector<double>{};
        process.sumw2 = sumw2 ? *sumw2 : std::vector<double>{};
        process.detector_template_count = detector_template_count;
        process.detector_down = detector_down ? *detector_down : std::vector<double>{};
        process.detector_up = detector_up ? *detector_up : std::vector<double>{};
        process.detector_templates = detector_templates ? *detector_templates : std::vector<double>{};
        process.total_down = total_down ? *total_down : std::vector<double>{};
        process.total_up = total_up ? *total_up : std::vector<double>{};

        fill_family(process.genie,
                    genie_branch_name,
                    genie_n_variations,
                    genie_eigen_rank,
                    genie_sigma,
                    genie_covariance,
                    genie_eigenvalues,
                    genie_eigenmodes);
        fill_family(process.flux,
                    flux_branch_name,
                    flux_n_variations,
                    flux_eigen_rank,
                    flux_sigma,
                    flux_covariance,
                    flux_eigenvalues,
                    flux_eigenmodes);
        fill_family(process.reint,
                    reint_branch_name,
                    reint_n_variations,
                    reint_eigen_rank,
                    reint_sigma,
                    reint_covariance,
                    reint_eigenvalues,
                    reint_eigenmodes);

        return process;
    }
}

const ChannelIO::Process *ChannelIO::Channel::find_process(const std::string &name) const
{
    for (const auto &process : processes)
    {
        if (process.name == name)
            return &process;
    }
    return nullptr;
}

ChannelIO::Process *ChannelIO::Channel::find_process(const std::string &name)
{
    for (auto &process : processes)
    {
        if (process.name == name)
            return &process;
    }
    return nullptr;
}

const char *ChannelIO::process_kind_name(ProcessKind kind)
{
    switch (kind)
    {
        case ProcessKind::kData: return "data";
        case ProcessKind::kSignal: return "signal";
        case ProcessKind::kBackground: return "background";
        default: return "background";
    }
}

ChannelIO::ProcessKind ChannelIO::process_kind_from(const std::string &name)
{
    const std::string key = utils::lower(name);
    if (key == "data")
        return ProcessKind::kData;
    if (key == "signal")
        return ProcessKind::kSignal;
    return ProcessKind::kBackground;
}

ChannelIO::ChannelIO(const std::string &path, Mode mode)
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
        throw std::runtime_error("ChannelIO: failed to open: " + path_);
    }
}

ChannelIO::~ChannelIO()
{
    if (file_)
    {
        file_->Close();
        delete file_;
        file_ = nullptr;
    }
}

void ChannelIO::require_open_() const
{
    if (!file_)
        throw std::runtime_error("ChannelIO: file is not open");
}

ChannelIO::Metadata ChannelIO::metadata() const
{
    require_open_();
    TDirectory *meta_dir = utils::must_dir(file_, "meta", false);

    Metadata metadata;
    metadata.distribution_path = utils::read_named_or(meta_dir, "distribution_path");
    metadata.build_version = utils::read_param<int>(meta_dir, "build_version");
    return metadata;
}

void ChannelIO::write_metadata(const Metadata &metadata)
{
    require_open_();
    if (mode_ == Mode::kRead)
        throw std::runtime_error("ChannelIO: write_metadata requires write or update mode");

    TDirectory *meta_dir = utils::must_dir(file_, "meta", true);
    utils::write_named(meta_dir, "distribution_path", metadata.distribution_path);
    utils::write_param<int>(meta_dir, "build_version", metadata.build_version);
}

void ChannelIO::flush()
{
    require_open_();
    if (mode_ == Mode::kRead)
        return;
    file_->Write(nullptr, TObject::kOverwrite);
}

std::vector<std::string> ChannelIO::channel_keys() const
{
    require_open_();
    TDirectory *channels = channels_dir_for(file_, false);
    return utils::list_keys(channels);
}

std::vector<std::string> ChannelIO::process_names(const std::string &channel_key) const
{
    require_open_();
    TDirectory *channel_dir = channel_dir_for(file_, channel_key, false);
    TDirectory *proc_root = proc_root_for(channel_dir, false);
    return utils::list_keys(proc_root);
}

bool ChannelIO::has(const std::string &channel_key) const
{
    require_open_();
    try
    {
        (void)channel_dir_for(file_, channel_key, false);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

ChannelIO::Channel ChannelIO::read(const std::string &channel_key) const
{
    require_open_();

    TDirectory *channel_dir = channel_dir_for(file_, channel_key, false);
    TDirectory *meta_dir = utils::must_dir(channel_dir, "meta", false);

    Channel channel;
    channel.spec.channel_key = utils::read_named(meta_dir, "channel_key");
    channel.spec.branch_expr = utils::read_named(meta_dir, "branch_expr");
    channel.spec.selection_expr = utils::read_named_or(meta_dir, "selection_expr");
    channel.spec.nbins = utils::read_param<int>(meta_dir, "nbins");
    channel.spec.xmin = utils::read_param<double>(meta_dir, "xmin");
    channel.spec.xmax = utils::read_param<double>(meta_dir, "xmax");

    if (TDirectory *data_dir = channel_dir->GetDirectory("data"))
    {
        TTree *payload = dynamic_cast<TTree *>(data_dir->Get("payload"));
        if (payload)
        {
            std::vector<double> *data = nullptr;
            payload->SetBranchAddress("data", &data);
            if (payload->GetEntries() > 0)
            {
                payload->GetEntry(0);
                channel.data = data ? *data : std::vector<double>{};
            }
        }
    }

    if (TDirectory *proc_root = channel_dir->GetDirectory("proc"))
    {
        const auto names = utils::list_keys(proc_root);
        channel.processes.reserve(names.size());
        for (const auto &name : names)
        {
            TDirectory *proc_dir = proc_root->GetDirectory(name.c_str());
            if (!proc_dir)
                throw std::runtime_error("ChannelIO: missing process directory: " + name);
            channel.processes.push_back(read_process(proc_dir));
        }
    }

    return channel;
}

void ChannelIO::write(const std::string &channel_key, const Channel &channel)
{
    require_open_();
    if (mode_ == Mode::kRead)
        throw std::runtime_error("ChannelIO: write requires write or update mode");
    if (channel_key.empty())
        throw std::runtime_error("ChannelIO: channel_key must not be empty");

    TDirectory *channel_dir = channel_dir_for(file_, channel_key, true);
    TDirectory *meta_dir = utils::must_dir(channel_dir, "meta", true);
    write_channel_meta(meta_dir, channel_key, channel);

    {
        TDirectory *data_dir = utils::must_dir(channel_dir, "data", true);
        std::vector<double> data = channel.data;
        data_dir->cd();
        TTree payload("payload", "Channel data payload");
        payload.Branch("data", &data);
        payload.Fill();
        payload.Write("payload", TObject::kOverwrite);
    }

    TDirectory *proc_root = proc_root_for(channel_dir, true);
    for (const auto &process : channel.processes)
    {
        if (process.name.empty())
            throw std::runtime_error("ChannelIO: process name must not be empty");

        TDirectory *proc_dir = utils::must_subdir(proc_root, process.name, true, "channels/proc");
        TDirectory *proc_meta = utils::must_dir(proc_dir, "meta", true);
        write_process_meta(proc_meta, process);
        write_process_payload(proc_dir, process);
    }
}
