#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "EventListIO.hh"
#include "Systematics.hh"
#include "SystematicsCacheBuilder.hh"

void cache_systematics(const char *read_path = nullptr,
                       const char *sample_key = nullptr,
                       const char *branch_expr = nullptr,
                       int nbins = 100,
                       double xmin = 0.0,
                       double xmax = 1.0,
                       const char *selection_expr = "",
                       int cache_nbins = 0,
                       const char *detector_samples_csv = "",
                       bool enable_genie = true,
                       bool enable_flux = false,
                       bool enable_reint = false)
{
    macro_utils::run_macro("cache_systematics", [&]() {
        if (!read_path || !*read_path)
            throw std::runtime_error("read_path is required");
        if (!sample_key || !*sample_key)
            throw std::runtime_error("sample_key is required");
        if (!branch_expr || !*branch_expr)
            throw std::runtime_error("branch_expr is required");
        if (nbins <= 0)
            throw std::runtime_error("nbins must be positive");
        if (!(xmax > xmin))
            throw std::runtime_error("xmax must be greater than xmin");

        auto split_csv = [](const std::string &csv) {
            std::vector<std::string> out;
            std::string current;
            for (char c : csv)
            {
                if (c == ',')
                {
                    if (!current.empty())
                    {
                        out.push_back(current);
                        current.clear();
                    }
                    continue;
                }
                if (!std::isspace(static_cast<unsigned char>(c)))
                    current.push_back(c);
            }
            if (!current.empty())
                out.push_back(current);
            return out;
        };

        EventListIO eventlist(read_path, EventListIO::Mode::kUpdate);

        syst::CacheBuildOptions cache_options;
        cache_options.overwrite_existing = true;
        cache_options.cache_nbins = std::max(nbins, cache_nbins);
        cache_options.enable_genie = enable_genie;
        cache_options.enable_flux = enable_flux;
        cache_options.enable_reint = enable_reint;

        syst::CacheRequest request;
        request.sample_key = sample_key;
        request.branch_expr = branch_expr;
        request.nbins = nbins;
        request.xmin = xmin;
        request.xmax = xmax;
        request.selection_expr = (selection_expr && *selection_expr) ? selection_expr : "";
        request.detector_sample_keys =
            (detector_samples_csv && *detector_samples_csv) ? split_csv(detector_samples_csv)
                                                            : std::vector<std::string>{};
        cache_options.requests.push_back(request);

        syst::SystematicsCacheBuilder::build(eventlist, cache_options);

        plot_utils::HistogramSpec spec;
        spec.branch_expr = request.branch_expr;
        spec.nbins = request.nbins;
        spec.xmin = request.xmin;
        spec.xmax = request.xmax;
        spec.selection_expr = request.selection_expr;

        plot_utils::SystematicsOptions readback_options;
        readback_options.enable_memory_cache = false;
        readback_options.persistent_cache = plot_utils::CachePolicy::kLoadOnly;
        readback_options.cache_nbins = cache_options.cache_nbins;
        readback_options.enable_detector = !request.detector_sample_keys.empty();
        readback_options.detector_sample_keys = request.detector_sample_keys;
        readback_options.enable_genie = cache_options.enable_genie;
        readback_options.enable_flux = cache_options.enable_flux;
        readback_options.enable_reint = cache_options.enable_reint;

        const auto result = plot_utils::SystematicsEngine::evaluate(eventlist, sample_key, spec, readback_options);
        std::cout << "cache_key=" << result.cache_key
                  << " cached_nbins=" << result.cached_nbins
                  << " loaded_from_persistent_cache=" << (result.loaded_from_persistent_cache ? 1 : 0)
                  << "\n";
    });
}
