#ifndef SYSTEMATICS_CACHE_BUILDER_HH
#define SYSTEMATICS_CACHE_BUILDER_HH

#include <string>
#include <vector>

#include "EventListIO.hh"

namespace syst
{
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
        bool enable_flux = false;
        bool enable_reint = false;

        bool build_full_covariance = false;
        bool retain_universe_histograms = false;
        bool enable_eigenmode_compression = true;
        bool persist_covariance = true;
        int max_eigenmodes = 8;
        double eigenmode_fraction = 0.99;

        std::vector<CacheRequest> requests;

        bool active() const
        {
            return !requests.empty();
        }
    };

    void build_systematics_cache(EventListIO &eventlist,
                                 const CacheBuildOptions &options);
}

#endif // SYSTEMATICS_CACHE_BUILDER_HH
