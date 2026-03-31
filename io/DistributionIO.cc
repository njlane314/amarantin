#include "DistributionIO.hh"
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

    void write_spectrum_meta(TDirectory *meta_dir,
                             const std::string &sample_key,
                             const std::string &cache_key,
                             const DistributionIO::Spectrum &spectrum)
    {
        utils::write_named(meta_dir, "sample_key", sample_key);
        utils::write_named(meta_dir, "branch_expr", spectrum.spec.branch_expr);
        utils::write_named(meta_dir, "selection_expr", spectrum.spec.selection_expr);
        utils::write_named(meta_dir, "cache_key", cache_key);
        utils::write_param<int>(meta_dir, "nbins", spectrum.spec.nbins);
        utils::write_param<double>(meta_dir, "xmin", spectrum.spec.xmin);
        utils::write_param<double>(meta_dir, "xmax", spectrum.spec.xmax);
    }

    struct RebinMatrix
    {
        int target_nbins = 0;
        int source_nbins = 0;
        std::vector<double> weights;

        double operator()(int target_bin, int source_bin) const
        {
            return weights[static_cast<std::size_t>(target_bin * source_nbins + source_bin)];
        }
    };

    RebinMatrix build_rebin_matrix(const DistributionIO::HistogramSpec &source_spec,
                                   const DistributionIO::HistogramSpec &target_spec)
    {
        if (source_spec.nbins <= 0 || target_spec.nbins <= 0)
            throw std::runtime_error("DistributionIO: invalid rebinning range");
        if (!(source_spec.xmax > source_spec.xmin) || !(target_spec.xmax > target_spec.xmin))
            throw std::runtime_error("DistributionIO: invalid rebinning range");

        RebinMatrix matrix;
        matrix.target_nbins = target_spec.nbins;
        matrix.source_nbins = source_spec.nbins;
        matrix.weights.assign(static_cast<std::size_t>(target_spec.nbins * source_spec.nbins), 0.0);

        const double source_width =
            (source_spec.xmax - source_spec.xmin) / static_cast<double>(source_spec.nbins);
        const double target_width =
            (target_spec.xmax - target_spec.xmin) / static_cast<double>(target_spec.nbins);
        if (source_width <= 0.0 || target_width <= 0.0)
            throw std::runtime_error("DistributionIO: invalid rebinning range");

        for (int target_bin = 0; target_bin < target_spec.nbins; ++target_bin)
        {
            const double target_low =
                target_spec.xmin + target_width * static_cast<double>(target_bin);
            const double target_high = target_low + target_width;

            for (int source_bin = 0; source_bin < source_spec.nbins; ++source_bin)
            {
                const double source_low =
                    source_spec.xmin + source_width * static_cast<double>(source_bin);
                const double source_high = source_low + source_width;
                const double overlap =
                    std::max(0.0,
                             std::min(source_high, target_high) -
                                 std::max(source_low, target_low));
                if (overlap > 0.0)
                    matrix.weights[static_cast<std::size_t>(target_bin * source_spec.nbins + source_bin)] =
                        overlap / source_width;
            }
        }

        return matrix;
    }

    std::vector<double> rebin_values_with_matrix(const std::vector<double> &source,
                                                 int source_nbins,
                                                 const RebinMatrix &matrix)
    {
        if (source.empty())
            return {};
        if (source.size() != static_cast<std::size_t>(source_nbins))
            throw std::runtime_error("DistributionIO: value payload size does not match source bins");

        std::vector<double> out(static_cast<std::size_t>(matrix.target_nbins), 0.0);
        for (int target_bin = 0; target_bin < matrix.target_nbins; ++target_bin)
        {
            double sum = 0.0;
            for (int source_bin = 0; source_bin < source_nbins; ++source_bin)
                sum += matrix(target_bin, source_bin) * source[static_cast<std::size_t>(source_bin)];
            out[static_cast<std::size_t>(target_bin)] = sum;
        }
        return out;
    }

    std::vector<double> rebin_source_major_payload_with_matrix(const std::vector<double> &source_payload,
                                                               int row_count,
                                                               int source_nbins,
                                                               const RebinMatrix &matrix)
    {
        if (source_payload.empty())
            return {};
        if (row_count <= 0)
            return {};
        if (source_payload.size() != static_cast<std::size_t>(row_count * source_nbins))
            throw std::runtime_error("DistributionIO: row-major payload is truncated");

        std::vector<double> out(static_cast<std::size_t>(row_count * matrix.target_nbins), 0.0);
        for (int row = 0; row < row_count; ++row)
        {
            for (int target_bin = 0; target_bin < matrix.target_nbins; ++target_bin)
            {
                double sum = 0.0;
                for (int source_bin = 0; source_bin < source_nbins; ++source_bin)
                {
                    sum += matrix(target_bin, source_bin) *
                           source_payload[static_cast<std::size_t>(row * source_nbins + source_bin)];
                }
                out[static_cast<std::size_t>(row * matrix.target_nbins + target_bin)] = sum;
            }
        }
        return out;
    }

    std::vector<double> rebin_bin_major_payload_with_matrix(const std::vector<double> &source_payload,
                                                            int source_nbins,
                                                            int column_count,
                                                            const RebinMatrix &matrix)
    {
        if (source_payload.empty())
            return {};
        if (column_count <= 0)
            return {};
        if (source_payload.size() != static_cast<std::size_t>(source_nbins * column_count))
            throw std::runtime_error("DistributionIO: bin-major payload is truncated");

        std::vector<double> out(static_cast<std::size_t>(matrix.target_nbins * column_count), 0.0);
        for (int target_bin = 0; target_bin < matrix.target_nbins; ++target_bin)
        {
            for (int source_bin = 0; source_bin < source_nbins; ++source_bin)
            {
                const double weight = matrix(target_bin, source_bin);
                if (weight == 0.0)
                    continue;
                for (int col = 0; col < column_count; ++col)
                {
                    out[static_cast<std::size_t>(target_bin * column_count + col)] +=
                        weight *
                        source_payload[static_cast<std::size_t>(source_bin * column_count + col)];
                }
            }
        }
        return out;
    }

    std::vector<double> rebin_covariance_with_matrix(const std::vector<double> &source_covariance,
                                                     int source_nbins,
                                                     const RebinMatrix &matrix)
    {
        if (source_covariance.empty())
            return {};
        if (source_covariance.size() !=
            static_cast<std::size_t>(source_nbins * source_nbins))
        {
            throw std::runtime_error("DistributionIO: covariance payload is truncated");
        }

        std::vector<double> temp(static_cast<std::size_t>(matrix.target_nbins * source_nbins), 0.0);
        for (int target_row = 0; target_row < matrix.target_nbins; ++target_row)
        {
            for (int source_col = 0; source_col < source_nbins; ++source_col)
            {
                double sum = 0.0;
                for (int source_row = 0; source_row < source_nbins; ++source_row)
                {
                    sum += matrix(target_row, source_row) *
                           source_covariance[static_cast<std::size_t>(source_row * source_nbins + source_col)];
                }
                temp[static_cast<std::size_t>(target_row * source_nbins + source_col)] = sum;
            }
        }

        std::vector<double> out(static_cast<std::size_t>(matrix.target_nbins * matrix.target_nbins), 0.0);
        for (int target_row = 0; target_row < matrix.target_nbins; ++target_row)
        {
            for (int target_col = 0; target_col < matrix.target_nbins; ++target_col)
            {
                double sum = 0.0;
                for (int source_col = 0; source_col < source_nbins; ++source_col)
                {
                    sum += temp[static_cast<std::size_t>(target_row * source_nbins + source_col)] *
                           matrix(target_col, source_col);
                }
                out[static_cast<std::size_t>(target_row * matrix.target_nbins + target_col)] = sum;
            }
        }
        return out;
    }
}

