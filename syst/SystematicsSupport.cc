#include "bits/SystematicsInternal.hh"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>

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
           << ";flux=" << options.enable_flux
           << ";reint=" << options.enable_reint
           << ";cov=" << options.persist_covariance
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

    MatrixRowMajor build_rebin_matrix(int source_nbins,
                                      double source_xmin,
                                      double source_xmax,
                                      int target_nbins,
                                      double target_xmin,
                                      double target_xmax)
    {
        MatrixRowMajor rebin = MatrixRowMajor::Zero(target_nbins, source_nbins);
        const double source_width = (source_xmax - source_xmin) / static_cast<double>(source_nbins);
        const double target_width = (target_xmax - target_xmin) / static_cast<double>(target_nbins);
        if (source_width <= 0.0 || target_width <= 0.0)
            throw std::runtime_error("SystematicsEngine: invalid rebinning range");

        for (int target_bin = 0; target_bin < target_nbins; ++target_bin)
        {
            const double target_low = target_xmin + target_width * static_cast<double>(target_bin);
            const double target_high = target_low + target_width;

            for (int source_bin = 0; source_bin < source_nbins; ++source_bin)
            {
                const double source_low = source_xmin + source_width * static_cast<double>(source_bin);
                const double source_high = source_low + source_width;
                const double overlap = std::max(0.0, std::min(source_high, target_high) - std::max(source_low, target_low));
                if (overlap > 0.0)
                    rebin(target_bin, source_bin) = overlap / source_width;
            }
        }
        return rebin;
    }

    std::vector<double> rebin_vector(const std::vector<double> &source,
                                     int source_nbins,
                                     double source_xmin,
                                     double source_xmax,
                                     const HistogramSpec &target_spec)
    {
        if (source.empty())
            return std::vector<double>{};

        const MatrixRowMajor rebin = build_rebin_matrix(source_nbins,
                                                        source_xmin,
                                                        source_xmax,
                                                        target_spec.nbins,
                                                        target_spec.xmin,
                                                        target_spec.xmax);
        const Eigen::Map<const Eigen::VectorXd> source_vec(source.data(), source_nbins);
        const Eigen::VectorXd target = rebin * source_vec;
        return std::vector<double>(target.data(), target.data() + target.size());
    }
}
