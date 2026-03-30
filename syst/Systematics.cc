#include "Systematics.hh"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "TH1D.h"
#include "TTree.h"

#include "bits/SystematicsInternal.hh"

namespace
{
    std::mutex &memory_cache_mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::unordered_map<std::string, syst::SystematicsResult> &memory_cache_store()
    {
        static std::unordered_map<std::string, syst::SystematicsResult> cache;
        return cache;
    }

    std::vector<double> combine_total_up(const std::vector<double> &nominal,
                                         const syst::Envelope &detector,
                                         const std::vector<double> &genie_sigma,
                                         const std::vector<double> &flux_sigma,
                                         const std::vector<double> &reint_sigma)
    {
        std::vector<double> out = nominal;
        for (std::size_t bin = 0; bin < nominal.size(); ++bin)
        {
            double variance = 0.0;
            if (!detector.empty())
            {
                const double shift = std::max(0.0, detector.up[bin] - nominal[bin]);
                variance += shift * shift;
            }
            if (bin < genie_sigma.size()) variance += genie_sigma[bin] * genie_sigma[bin];
            if (bin < flux_sigma.size()) variance += flux_sigma[bin] * flux_sigma[bin];
            if (bin < reint_sigma.size()) variance += reint_sigma[bin] * reint_sigma[bin];
            out[bin] = nominal[bin] + std::sqrt(variance);
        }
        return out;
    }

    std::vector<double> combine_total_down(const std::vector<double> &nominal,
                                           const syst::Envelope &detector,
                                           const std::vector<double> &genie_sigma,
                                           const std::vector<double> &flux_sigma,
                                           const std::vector<double> &reint_sigma)
    {
        std::vector<double> out = nominal;
        for (std::size_t bin = 0; bin < nominal.size(); ++bin)
        {
            double variance = 0.0;
            if (!detector.empty())
            {
                const double shift = std::max(0.0, nominal[bin] - detector.down[bin]);
                variance += shift * shift;
            }
            if (bin < genie_sigma.size()) variance += genie_sigma[bin] * genie_sigma[bin];
            if (bin < flux_sigma.size()) variance += flux_sigma[bin] * flux_sigma[bin];
            if (bin < reint_sigma.size()) variance += reint_sigma[bin] * reint_sigma[bin];
            out[bin] = std::max(0.0, nominal[bin] - std::sqrt(variance));
        }
        return out;
    }