std::vector<double> DistributionIO::Spectrum::rebinned_values(const std::vector<double> &source,
                                                              const HistogramSpec &target_spec) const
{
    if (source.empty())
        return {};
    if (same_binning(target_spec))
    {
        if (source.size() != static_cast<std::size_t>(spec.nbins))
            throw std::runtime_error("DistributionIO: value payload size does not match source bins");
        return source;
    }

    return rebin_values_with_matrix(source,
                                    spec.nbins,
                                    build_rebin_matrix(spec, target_spec));
}

std::vector<double> DistributionIO::Spectrum::rebinned_covariance(
    const std::vector<double> &source_covariance,
    const HistogramSpec &target_spec) const
{
    if (source_covariance.empty())
        return {};
    if (same_binning(target_spec))
    {
        if (source_covariance.size() != static_cast<std::size_t>(spec.nbins * spec.nbins))
            throw std::runtime_error("DistributionIO: covariance payload is truncated");
        return source_covariance;
    }

    return rebin_covariance_with_matrix(source_covariance,
                                        spec.nbins,
                                        build_rebin_matrix(spec, target_spec));
}

std::vector<double> DistributionIO::Spectrum::rebinned_source_major_payload(
    const std::vector<double> &source_payload,
    int row_count,
    const HistogramSpec &target_spec) const
{
    if (source_payload.empty())
        return {};
    if (same_binning(target_spec))
    {
        if (row_count > 0 &&
            source_payload.size() != static_cast<std::size_t>(row_count * spec.nbins))
        {
            throw std::runtime_error("DistributionIO: row-major payload is truncated");
        }
        return source_payload;
    }

    return rebin_source_major_payload_with_matrix(source_payload,
                                                  row_count,
                                                  spec.nbins,
                                                  build_rebin_matrix(spec, target_spec));
}

