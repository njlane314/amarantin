#ifndef DISTRIBUTION_IO_HH
#define DISTRIBUTION_IO_HH

#include <string>
#include <vector>

class TFile;

class DistributionIO
{
public:
    enum class Mode { kRead, kWrite, kUpdate };

    struct Metadata
    {
        std::string eventlist_path;
        int build_version = 1;
    };

    struct Spec
    {
        std::string sample_key;
        std::string branch_expr;
        std::string selection_expr;
        int nbins = 0;
        double xmin = 0.0;
        double xmax = 0.0;
        std::string cache_key;
    };

    struct Family
    {
        std::string branch_name;
        long long n_variations = 0;
        int eigen_rank = 0;
        std::vector<double> sigma;
        std::vector<double> covariance;
        std::vector<double> eigenvalues;
        std::vector<double> eigenmodes;
        std::vector<double> universe_histograms;

        bool empty() const
        {
            return branch_name.empty() &&
                   sigma.empty() &&
                   covariance.empty() &&
                   eigenvalues.empty() &&
                   eigenmodes.empty() &&
                   universe_histograms.empty();
        }
    };

    struct Spectrum
    {
        Spec spec;
        std::vector<double> nominal;
        std::vector<double> sumw2;
        std::vector<std::string> detector_source_labels;
        std::vector<std::string> detector_cv_sample_keys;
        std::vector<std::string> detector_sample_keys;
        std::vector<double> detector_shift_vectors;
        int detector_source_count = 0;
        std::vector<double> detector_covariance;
        std::vector<double> detector_down;
        std::vector<double> detector_up;
        std::vector<double> detector_templates;
        int detector_template_count = 0;
        Family genie;
        Family flux;
        Family reint;
        std::vector<double> total_down;
        std::vector<double> total_up;
    };

    explicit DistributionIO(const std::string &path, Mode mode = Mode::kRead);
    ~DistributionIO();

    DistributionIO(const DistributionIO &) = delete;
    DistributionIO &operator=(const DistributionIO &) = delete;

    const std::string &path() const { return path_; }
    Mode mode() const { return mode_; }

    Metadata metadata() const;
    void write_metadata(const Metadata &metadata);
    void flush();

    std::vector<std::string> sample_keys() const;
    std::vector<std::string> dist_keys(const std::string &sample_key) const;
    bool has(const std::string &sample_key, const std::string &cache_key) const;
    Spectrum read(const std::string &sample_key, const std::string &cache_key) const;
    void write(const std::string &sample_key,
               const std::string &cache_key,
               const Spectrum &spectrum);

private:
    void require_open_() const;

    std::string path_;
    Mode mode_;
    TFile *file_ = nullptr;
};

std::string default_distribution_path(const std::string &eventlist_path);

#endif // DISTRIBUTION_IO_HH
