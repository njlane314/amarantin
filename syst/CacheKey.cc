#include "bits/Detail.hh"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace syst::detail
{
    HistogramSpec fine_spec_for(const HistogramSpec &spec,
                                const SystematicsOptions &options)
    {
        HistogramSpec out = spec;
        if (options.cache_nbins > out.nbins)
            out.nbins = options.cache_nbins;
        return out;
    }

    std::string encode_options_for_cache(const HistogramSpec &spec,
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

    std::string stable_hash_hex(const std::string &text)
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

    std::string evaluation_cache_key(const EventListIO &eventlist,
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
}