std::vector<double> DistributionIO::Spectrum::rebinned_bin_major_payload(
    const std::vector<double> &source_payload,
    int column_count,
    const HistogramSpec &target_spec) const
{
    if (source_payload.empty())
        return {};
    if (same_binning(target_spec))
    {
        if (column_count > 0 &&
            source_payload.size() != static_cast<std::size_t>(spec.nbins * column_count))
        {
            throw std::runtime_error("DistributionIO: bin-major payload is truncated");
        }
        return source_payload;
    }

    return rebin_bin_major_payload_with_matrix(source_payload,
                                               spec.nbins,
                                               column_count,
                                               build_rebin_matrix(spec, target_spec));
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

DistributionIO::Spectrum DistributionIO::read(const std::string &sample_key,
                                              const std::string &cache_key) const
{
    require_open_();

    TDirectory *spectrum_dir = dist_dir_for(file_, sample_key, cache_key, false);
    TDirectory *meta_dir = utils::must_dir(spectrum_dir, "meta", false);
    TTree *payload = dynamic_cast<TTree *>(spectrum_dir->Get("payload"));
    if (!payload)
        throw std::runtime_error("DistributionIO: missing payload for sample/cache: " +
                                 sample_key + "/" + cache_key);

    Spectrum spectrum;
    spectrum.spec.sample_key = utils::read_named(meta_dir, "sample_key");
    spectrum.spec.branch_expr = utils::read_named(meta_dir, "branch_expr");
    spectrum.spec.selection_expr = utils::read_named_or(meta_dir, "selection_expr");
    spectrum.spec.cache_key = utils::read_named(meta_dir, "cache_key");
    spectrum.spec.nbins = utils::read_param<int>(meta_dir, "nbins");
    spectrum.spec.xmin = utils::read_param<double>(meta_dir, "xmin");
    spectrum.spec.xmax = utils::read_param<double>(meta_dir, "xmax");

    std::vector<double> *nominal = nullptr;
    std::vector<double> *sumw2 = nullptr;
    std::vector<std::string> *detector_source_labels = nullptr;
    std::vector<std::string> *detector_cv_sample_keys = nullptr;
    std::vector<std::string> *detector_sample_keys = nullptr;
    std::vector<double> *detector_shift_vectors = nullptr;
    int detector_source_count = 0;
    std::vector<double> *detector_covariance = nullptr;
    std::vector<std::string> *genie_knob_source_labels = nullptr;
    std::vector<double> *genie_knob_shift_vectors = nullptr;
    int genie_knob_source_count = 0;
    std::vector<double> *genie_knob_covariance = nullptr;
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
    if (payload->GetBranch("detector_source_labels"))
        payload->SetBranchAddress("detector_source_labels", &detector_source_labels);
    if (payload->GetBranch("detector_cv_sample_keys"))
        payload->SetBranchAddress("detector_cv_sample_keys", &detector_cv_sample_keys);
    if (payload->GetBranch("detector_sample_keys"))
        payload->SetBranchAddress("detector_sample_keys", &detector_sample_keys);
    if (payload->GetBranch("detector_shift_vectors"))
        payload->SetBranchAddress("detector_shift_vectors", &detector_shift_vectors);
    if (payload->GetBranch("detector_source_count"))
        payload->SetBranchAddress("detector_source_count", &detector_source_count);
    if (payload->GetBranch("detector_covariance"))
        payload->SetBranchAddress("detector_covariance", &detector_covariance);
    if (payload->GetBranch("genie_knob_source_labels"))
        payload->SetBranchAddress("genie_knob_source_labels", &genie_knob_source_labels);
    if (payload->GetBranch("genie_knob_shift_vectors"))
        payload->SetBranchAddress("genie_knob_shift_vectors", &genie_knob_shift_vectors);
    if (payload->GetBranch("genie_knob_source_count"))
        payload->SetBranchAddress("genie_knob_source_count", &genie_knob_source_count);
    if (payload->GetBranch("genie_knob_covariance"))
        payload->SetBranchAddress("genie_knob_covariance", &genie_knob_covariance);
    if (payload->GetBranch("detector_template_count"))
        payload->SetBranchAddress("detector_template_count", &detector_template_count);
    if (payload->GetBranch("detector_down"))
        payload->SetBranchAddress("detector_down", &detector_down);
    if (payload->GetBranch("detector_up"))
        payload->SetBranchAddress("detector_up", &detector_up);
    if (payload->GetBranch("detector_templates"))
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

    spectrum.nominal = nominal ? *nominal : std::vector<double>{};
    spectrum.sumw2 = sumw2 ? *sumw2 : std::vector<double>{};
    spectrum.detector_source_labels = detector_source_labels ? *detector_source_labels : std::vector<std::string>{};
    spectrum.detector_cv_sample_keys = detector_cv_sample_keys ? *detector_cv_sample_keys : std::vector<std::string>{};
    spectrum.detector_sample_keys = detector_sample_keys ? *detector_sample_keys : std::vector<std::string>{};
    spectrum.detector_shift_vectors = detector_shift_vectors ? *detector_shift_vectors : std::vector<double>{};
    spectrum.detector_source_count = detector_source_count;
    spectrum.detector_covariance = detector_covariance ? *detector_covariance : std::vector<double>{};
    spectrum.genie_knob_source_labels = genie_knob_source_labels ? *genie_knob_source_labels : std::vector<std::string>{};
    spectrum.genie_knob_shift_vectors = genie_knob_shift_vectors ? *genie_knob_shift_vectors : std::vector<double>{};
    spectrum.genie_knob_source_count = genie_knob_source_count;
    spectrum.genie_knob_covariance = genie_knob_covariance ? *genie_knob_covariance : std::vector<double>{};
    spectrum.detector_template_count = detector_template_count;
    spectrum.detector_down = detector_down ? *detector_down : std::vector<double>{};
    spectrum.detector_up = detector_up ? *detector_up : std::vector<double>{};
    spectrum.detector_templates = detector_templates ? *detector_templates : std::vector<double>{};
    spectrum.total_down = total_down ? *total_down : std::vector<double>{};
    spectrum.total_up = total_up ? *total_up : std::vector<double>{};

    spectrum.genie.branch_name = genie_branch_name ? *genie_branch_name : std::string();
    spectrum.genie.n_variations = genie_n_variations;
    spectrum.genie.eigen_rank = genie_eigen_rank;
    spectrum.genie.sigma = genie_sigma ? *genie_sigma : std::vector<double>{};
    spectrum.genie.covariance = genie_covariance ? *genie_covariance : std::vector<double>{};
    spectrum.genie.eigenvalues = genie_eigenvalues ? *genie_eigenvalues : std::vector<double>{};
    spectrum.genie.eigenmodes = genie_eigenmodes ? *genie_eigenmodes : std::vector<double>{};
    spectrum.genie.universe_histograms = genie_universe_histograms ? *genie_universe_histograms : std::vector<double>{};

    spectrum.flux.branch_name = flux_branch_name ? *flux_branch_name : std::string();
    spectrum.flux.n_variations = flux_n_variations;
    spectrum.flux.eigen_rank = flux_eigen_rank;
    spectrum.flux.sigma = flux_sigma ? *flux_sigma : std::vector<double>{};
    spectrum.flux.covariance = flux_covariance ? *flux_covariance : std::vector<double>{};
    spectrum.flux.eigenvalues = flux_eigenvalues ? *flux_eigenvalues : std::vector<double>{};
    spectrum.flux.eigenmodes = flux_eigenmodes ? *flux_eigenmodes : std::vector<double>{};
    spectrum.flux.universe_histograms = flux_universe_histograms ? *flux_universe_histograms : std::vector<double>{};

    spectrum.reint.branch_name = reint_branch_name ? *reint_branch_name : std::string();
    spectrum.reint.n_variations = reint_n_variations;
    spectrum.reint.eigen_rank = reint_eigen_rank;
    spectrum.reint.sigma = reint_sigma ? *reint_sigma : std::vector<double>{};
    spectrum.reint.covariance = reint_covariance ? *reint_covariance : std::vector<double>{};
    spectrum.reint.eigenvalues = reint_eigenvalues ? *reint_eigenvalues : std::vector<double>{};
    spectrum.reint.eigenmodes = reint_eigenmodes ? *reint_eigenmodes : std::vector<double>{};
    spectrum.reint.universe_histograms = reint_universe_histograms ? *reint_universe_histograms : std::vector<double>{};

    return spectrum;
}

void DistributionIO::write(const std::string &sample_key,
                           const std::string &cache_key,
                           const Spectrum &spectrum)
{
    require_open_();
    if (mode_ == Mode::kRead)
        throw std::runtime_error("DistributionIO: write requires write or update mode");
    if (sample_key.empty())
        throw std::runtime_error("DistributionIO: sample_key must not be empty");
    if (cache_key.empty())
        throw std::runtime_error("DistributionIO: cache_key must not be empty");

    TDirectory *spectrum_dir = dist_dir_for(file_, sample_key, cache_key, true);
    TDirectory *meta_dir = utils::must_dir(spectrum_dir, "meta", true);
    write_spectrum_meta(meta_dir, sample_key, cache_key, spectrum);

    std::vector<double> nominal = spectrum.nominal;
    std::vector<double> sumw2 = spectrum.sumw2;
    std::vector<std::string> detector_source_labels = spectrum.detector_source_labels;
    std::vector<std::string> detector_cv_sample_keys = spectrum.detector_cv_sample_keys;
    std::vector<std::string> detector_sample_keys = spectrum.detector_sample_keys;
    std::vector<double> detector_shift_vectors = spectrum.detector_shift_vectors;
    int detector_source_count = spectrum.detector_source_count;
    std::vector<double> detector_covariance = spectrum.detector_covariance;
    std::vector<std::string> genie_knob_source_labels = spectrum.genie_knob_source_labels;
    std::vector<double> genie_knob_shift_vectors = spectrum.genie_knob_shift_vectors;
    int genie_knob_source_count = spectrum.genie_knob_source_count;
    std::vector<double> genie_knob_covariance = spectrum.genie_knob_covariance;
    int detector_template_count = spectrum.detector_template_count;
    std::vector<double> detector_down = spectrum.detector_down;
    std::vector<double> detector_up = spectrum.detector_up;
    std::vector<double> detector_templates = spectrum.detector_templates;
    std::vector<double> total_down = spectrum.total_down;
    std::vector<double> total_up = spectrum.total_up;

    std::string genie_branch_name = spectrum.genie.branch_name;
    long long genie_n_variations = spectrum.genie.n_variations;
    int genie_eigen_rank = spectrum.genie.eigen_rank;
    std::vector<double> genie_sigma = spectrum.genie.sigma;
    std::vector<double> genie_covariance = spectrum.genie.covariance;
    std::vector<double> genie_eigenvalues = spectrum.genie.eigenvalues;
    std::vector<double> genie_eigenmodes = spectrum.genie.eigenmodes;
    std::vector<double> genie_universe_histograms = spectrum.genie.universe_histograms;

    std::string flux_branch_name = spectrum.flux.branch_name;
    long long flux_n_variations = spectrum.flux.n_variations;
    int flux_eigen_rank = spectrum.flux.eigen_rank;
    std::vector<double> flux_sigma = spectrum.flux.sigma;
    std::vector<double> flux_covariance = spectrum.flux.covariance;
    std::vector<double> flux_eigenvalues = spectrum.flux.eigenvalues;
    std::vector<double> flux_eigenmodes = spectrum.flux.eigenmodes;
    std::vector<double> flux_universe_histograms = spectrum.flux.universe_histograms;

    std::string reint_branch_name = spectrum.reint.branch_name;
    long long reint_n_variations = spectrum.reint.n_variations;
    int reint_eigen_rank = spectrum.reint.eigen_rank;
    std::vector<double> reint_sigma = spectrum.reint.sigma;
    std::vector<double> reint_covariance = spectrum.reint.covariance;
    std::vector<double> reint_eigenvalues = spectrum.reint.eigenvalues;
    std::vector<double> reint_eigenmodes = spectrum.reint.eigenmodes;
    std::vector<double> reint_universe_histograms = spectrum.reint.universe_histograms;

    spectrum_dir->cd();
    TTree payload("payload", "Distribution payload");
    payload.Branch("nominal", &nominal);
    payload.Branch("sumw2", &sumw2);
    payload.Branch("detector_source_labels", &detector_source_labels);
    payload.Branch("detector_cv_sample_keys", &detector_cv_sample_keys);
    payload.Branch("detector_sample_keys", &detector_sample_keys);
    payload.Branch("detector_shift_vectors", &detector_shift_vectors);
    payload.Branch("detector_source_count", &detector_source_count);
    payload.Branch("detector_covariance", &detector_covariance);
    payload.Branch("genie_knob_source_labels", &genie_knob_source_labels);
    payload.Branch("genie_knob_shift_vectors", &genie_knob_shift_vectors);
    payload.Branch("genie_knob_source_count", &genie_knob_source_count);
    payload.Branch("genie_knob_covariance", &genie_knob_covariance);
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
