#ifndef SYSTEMATICS_HH
#define SYSTEMATICS_HH

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "DistributionIO.hh"
#include "EventListIO.hh"

class TH1D;

namespace syst
{
    struct HistogramSpec
    {
        std::string branch_expr;
        int nbins = 0;
        double xmin = 0.0;
        double xmax = 0.0;
        std::string selection_expr;
    };

    struct Envelope
    {
        std::vector<double> down;
        std::vector<double> up;

        bool empty() const
        {
            return down.empty() || up.empty();
        }
    };

    struct UniverseFamilyResult
    {
        std::string branch_name;
        std::size_t n_universes = 0;
        int eigen_rank = 0;
        Envelope envelope;
        std::vector<double> sigma;
        std::vector<double> covariance;
        std::vector<double> eigenvalues;
        std::vector<double> eigenmodes;
        std::vector<std::vector<double>> universe_histograms;
    };

    enum class CachePolicy
    {
        kMemoryOnly,
        kLoadOnly,
        kComputeIfMissing,
        kRebuild
    };

    struct SystematicsOptions
    {
        bool enable_memory_cache = true;
        CachePolicy persistent_cache = CachePolicy::kMemoryOnly;
        int cache_nbins = 0;

        bool enable_detector = false;
        std::vector<std::string> detector_sample_keys;

        bool enable_genie = false;
        bool enable_genie_knobs = false;
        bool enable_flux = false;
        bool enable_reint = false;

        bool build_full_covariance = false;
        bool retain_universe_histograms = false;
        bool enable_eigenmode_compression = true;
        int max_eigenmodes = 8;
        double eigenmode_fraction = 0.99;
        bool validate_detector_cv_compatibility = false;
    };

    struct SystematicsResult
    {
        std::string cache_key;
        int cached_nbins = 0;
        bool loaded_from_persistent_cache = false;
        std::vector<double> nominal;
        Envelope detector;
        std::vector<std::string> genie_knob_source_labels;
        int genie_knob_source_count = 0;
        Envelope genie_knobs;

        std::optional<UniverseFamilyResult> genie;
        std::optional<UniverseFamilyResult> flux;
        std::optional<UniverseFamilyResult> reint;

        std::vector<double> total_down;
        std::vector<double> total_up;
    };

    struct CacheRequest
    {
        std::string sample_key;
        std::string branch_expr;
        int nbins = 0;
        double xmin = 0.0;
        double xmax = 0.0;
        std::string selection_expr;
        std::vector<std::string> detector_sample_keys;
    };

    struct CacheBuildOptions
    {
        bool overwrite_existing = true;
        int cache_nbins = 0;

        bool enable_genie = false;
        bool enable_genie_knobs = false;
        bool enable_flux = false;
        bool enable_reint = false;

        bool build_full_covariance = false;
        bool retain_universe_histograms = false;
        bool enable_eigenmode_compression = true;
        int max_eigenmodes = 8;
        double eigenmode_fraction = 0.99;
        bool validate_detector_cv_compatibility = false;

        std::vector<CacheRequest> requests;

        bool active() const
        {
            return !requests.empty();
        }
    };

    SystematicsResult evaluate(EventListIO &eventlist,
                               const std::string &sample_key,
                               const HistogramSpec &spec,
                               const SystematicsOptions &options = SystematicsOptions{});

    SystematicsResult evaluate(EventListIO &eventlist,
                               DistributionIO &distfile,
                               const std::string &sample_key,
                               const HistogramSpec &spec,
                               const SystematicsOptions &options = SystematicsOptions{});

    std::string cache_key(const HistogramSpec &spec,
                          const SystematicsOptions &options);

    void clear_cache();

    std::unique_ptr<TH1D> make_histogram(const HistogramSpec &spec,
                                         const std::vector<double> &bins,
                                         const char *hist_name = "h_systematics",
                                         const char *title = "");

    void build_systematics_cache(EventListIO &eventlist,
                                 DistributionIO &distfile,
                                 const CacheBuildOptions &options);

}

#endif // SYSTEMATICS_HH
