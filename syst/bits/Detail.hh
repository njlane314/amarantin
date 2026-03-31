#ifndef SYST_BITS_DETAIL_HH
#define SYST_BITS_DETAIL_HH

#include <cstddef>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include "Systematics.hh"

class TTree;

namespace syst::detail
{
    constexpr const char *kCentralWeightBranch = "__w__";
    constexpr int kSystematicsCacheVersion = 2;

    using CacheEntry = DistributionIO::Spectrum;
    using FamilyCache = DistributionIO::Family;
    using MatrixRowMajor = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

    struct UniverseAccumulator
    {
        std::string branch_name;
        std::vector<unsigned short> *raw = nullptr;
        std::size_t n_universes = 0;
        std::vector<double> histograms; // row-major: bin-major, universe-minor

        void ensure_size(int nbins);
        void accumulate(int bin, int nbins, double base_weight);
    };

    struct SampleComputation
    {
        std::vector<double> nominal;
        std::vector<double> sumw2;
        std::optional<UniverseAccumulator> genie;
        std::optional<UniverseAccumulator> flux;
        std::optional<UniverseAccumulator> reint;
    };

    HistogramSpec fine_spec_for(const HistogramSpec &spec,
                                const SystematicsOptions &options);

    std::string encode_options_for_cache(const HistogramSpec &spec,
                                         const SystematicsOptions &options);

    std::string stable_hash_hex(const std::string &text);

    std::string evaluation_cache_key(const EventListIO &eventlist,
                                     const DistributionIO *distfile,
                                     const std::string &sample_key,
                                     const HistogramSpec &spec,
                                     const SystematicsOptions &options);

    SampleComputation compute_sample(TTree *tree,
                                     const HistogramSpec &spec,
                                     const SystematicsOptions &options);

    MatrixRowMajor build_rebin_matrix(int source_nbins,
                                      double source_xmin,
                                      double source_xmax,
                                      int target_nbins,
                                      double target_xmin,
                                      double target_xmax);

    std::vector<double> rebin_vector(const std::vector<double> &source,
                                     int source_nbins,
                                     double source_xmin,
                                     double source_xmax,
                                     const HistogramSpec &target_spec);

    std::vector<std::string> resolve_detector_sample_keys(EventListIO &eventlist,
                                                          const CacheRequest &request);

    std::vector<std::vector<double>> rebin_detector_templates(const CacheEntry &entry,
                                                              const HistogramSpec &target_spec);

    Envelope detector_envelope(const std::vector<double> &nominal,
                               const std::vector<std::vector<double>> &variations);

    FamilyCache build_family_cache(const UniverseAccumulator &family,
                                   const std::vector<double> &nominal,
                                   int nbins,
                                   const SystematicsOptions &options);

    UniverseFamilyResult family_result_from_cache(const FamilyCache &family,
                                                  const CacheEntry &entry,
                                                  const HistogramSpec &target_spec,
                                                  bool build_full_covariance,
                                                  bool retain_universe_histograms);
}

#endif // SYST_BITS_DETAIL_HH