    syst::detail::CacheEntry build_cache_entry(TTree *nominal_tree,
                                               const syst::HistogramSpec &fine_spec,
                                               const syst::SystematicsOptions &options,
                                               const std::vector<TTree *> &detector_trees)
    {
        const syst::detail::SampleComputation nominal_sample =
            syst::detail::compute_sample(nominal_tree, fine_spec, options);

        syst::detail::CacheEntry entry;
        entry.spec.branch_expr = fine_spec.branch_expr;
        entry.spec.selection_expr = fine_spec.selection_expr;
        entry.spec.nbins = fine_spec.nbins;
        entry.spec.xmin = fine_spec.xmin;
        entry.spec.xmax = fine_spec.xmax;
        entry.nominal = nominal_sample.nominal;
        entry.sumw2 = nominal_sample.sumw2;

        std::vector<std::vector<double>> detector_histograms;
        detector_histograms.reserve(detector_trees.size());
        for (TTree *tree : detector_trees)
        {
            const syst::detail::SampleComputation variation =
                syst::detail::compute_sample(tree, fine_spec, syst::SystematicsOptions{});
            detector_histograms.push_back(variation.nominal);
        }
        if (!detector_histograms.empty())
        {
            entry.detector_template_count = static_cast<int>(detector_histograms.size());
            entry.detector_templates.assign(static_cast<std::size_t>(entry.detector_template_count * fine_spec.nbins), 0.0);
            for (int row = 0; row < entry.detector_template_count; ++row)
            {
                for (int col = 0; col < fine_spec.nbins; ++col)
                {
                    entry.detector_templates[static_cast<std::size_t>(row * fine_spec.nbins + col)] =
                        detector_histograms[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
                }
            }
            const syst::Envelope detector =
                syst::detail::detector_envelope(entry.nominal, detector_histograms);
            entry.detector_down = detector.down;
            entry.detector_up = detector.up;
        }

        if (nominal_sample.genie)
            entry.genie = syst::detail::build_family_cache(*nominal_sample.genie, entry.nominal, fine_spec.nbins, options);
        if (nominal_sample.flux)
            entry.flux = syst::detail::build_family_cache(*nominal_sample.flux, entry.nominal, fine_spec.nbins, options);
        if (nominal_sample.reint)
            entry.reint = syst::detail::build_family_cache(*nominal_sample.reint, entry.nominal, fine_spec.nbins, options);

        entry.total_up = combine_total_up(entry.nominal,
                                          syst::Envelope{entry.detector_down, entry.detector_up},
                                          entry.genie.sigma,
                                          entry.flux.sigma,
                                          entry.reint.sigma);
        entry.total_down = combine_total_down(entry.nominal,
                                              syst::Envelope{entry.detector_down, entry.detector_up},
                                              entry.genie.sigma,
                                              entry.flux.sigma,
                                              entry.reint.sigma);
        return entry;
    }

    syst::SystematicsResult result_from_cache(const syst::detail::CacheEntry &entry,
                                              const std::string &cache_key,
                                              const syst::HistogramSpec &target_spec,
                                              const syst::SystematicsOptions &options,
                                              bool loaded_from_persistent_cache)
    {
        syst::SystematicsResult result;
        result.cache_key = cache_key;
        result.cached_nbins = entry.spec.nbins;
        result.loaded_from_persistent_cache = loaded_from_persistent_cache;
        result.nominal = syst::detail::rebin_vector(entry.nominal,
                                                    entry.spec.nbins,
                                                    entry.spec.xmin,
                                                    entry.spec.xmax,
                                                    target_spec);

        if (entry.detector_template_count > 0 && !entry.detector_templates.empty())
        {
            const std::vector<std::vector<double>> detector_histograms =
                syst::detail::rebin_detector_templates(entry, target_spec);
            result.detector = syst::detail::detector_envelope(result.nominal, detector_histograms);
        }
        else if (!entry.detector_down.empty() && !entry.detector_up.empty())
        {
            result.detector.down = syst::detail::rebin_vector(entry.detector_down,
                                                              entry.spec.nbins,
                                                              entry.spec.xmin,
                                                              entry.spec.xmax,
                                                              target_spec);
            result.detector.up = syst::detail::rebin_vector(entry.detector_up,
                                                            entry.spec.nbins,
                                                            entry.spec.xmin,
                                                            entry.spec.xmax,
                                                            target_spec);
        }

        if (!entry.genie.empty())
        {
            result.genie = syst::detail::family_result_from_cache(entry.genie,
                                                                  entry,
                                                                  target_spec,
                                                                  options.build_full_covariance,
                                                                  options.retain_universe_histograms);
        }
        if (!entry.flux.empty())
        {
            result.flux = syst::detail::family_result_from_cache(entry.flux,
                                                                 entry,
                                                                 target_spec,
                                                                 options.build_full_covariance,
                                                                 options.retain_universe_histograms);
        }
        if (!entry.reint.empty())
        {
            result.reint = syst::detail::family_result_from_cache(entry.reint,
                                                                  entry,
                                                                  target_spec,
                                                                  options.build_full_covariance,
                                                                  options.retain_universe_histograms);
        }

        const std::vector<double> empty;
        result.total_up = combine_total_up(result.nominal,
                                           result.detector,
                                           result.genie ? result.genie->sigma : empty,
                                           result.flux ? result.flux->sigma : empty,
                                           result.reint ? result.reint->sigma : empty);
        result.total_down = combine_total_down(result.nominal,
                                               result.detector,
                                               result.genie ? result.genie->sigma : empty,
                                               result.flux ? result.flux->sigma : empty,
                                               result.reint ? result.reint->sigma : empty);
        return result;
    }
}

namespace syst
{
    std::string cache_key(const HistogramSpec &spec,
                          const SystematicsOptions &options)
    {
        return detail::stable_hash_hex(detail::encode_options_for_cache(spec, options));
    }

