#include "Systematics.hh"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "TH1D.h"
#include "TTree.h"

#include "bits/Detail.hh"

namespace
{
    enum class PersistentCacheState
    {
        kEmpty,
        kCompatible,
        kIncompatible
    };

    struct PersistentCacheInspection
    {
        PersistentCacheState state = PersistentCacheState::kEmpty;
        DistributionIO::Metadata metadata;
        bool metadata_present = false;
    };

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

    PersistentCacheInspection inspect_persistent_cache(const EventListIO &eventlist,
                                                       const DistributionIO &distfile)
    {
        PersistentCacheInspection inspection;

        bool has_entries = false;
        try
        {
            has_entries = !distfile.sample_keys().empty();
        }
        catch (...)
        {
            has_entries = false;
        }

        if (!has_entries)
            return inspection;

        try
        {
            inspection.metadata = distfile.metadata();
            inspection.metadata_present = true;
            if (inspection.metadata.eventlist_path == eventlist.path() &&
                inspection.metadata.build_version == syst::detail::kSystematicsCacheVersion)
            {
                inspection.state = PersistentCacheState::kCompatible;
                return inspection;
            }
        }
        catch (...)
        {
        }

        inspection.state = PersistentCacheState::kIncompatible;
        return inspection;
    }

    [[noreturn]] void throw_incompatible_persistent_cache(const EventListIO &eventlist,
                                                          const DistributionIO &distfile,
                                                          const PersistentCacheInspection &inspection)
    {
        std::ostringstream os;
        os << "syst: persistent cache file " << distfile.path()
           << " does not match event list " << eventlist.path();
        if (inspection.metadata_present)
        {
            os << " (found event list " << inspection.metadata.eventlist_path
               << ", build version " << inspection.metadata.build_version
               << "; expected build version " << syst::detail::kSystematicsCacheVersion << ")";
        }
        else
        {
            os << " (missing metadata for a non-empty cache file)";
        }
        os << ". Use a fresh DistributionIO path for this EventListIO.";
        throw std::runtime_error(os.str());
    }

    void write_persistent_cache_metadata(const EventListIO &eventlist,
                                         DistributionIO &distfile)
    {
        DistributionIO::Metadata metadata;
        metadata.eventlist_path = eventlist.path();
        metadata.build_version = syst::detail::kSystematicsCacheVersion;
        distfile.write_metadata(metadata);
    }

    std::vector<double> combine_total_up(const std::vector<double> &nominal,
                                         const syst::Envelope &detector,
                                         const syst::Envelope &genie_knobs,
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
            if (!genie_knobs.empty())
            {
                const double shift = std::max(0.0, genie_knobs.up[bin] - nominal[bin]);
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
                                           const syst::Envelope &genie_knobs,
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
            if (!genie_knobs.empty())
            {
                const double shift = std::max(0.0, nominal[bin] - genie_knobs.down[bin]);
                variance += shift * shift;
            }
            if (bin < genie_sigma.size()) variance += genie_sigma[bin] * genie_sigma[bin];
            if (bin < flux_sigma.size()) variance += flux_sigma[bin] * flux_sigma[bin];
            if (bin < reint_sigma.size()) variance += reint_sigma[bin] * reint_sigma[bin];
            out[bin] = std::max(0.0, nominal[bin] - std::sqrt(variance));
        }
        return out;
    }

    DistributionIO::HistogramSpec target_distribution_spec(
        const DistributionIO::HistogramSpec &source_spec,
        const syst::HistogramSpec &target_spec)
    {
        DistributionIO::HistogramSpec out = source_spec;
        out.nbins = target_spec.nbins;
        out.xmin = target_spec.xmin;
        out.xmax = target_spec.xmax;
        return out;
    }

    bool histogram_compatible(const std::vector<double> &lhs,
                              const std::vector<double> &rhs,
                              double rel_tol)
    {
        if (lhs.size() != rhs.size())
            return false;

        double scale = 1.0;
        double max_diff = 0.0;
        for (std::size_t i = 0; i < lhs.size(); ++i)
        {
            scale = std::max(scale, std::max(std::fabs(lhs[i]), std::fabs(rhs[i])));
            max_diff = std::max(max_diff, std::fabs(lhs[i] - rhs[i]));
        }
        return max_diff <= rel_tol * scale;
    }

