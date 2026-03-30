#include "SystematicsCacheBuilder.hh"

#include <algorithm>
#include <stdexcept>

#include "SampleBook.hh"
#include "Systematics.hh"

namespace syst
{
    namespace
    {
        std::vector<std::string> resolve_detector_sample_keys(EventListIO &eventlist,
                                                              const CacheRequest &request)
        {
            if (!request.detector_sample_keys.empty())
                return request.detector_sample_keys;

            const SampleBook book = SampleBook::from_event_list(eventlist);
            if (!book.has(request.sample_key))
                return {};
            return book.detector_mates(request.sample_key);
        }
    }

    void SystematicsCacheBuilder::build(EventListIO &eventlist,
                                        const CacheBuildOptions &options)
    {
        if (!options.active())
            return;
        if (eventlist.mode() == EventListIO::Mode::kRead)
            throw std::runtime_error("SystematicsCacheBuilder: event list must be writable");

        for (const auto &request : options.requests)
        {
            if (request.sample_key.empty())
                throw std::runtime_error("SystematicsCacheBuilder: request sample_key must not be empty");
            if (request.branch_expr.empty())
                throw std::runtime_error("SystematicsCacheBuilder: request branch_expr must not be empty");
            if (request.nbins <= 0)
                throw std::runtime_error("SystematicsCacheBuilder: request nbins must be positive");
            if (!(request.xmax > request.xmin))
                throw std::runtime_error("SystematicsCacheBuilder: request range is invalid");

            HistogramSpec spec;
            spec.branch_expr = request.branch_expr;
            spec.nbins = request.nbins;
            spec.xmin = request.xmin;
            spec.xmax = request.xmax;
            spec.selection_expr = request.selection_expr;

            const std::vector<std::string> detector_sample_keys =
                resolve_detector_sample_keys(eventlist, request);

            SystematicsOptions sysopt;
            sysopt.enable_memory_cache = false;
            sysopt.persistent_cache =
                options.overwrite_existing ? CachePolicy::kRebuild
                                           : CachePolicy::kComputeIfMissing;
            sysopt.cache_nbins = std::max(request.nbins, options.cache_nbins);
            sysopt.enable_detector = !detector_sample_keys.empty();
            sysopt.detector_sample_keys = detector_sample_keys;
            sysopt.enable_genie = options.enable_genie;
            sysopt.enable_flux = options.enable_flux;
            sysopt.enable_reint = options.enable_reint;
            sysopt.build_full_covariance = options.build_full_covariance;
            sysopt.retain_universe_histograms = options.retain_universe_histograms;
            sysopt.enable_eigenmode_compression = options.enable_eigenmode_compression;
            sysopt.persist_covariance = options.persist_covariance;
            sysopt.max_eigenmodes = options.max_eigenmodes;
            sysopt.eigenmode_fraction = options.eigenmode_fraction;

            (void)SystematicsEngine::evaluate(eventlist,
                                             request.sample_key,
                                             spec,
                                             sysopt);
        }

        eventlist.flush();
    }
}