    SystematicsResult evaluate(EventListIO &eventlist,
                               const std::string &sample_key,
                               const HistogramSpec &spec,
                               const SystematicsOptions &options)
    {
        if (options.persistent_cache != CachePolicy::kMemoryOnly)
        {
            throw std::runtime_error(
                "SystematicsEngine: DistributionIO is required for persistent cache policies");
        }
        if (sample_key.empty())
            throw std::runtime_error("SystematicsEngine: sample_key is required");

        const std::string eval_key = detail::evaluation_cache_key(eventlist, nullptr, sample_key, spec, options);
        if (options.enable_memory_cache)
        {
            std::lock_guard<std::mutex> lock(memory_cache_mutex());
            const auto it = memory_cache_store().find(eval_key);
            if (it != memory_cache_store().end())
                return it->second;
        }

        const HistogramSpec fine_spec = detail::fine_spec_for(spec, options);
        TTree *nominal_tree = eventlist.selected_tree(sample_key);
        if (!nominal_tree)
            throw std::runtime_error("SystematicsEngine: missing selected tree for sample " + sample_key);

        std::vector<TTree *> detector_trees;
        detector_trees.reserve(options.detector_sample_keys.size());
        for (const auto &detector_sample_key : options.detector_sample_keys)
        {
            if (detector_sample_key.empty())
                continue;
            detector_trees.push_back(eventlist.selected_tree(detector_sample_key));
        }

        detail::CacheEntry entry = build_cache_entry(nominal_tree, fine_spec, options, detector_trees);
        entry.spec.sample_key = sample_key;
        entry.spec.cache_key = cache_key(spec, options);
        entry.detector_sample_keys = options.detector_sample_keys;

        SystematicsResult result = result_from_cache(entry,
                                                     entry.spec.cache_key,
                                                     spec,
                                                     options,
                                                     false);

        if (options.enable_memory_cache)
        {
            std::lock_guard<std::mutex> lock(memory_cache_mutex());
            memory_cache_store()[eval_key] = result;
        }

        return result;
    }

    SystematicsResult evaluate(EventListIO &eventlist,
                               DistributionIO &distfile,
                               const std::string &sample_key,
                               const HistogramSpec &spec,
                               const SystematicsOptions &options)
    {
        if (sample_key.empty())
            throw std::runtime_error("SystematicsEngine: sample_key is required");

        const std::string eval_key = detail::evaluation_cache_key(eventlist, &distfile, sample_key, spec, options);
        if (options.enable_memory_cache)
        {
            std::lock_guard<std::mutex> lock(memory_cache_mutex());
            const auto it = memory_cache_store().find(eval_key);
            if (it != memory_cache_store().end())
                return it->second;
        }

        const HistogramSpec fine_spec = detail::fine_spec_for(spec, options);
        const std::string persistent_key = cache_key(spec, options);

        bool loaded_from_persistent_cache = false;
        detail::CacheEntry entry;

        const bool use_persistent_cache = options.persistent_cache != CachePolicy::kMemoryOnly;
        const bool can_write_persistent_cache = distfile.mode() != DistributionIO::Mode::kRead;

        if (use_persistent_cache && can_write_persistent_cache)
        {
            DistributionIO::Metadata metadata;
            metadata.eventlist_path = eventlist.path();
            metadata.build_version = detail::kSystematicsCacheVersion;
            distfile.write_metadata(metadata);
        }

        if (use_persistent_cache &&
            options.persistent_cache != CachePolicy::kRebuild &&
            distfile.has(sample_key, persistent_key))
        {
            entry = distfile.read(sample_key, persistent_key);
            loaded_from_persistent_cache = true;
        }
        else
        {
            if (use_persistent_cache && options.persistent_cache == CachePolicy::kLoadOnly)
            {
                throw std::runtime_error("SystematicsEngine: persistent cache miss for sample " +
                                         sample_key + " key " + persistent_key);
            }

            TTree *nominal_tree = eventlist.selected_tree(sample_key);
            if (!nominal_tree)
                throw std::runtime_error("SystematicsEngine: missing selected tree for sample " + sample_key);

            std::vector<TTree *> detector_trees;
            detector_trees.reserve(options.detector_sample_keys.size());
            for (const auto &detector_sample_key : options.detector_sample_keys)
            {
                if (detector_sample_key.empty())
                    continue;
                detector_trees.push_back(eventlist.selected_tree(detector_sample_key));
            }

            entry = build_cache_entry(nominal_tree, fine_spec, options, detector_trees);
            entry.spec.sample_key = sample_key;
            entry.spec.cache_key = persistent_key;
            entry.detector_sample_keys = options.detector_sample_keys;

            if (use_persistent_cache && can_write_persistent_cache)
            {
                distfile.write(sample_key, persistent_key, entry);
                distfile.flush();
            }
        }

        SystematicsResult result = result_from_cache(entry,
                                                     persistent_key,
                                                     spec,
                                                     options,
                                                     loaded_from_persistent_cache);

        if (options.enable_memory_cache)
        {
            std::lock_guard<std::mutex> lock(memory_cache_mutex());
            memory_cache_store()[eval_key] = result;
        }

        return result;
    }