    std::vector<std::vector<double>> unpack_source_major_rows(const std::vector<double> &payload,
                                                              int row_count,
                                                              int nbins)
    {
        std::vector<std::vector<double>> out;
        if (payload.empty() || row_count <= 0)
            return out;
        if (payload.size() != static_cast<std::size_t>(row_count * nbins))
            throw std::runtime_error("syst: row-major payload is truncated");

        out.assign(static_cast<std::size_t>(row_count),
                   std::vector<double>(static_cast<std::size_t>(nbins), 0.0));
        for (int row = 0; row < row_count; ++row)
        {
            for (int col = 0; col < nbins; ++col)
            {
                out[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] =
                    payload[static_cast<std::size_t>(row * nbins + col)];
            }
        }
        return out;
    }

    syst::detail::CacheEntry build_cache_entry(EventListIO &eventlist,
                                               const std::string &sample_key,
                                               const syst::HistogramSpec &fine_spec,
                                               const syst::SystematicsOptions &options)
    {
        TTree *nominal_tree = eventlist.selected_tree(sample_key);
        if (!nominal_tree)
            throw std::runtime_error("syst: missing selected tree for sample " + sample_key);

        const syst::detail::ComputedSample nominal_sample =
            syst::detail::compute_sample(nominal_tree, fine_spec, options);

        syst::detail::CacheEntry entry;
        entry.spec.branch_expr = fine_spec.branch_expr;
        entry.spec.selection_expr = fine_spec.selection_expr;
        entry.spec.nbins = fine_spec.nbins;
        entry.spec.xmin = fine_spec.xmin;
        entry.spec.xmax = fine_spec.xmax;
        entry.nominal = nominal_sample.nominal;
        entry.sumw2 = nominal_sample.sumw2;

        syst::Envelope genie_knob_envelope;

        std::vector<std::vector<double>> detector_histograms;
        if (options.enable_detector && !options.detector_sample_keys.empty())
        {
            const std::vector<syst::detail::DetectorSourceMatch> detector_sources =
                syst::detail::resolve_detector_source_matches(eventlist,
                                                              sample_key,
                                                              options.detector_sample_keys);

            detector_histograms.reserve(detector_sources.size());
            entry.detector_source_labels.reserve(detector_sources.size());
            entry.detector_sample_keys.reserve(detector_sources.size());
            entry.detector_shift_vectors.assign(static_cast<std::size_t>(detector_sources.size() * fine_spec.nbins), 0.0);

            for (std::size_t row = 0; row < detector_sources.size(); ++row)
            {
                const auto &source = detector_sources[row];

                TTree *cv_tree = eventlist.selected_tree(source.cv_sample_key);
                if (!cv_tree)
                {
                    throw std::runtime_error("syst: missing detector CV tree for sample " +
                                             source.cv_sample_key);
                }
                TTree *varied_tree = eventlist.selected_tree(source.varied_sample_key);
                if (!varied_tree)
                {
                    throw std::runtime_error("syst: missing detector variation tree for sample " +
                                             source.varied_sample_key);
                }

                const syst::detail::ComputedSample cv_sample =
                    syst::detail::compute_sample(cv_tree, fine_spec, syst::SystematicsOptions{});
                const syst::detail::ComputedSample variation_sample =
                    syst::detail::compute_sample(varied_tree, fine_spec, syst::SystematicsOptions{});

                if (options.validate_detector_cv_compatibility &&
                    source.cv_sample_key != sample_key &&
                    !histogram_compatible(cv_sample.nominal, entry.nominal, 1e-9))
                {
                    throw std::runtime_error(
                        "syst: detector CV sample " + source.cv_sample_key +
                        " is not histogram-compatible with nominal " + sample_key +
                        "; fit-side recentering of detector shifts would be ill-defined");
                }

                entry.detector_source_labels.push_back(source.source_label);
                entry.detector_sample_keys.push_back(source.varied_sample_key);
                detector_histograms.push_back(variation_sample.nominal);

                for (int col = 0; col < fine_spec.nbins; ++col)
                {
                    entry.detector_shift_vectors[static_cast<std::size_t>(row * fine_spec.nbins + col)] =
                        variation_sample.nominal[static_cast<std::size_t>(col)] -
                        cv_sample.nominal[static_cast<std::size_t>(col)];
                }
            }

            entry.detector_source_count = static_cast<int>(detector_sources.size());
            entry.detector_covariance =
                syst::detail::detector_covariance_from_shift_vectors(entry.detector_shift_vectors,
                                                                     entry.detector_source_count,
                                                                     fine_spec.nbins);
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
        }
        if (!entry.detector_covariance.empty())
        {
            const syst::Envelope detector =
                syst::detail::detector_envelope_from_covariance(entry.nominal,
                                                                entry.detector_covariance);
            entry.detector_down = detector.down;
            entry.detector_up = detector.up;
        }
        else if (!detector_histograms.empty())
        {
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
        if (nominal_sample.genie_knobs && !nominal_sample.genie_knobs->shift_vectors.empty())
        {
            entry.genie_knob_source_labels = nominal_sample.genie_knobs->source_labels;
            entry.genie_knob_shift_vectors = nominal_sample.genie_knobs->shift_vectors;
            entry.genie_knob_source_count =
                static_cast<int>(nominal_sample.genie_knobs->source_labels.size());
            entry.genie_knob_covariance =
                syst::detail::detector_covariance_from_shift_vectors(
                    entry.genie_knob_shift_vectors,
                    entry.genie_knob_source_count,
                    fine_spec.nbins);
            genie_knob_envelope =
                syst::detail::detector_envelope_from_covariance(entry.nominal,
                                                                entry.genie_knob_covariance);
        }

        entry.total_up = combine_total_up(entry.nominal,
                                          syst::Envelope{entry.detector_down, entry.detector_up},
                                          genie_knob_envelope,
                                          entry.genie.sigma,
                                          entry.flux.sigma,
                                          entry.reint.sigma);
        entry.total_down = combine_total_down(entry.nominal,
                                              syst::Envelope{entry.detector_down, entry.detector_up},
                                              genie_knob_envelope,
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
        const DistributionIO::HistogramSpec target_dist_spec =
            target_distribution_spec(entry.spec, target_spec);
        result.nominal = entry.rebinned_values(entry.nominal, target_dist_spec);
        result.genie_knob_source_labels = entry.genie_knob_source_labels;
        result.genie_knob_source_count = entry.genie_knob_source_count;

        if (entry.detector_source_count > 0 && !entry.detector_shift_vectors.empty())
        {
            const std::vector<double> rebinned_shift_vectors =
                entry.rebinned_source_major_payload(entry.detector_shift_vectors,
                                                    entry.detector_source_count,
                                                    target_dist_spec);
            const std::vector<double> rebinned_covariance =
                syst::detail::detector_covariance_from_shift_vectors(rebinned_shift_vectors,
                                                                     entry.detector_source_count,
                                                                     target_spec.nbins);
            result.detector =
                syst::detail::detector_envelope_from_covariance(result.nominal,
                                                                rebinned_covariance);
        }
        else if (!entry.detector_covariance.empty())
        {
            const std::vector<double> rebinned_covariance =
                entry.rebinned_covariance(entry.detector_covariance, target_dist_spec);
            result.detector =
                syst::detail::detector_envelope_from_covariance(result.nominal,
                                                                rebinned_covariance);
        }
        else if (entry.detector_template_count > 0 && !entry.detector_templates.empty())
        {
            const std::vector<std::vector<double>> detector_histograms =
                unpack_source_major_rows(
                    entry.rebinned_source_major_payload(entry.detector_templates,
                                                        entry.detector_template_count,
                                                        target_dist_spec),
                    entry.detector_template_count,
                    target_spec.nbins);
            result.detector = syst::detail::detector_envelope(result.nominal, detector_histograms);
        }
        else if (!entry.detector_down.empty() && !entry.detector_up.empty())
        {
            result.detector.down = entry.rebinned_values(entry.detector_down, target_dist_spec);
            result.detector.up = entry.rebinned_values(entry.detector_up, target_dist_spec);
        }

        if (entry.genie_knob_source_count > 0 && !entry.genie_knob_shift_vectors.empty())
        {
            const std::vector<double> rebinned_shift_vectors =
                entry.rebinned_source_major_payload(entry.genie_knob_shift_vectors,
                                                    entry.genie_knob_source_count,
                                                    target_dist_spec);
            const std::vector<double> rebinned_covariance =
                syst::detail::detector_covariance_from_shift_vectors(
                    rebinned_shift_vectors,
                    entry.genie_knob_source_count,
                    target_spec.nbins);
            result.genie_knobs =
                syst::detail::detector_envelope_from_covariance(result.nominal,
                                                                rebinned_covariance);
        }
        else if (!entry.genie_knob_covariance.empty())
        {
            const std::vector<double> rebinned_covariance =
                entry.rebinned_covariance(entry.genie_knob_covariance, target_dist_spec);
            result.genie_knobs =
                syst::detail::detector_envelope_from_covariance(result.nominal,
                                                                rebinned_covariance);
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
                                           result.genie_knobs,
                                           result.genie ? result.genie->sigma : empty,
                                           result.flux ? result.flux->sigma : empty,
                                           result.reint ? result.reint->sigma : empty);
        result.total_down = combine_total_down(result.nominal,
                                               result.detector,
                                               result.genie_knobs,
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
                "syst: DistributionIO is required for persistent cache policies");
        }
        if (sample_key.empty())
            throw std::runtime_error("syst: sample_key is required");

        const std::string eval_key = detail::evaluation_cache_key(eventlist, nullptr, sample_key, spec, options);
        if (options.enable_memory_cache)
        {
            std::lock_guard<std::mutex> lock(memory_cache_mutex());
            const auto it = memory_cache_store().find(eval_key);
            if (it != memory_cache_store().end())
                return it->second;
        }

        const HistogramSpec fine_spec = detail::fine_spec_for(spec, options);
        detail::CacheEntry entry = build_cache_entry(eventlist, sample_key, fine_spec, options);
        entry.spec.sample_key = sample_key;
        entry.spec.cache_key = cache_key(spec, options);

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
            throw std::runtime_error("syst: sample_key is required");

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
        const PersistentCacheInspection cache_inspection =
            use_persistent_cache ? inspect_persistent_cache(eventlist, distfile)
                                 : PersistentCacheInspection{};

        if (cache_inspection.state == PersistentCacheState::kIncompatible)
        {
            throw_incompatible_persistent_cache(eventlist, distfile, cache_inspection);
        }

        if (use_persistent_cache &&
            cache_inspection.state == PersistentCacheState::kCompatible &&
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
                throw std::runtime_error("syst: persistent cache miss for sample " +
                                         sample_key + " key " + persistent_key);
            }

            entry = build_cache_entry(eventlist, sample_key, fine_spec, options);
            entry.spec.sample_key = sample_key;
            entry.spec.cache_key = persistent_key;

            if (use_persistent_cache && can_write_persistent_cache)
            {
                if (cache_inspection.state == PersistentCacheState::kEmpty)
                    write_persistent_cache_metadata(eventlist, distfile);
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
            throw std::runtime_error("syst: nbins must be positive");

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

        const PersistentCacheInspection cache_inspection = inspect_persistent_cache(eventlist, distfile);
        if (cache_inspection.state == PersistentCacheState::kIncompatible)
            throw_incompatible_persistent_cache(eventlist, distfile, cache_inspection);
        if (cache_inspection.state == PersistentCacheState::kEmpty)
            write_persistent_cache_metadata(eventlist, distfile);

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
            sysopt.enable_genie_knobs = options.enable_genie_knobs;
            sysopt.enable_flux = options.enable_flux;
            sysopt.enable_reint = options.enable_reint;
            sysopt.build_full_covariance = options.build_full_covariance;
            sysopt.retain_universe_histograms = options.retain_universe_histograms;
            sysopt.enable_eigenmode_compression = options.enable_eigenmode_compression;
            sysopt.max_eigenmodes = options.max_eigenmodes;
            sysopt.eigenmode_fraction = options.eigenmode_fraction;
            sysopt.validate_detector_cv_compatibility =
                options.validate_detector_cv_compatibility;

            (void)evaluate(eventlist,
                           distfile,
                           request.sample_key,
                           spec,
                           sysopt);
        }

        distfile.flush();
    }

}
