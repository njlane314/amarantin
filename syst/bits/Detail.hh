#ifndef SYST_BITS_DETAIL_HH
#define SYST_BITS_DETAIL_HH

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "Systematics.hh"

class TTree;

namespace syst::detail
{
    constexpr int kSystematicsCacheVersion = 5;

    using CacheEntry = DistributionIO::Spectrum;
    using FamilyCache = DistributionIO::UniverseFamily;

    struct DetectorSourceMatch
    {
        std::string source_label;
        std::string cv_sample_key;
        std::string varied_sample_key;
    };

    struct UniverseAccumulator
    {
        std::string branch_name;
        std::vector<unsigned short> *raw = nullptr;
        std::size_t n_universes = 0;
        std::vector<double> histograms; // row-major: bin-major, universe-minor

        void ensure_size(int nbins);
        void accumulate(int bin, int nbins, double base_weight);
    };

    struct PairedShiftAccumulator
    {
        std::string up_branch_name;
        std::string down_branch_name;
        std::vector<std::string> source_labels;
        std::vector<unsigned short> *raw_up = nullptr;
        std::vector<unsigned short> *raw_down = nullptr;
        std::vector<double> shift_vectors; // row-major: source-major, bin-minor

        void ensure_size(int nbins);
        void accumulate(int bin, int nbins, double base_weight);
    };

    struct ComputedSample
    {
        std::vector<double> nominal;
        std::vector<double> sumw2;
        std::optional<PairedShiftAccumulator> genie_knobs;
        std::optional<UniverseAccumulator> genie;
        std::optional<UniverseAccumulator> flux;
        std::optional<UniverseAccumulator> reint;
    };

    inline HistogramSpec fine_spec_for(const HistogramSpec &spec,
                                       const SystematicsOptions &options)
    {
        HistogramSpec out = spec;
        if (options.cache_nbins > out.nbins)
            out.nbins = options.cache_nbins;
        return out;
    }

    inline std::string encode_options_for_cache(const HistogramSpec &spec,
                                                const SystematicsOptions &options)
    {
        const HistogramSpec fine_spec = fine_spec_for(spec, options);

        std::ostringstream os;
        os << "v=" << kSystematicsCacheVersion
           << ";branch=" << fine_spec.branch_expr
           << ";selection=" << fine_spec.selection_expr
           << ";nbins=" << fine_spec.nbins
           << ";xmin=" << std::setprecision(17) << fine_spec.xmin
           << ";xmax=" << std::setprecision(17) << fine_spec.xmax
           << ";det=" << options.enable_detector
           << ";genie=" << options.enable_genie
           << ";genieknobs=" << options.enable_genie_knobs
           << ";flux=" << options.enable_flux
           << ";reint=" << options.enable_reint
           << ";cov=1"
           << ";hist=" << options.retain_universe_histograms
           << ";modes=" << options.enable_eigenmode_compression
           << ";maxmodes=" << options.max_eigenmodes
           << ";frac=" << std::setprecision(17) << options.eigenmode_fraction;
        for (const auto &sample_key : options.detector_sample_keys)
            os << ";detkey=" << sample_key;
        return os.str();
    }

    inline std::string stable_hash_hex(const std::string &text)
    {
        std::uint64_t hash = 1469598103934665603ULL;
        for (const unsigned char c : text)
        {
            hash ^= static_cast<std::uint64_t>(c);
            hash *= 1099511628211ULL;
        }

        std::ostringstream os;
        os << std::hex << std::setw(16) << std::setfill('0') << hash;
        return os.str();
    }

    inline std::string evaluation_cache_key(const EventListIO &eventlist,
                                            const DistributionIO *distfile,
                                            const std::string &sample_key,
                                            const HistogramSpec &spec,
                                            const SystematicsOptions &options)
    {
        std::ostringstream os;
        os << eventlist.path() << "|"
           << (distfile ? distfile->path() : std::string()) << "|"
           << sample_key << "|"
           << syst::cache_key(spec, options) << "|"
           << spec.nbins << "|"
           << std::setprecision(17) << spec.xmin << "|"
           << std::setprecision(17) << spec.xmax << "|"
           << spec.selection_expr << "|"
           << options.build_full_covariance << "|"
           << options.retain_universe_histograms;
        return os.str();
    }

    ComputedSample compute_sample(TTree *tree,
                                  const HistogramSpec &spec,
                                  const SystematicsOptions &options);

    std::vector<std::string> resolve_detector_sample_keys(EventListIO &eventlist,
                                                          const CacheRequest &request);

    std::vector<DetectorSourceMatch> resolve_detector_source_matches(
        EventListIO &eventlist,
        const std::string &sample_key,
        const std::vector<std::string> &detector_sample_keys);

    std::vector<double> detector_covariance_from_shift_vectors(const std::vector<double> &shift_vectors,
                                                               int source_count,
                                                               int nbins);

    Envelope detector_envelope(const std::vector<double> &nominal,
                               const std::vector<std::vector<double>> &variations);

    Envelope detector_envelope_from_covariance(const std::vector<double> &nominal,
                                               const std::vector<double> &covariance);

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