    void clear_cache()
    {
        std::lock_guard<std::mutex> lock(memory_cache_mutex());
        memory_cache_store().clear();
    }

    std::unique_ptr<TH1D> make_histogram(const HistogramSpec &spec,
                                         const std::vector<double> &bins,
                                         const char *hist_name,
                                         const char *title)
    {
        if (spec.nbins <= 0)
            throw std::runtime_error("SystematicsEngine: nbins must be positive");

        std::unique_ptr<TH1D> hist(new TH1D(hist_name,
                                            (title && *title) ? title : spec.branch_expr.c_str(),
                                            spec.nbins,
                                            spec.xmin,
                                            spec.xmax));
        hist->SetDirectory(nullptr);
        hist->Sumw2(false);

        const std::size_t n = std::min<std::size_t>(bins.size(), static_cast<std::size_t>(spec.nbins));
        for (std::size_t bin = 0; bin < n; ++bin)
            hist->SetBinContent(static_cast<int>(bin + 1), bins[bin]);

        return hist;
    }

    void build_systematics_cache(EventListIO &eventlist,
                                 DistributionIO &distfile,
                                 const CacheBuildOptions &options)
    {
        if (!options.active())
            return;
        if (distfile.mode() == DistributionIO::Mode::kRead)
            throw std::runtime_error("syst::build_systematics_cache: distribution file must be writable");

        DistributionIO::Metadata metadata;
        metadata.eventlist_path = eventlist.path();
        metadata.build_version = detail::kSystematicsCacheVersion;
        distfile.write_metadata(metadata);

        for (const auto &request : options.requests)
        {
            if (request.sample_key.empty())
                throw std::runtime_error("syst::build_systematics_cache: request sample_key must not be empty");
            if (request.branch_expr.empty())
                throw std::runtime_error("syst::build_systematics_cache: request branch_expr must not be empty");
            if (request.nbins <= 0)
                throw std::runtime_error("syst::build_systematics_cache: request nbins must be positive");
            if (!(request.xmax > request.xmin))
                throw std::runtime_error("syst::build_systematics_cache: request range is invalid");

            HistogramSpec spec;
            spec.branch_expr = request.branch_expr;
            spec.nbins = request.nbins;
            spec.xmin = request.xmin;
            spec.xmax = request.xmax;
            spec.selection_expr = request.selection_expr;

            const std::vector<std::string> detector_sample_keys =
                detail::resolve_detector_sample_keys(eventlist, request);

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

            (void)evaluate(eventlist,
                           distfile,
                           request.sample_key,
                           spec,
                           sysopt);
        }

        distfile.flush();
    }

    SystematicsResult SystematicsEngine::evaluate(EventListIO &eventlist,
                                                  const std::string &sample_key,
                                                  const HistogramSpec &spec,
                                                  const SystematicsOptions &options)
    {
        return syst::evaluate(eventlist, sample_key, spec, options);
    }

    SystematicsResult SystematicsEngine::evaluate(EventListIO &eventlist,
                                                  DistributionIO &distfile,
                                                  const std::string &sample_key,
                                                  const HistogramSpec &spec,
                                                  const SystematicsOptions &options)
    {
        return syst::evaluate(eventlist, distfile, sample_key, spec, options);
    }

    std::string SystematicsEngine::cache_key(const HistogramSpec &spec,
                                             const SystematicsOptions &options)
    {
        return syst::cache_key(spec, options);
    }

    void SystematicsEngine::clear_cache()
    {
        syst::clear_cache();
    }

    std::unique_ptr<TH1D> SystematicsEngine::make_histogram(const HistogramSpec &spec,
                                                            const std::vector<double> &bins,
                                                            const char *hist_name,
                                                            const char *title)
    {
        return syst::make_histogram(spec, bins, hist_name, title);
    }
}
