#include "Systematics.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SampleDef.hh"
#include "TH1D.h"
#include "TTree.h"
#include "TTreeFormula.h"

#if defined(AMARANTIN_HAVE_EIGEN)
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#endif

namespace
{
    constexpr const char *kCentralWeightBranch = "__w__";
    constexpr int kSystematicsCacheVersion = 1;

    using CacheEntry = EventListIO::SystematicsCacheEntry;
    using FamilyCache = EventListIO::SystematicsFamilyCache;

    double sanitise_universe_weight(double weight)
    {
        if (!std::isfinite(weight) || weight <= 0.0)
            return 1.0;
        return weight;
    }

    double decode_universe_weight(unsigned short raw_weight)
    {
        return sanitise_universe_weight(static_cast<double>(raw_weight) / 1000.0);
    }

    syst::HistogramSpec fine_spec_for(const syst::HistogramSpec &spec,
                                      const syst::SystematicsOptions &options)
    {
        syst::HistogramSpec out = spec;
        if (options.cache_nbins > out.nbins)
            out.nbins = options.cache_nbins;
        return out;
    }

    std::string encode_options_for_cache(const syst::HistogramSpec &spec,
                                         const syst::SystematicsOptions &options)
    {
        const syst::HistogramSpec fine_spec = fine_spec_for(spec, options);

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
                                     const std::string &sample_key,
                                     const syst::HistogramSpec &spec,
                                     const syst::SystematicsOptions &options)
    {
        std::ostringstream os;
        os << eventlist.path() << "|"
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

    int find_bin(const syst::HistogramSpec &spec, double value)
    {
        if (!std::isfinite(value))
            return -1;
        if (value < spec.xmin || value > spec.xmax)
            return -1;
        if (value == spec.xmax)
            return spec.nbins - 1;

        const double width = (spec.xmax - spec.xmin) / static_cast<double>(spec.nbins);
        if (width <= 0.0)
            return -1;

        const int bin = static_cast<int>((value - spec.xmin) / width);
        if (bin < 0 || bin >= spec.nbins)
            return -1;
        return bin;
    }

    struct UniverseAccumulator
    {
        std::string branch_name;
        std::vector<unsigned short> *raw = nullptr;
        std::size_t n_universes = 0;
        std::vector<double> histograms; // row-major: bin-major, universe-minor

        void ensure_size(int nbins)
        {
            if (!raw)
                return;
            if (n_universes == 0)
            {
                n_universes = raw->size();
                histograms.assign(static_cast<std::size_t>(nbins) * n_universes, 0.0);
            }
        }

        void accumulate(int bin, int nbins, double base_weight)
        {
            ensure_size(nbins);
            if (n_universes == 0 || !raw)
                return;

            const std::size_t offset = static_cast<std::size_t>(bin) * n_universes;
            const std::size_t n = std::min(n_universes, raw->size());
            for (std::size_t universe = 0; universe < n; ++universe)
                histograms[offset + universe] += base_weight * decode_universe_weight((*raw)[universe]);
        }
    };

    struct SampleComputation
    {
        std::vector<double> nominal;
        std::optional<UniverseAccumulator> genie;
        std::optional<UniverseAccumulator> flux;
        std::optional<UniverseAccumulator> reint;
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

    std::vector<std::string> resolve_detector_sample_keys(EventListIO &eventlist,
                                                          const syst::CacheRequest &request)
    {
        if (!request.detector_sample_keys.empty())
            return request.detector_sample_keys;
        return ana::detector_mates(eventlist, request.sample_key);
    }

    SampleComputation compute_sample(TTree *tree,
                                     const syst::HistogramSpec &spec,
                                     const syst::SystematicsOptions &options)
    {
        if (!tree)
            throw std::runtime_error("SystematicsEngine: missing selected tree");
        if (spec.branch_expr.empty())
            throw std::runtime_error("SystematicsEngine: branch_expr is required");
        if (spec.nbins <= 0)
            throw std::runtime_error("SystematicsEngine: nbins must be positive");
        if (!(spec.xmax > spec.xmin))
            throw std::runtime_error("SystematicsEngine: invalid histogram range");

        SampleComputation out;
        out.nominal.assign(static_cast<std::size_t>(spec.nbins), 0.0);

        double central_weight = 1.0;
        tree->SetBranchAddress(kCentralWeightBranch, &central_weight);

        TTreeFormula observable("systematics_observable", spec.branch_expr.c_str(), tree);
        std::unique_ptr<TTreeFormula> selection;
        if (!spec.selection_expr.empty())
            selection.reset(new TTreeFormula("systematics_selection", spec.selection_expr.c_str(), tree));

        if (options.enable_genie && tree->GetBranch("weightsGenie"))
        {
            UniverseAccumulator family;
            family.branch_name = "weightsGenie";
            tree->SetBranchAddress(family.branch_name.c_str(), &family.raw);
            out.genie = family;
        }
        if (options.enable_flux && tree->GetBranch("weightsPPFX"))
        {
            UniverseAccumulator family;
            family.branch_name = "weightsPPFX";
            tree->SetBranchAddress(family.branch_name.c_str(), &family.raw);
            out.flux = family;
        }
        if (options.enable_reint && tree->GetBranch("weightsReint"))
        {
            UniverseAccumulator family;
            family.branch_name = "weightsReint";
            tree->SetBranchAddress(family.branch_name.c_str(), &family.raw);
            out.reint = family;
        }

        const Long64_t n_entries = tree->GetEntries();
        for (Long64_t entry = 0; entry < n_entries; ++entry)
        {
            tree->GetEntry(entry);

            if (selection && selection->EvalInstance() == 0.0)
                continue;

            const double value = observable.EvalInstance();
            const int bin = find_bin(spec, value);
            if (bin < 0)
                continue;

            out.nominal[static_cast<std::size_t>(bin)] += central_weight;

            if (out.genie)
                out.genie->accumulate(bin, spec.nbins, central_weight);
            if (out.flux)
                out.flux->accumulate(bin, spec.nbins, central_weight);
            if (out.reint)
                out.reint->accumulate(bin, spec.nbins, central_weight);
        }

        return out;
    }

#if defined(AMARANTIN_HAVE_EIGEN)
    using MatrixRowMajor = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

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
#endif

    std::vector<double> rebin_vector(const std::vector<double> &source,
                                     int source_nbins,
                                     double source_xmin,
                                     double source_xmax,
                                     const syst::HistogramSpec &target_spec)
    {
        if (source.empty())
            return std::vector<double>{};

#if defined(AMARANTIN_HAVE_EIGEN)
        const MatrixRowMajor rebin = build_rebin_matrix(source_nbins,
                                                        source_xmin,
                                                        source_xmax,
                                                        target_spec.nbins,
                                                        target_spec.xmin,
                                                        target_spec.xmax);
        const Eigen::Map<const Eigen::VectorXd> source_vec(source.data(), source_nbins);
        const Eigen::VectorXd target = rebin * source_vec;
        return std::vector<double>(target.data(), target.data() + target.size());
#else
        std::vector<double> target(static_cast<std::size_t>(target_spec.nbins), 0.0);
        const double source_width = (source_xmax - source_xmin) / static_cast<double>(source_nbins);
        const double target_width = (target_spec.xmax - target_spec.xmin) / static_cast<double>(target_spec.nbins);
        for (int target_bin = 0; target_bin < target_spec.nbins; ++target_bin)
        {
            const double target_low = target_spec.xmin + target_width * static_cast<double>(target_bin);
            const double target_high = target_low + target_width;

            for (int source_bin = 0; source_bin < source_nbins; ++source_bin)
            {
                const double source_low = source_xmin + source_width * static_cast<double>(source_bin);
                const double source_high = source_low + source_width;
                const double overlap = std::max(0.0, std::min(source_high, target_high) - std::max(source_low, target_low));
                if (overlap > 0.0)
                    target[static_cast<std::size_t>(target_bin)] += source[static_cast<std::size_t>(source_bin)] * (overlap / source_width);
            }
        }
        return target;
#endif
    }

    std::vector<std::vector<double>> rebin_detector_templates(const CacheEntry &entry,
                                                              const syst::HistogramSpec &target_spec)
    {
        std::vector<std::vector<double>> out;
        if (entry.detector_template_count <= 0 || entry.detector_templates.empty())
            return out;

        out.assign(static_cast<std::size_t>(entry.detector_template_count),
                   std::vector<double>(static_cast<std::size_t>(target_spec.nbins), 0.0));

#if defined(AMARANTIN_HAVE_EIGEN)
        const MatrixRowMajor rebin = build_rebin_matrix(entry.nbins,
                                                        entry.xmin,
                                                        entry.xmax,
                                                        target_spec.nbins,
                                                        target_spec.xmin,
                                                        target_spec.xmax);
        const Eigen::Map<const MatrixRowMajor> templates(entry.detector_templates.data(),
                                                         entry.detector_template_count,
                                                         entry.nbins);
        const MatrixRowMajor rebinned = templates * rebin.transpose();
        for (int row = 0; row < entry.detector_template_count; ++row)
        {
            for (int col = 0; col < target_spec.nbins; ++col)
                out[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] = rebinned(row, col);
        }
#else
        for (int row = 0; row < entry.detector_template_count; ++row)
        {
            const std::vector<double> source(entry.detector_templates.begin() + row * entry.nbins,
                                             entry.detector_templates.begin() + (row + 1) * entry.nbins);
            out[static_cast<std::size_t>(row)] = rebin_vector(source,
                                                              entry.nbins,
                                                              entry.xmin,
                                                              entry.xmax,
                                                              target_spec);
        }
#endif
        return out;
    }

    syst::Envelope detector_envelope(const std::vector<double> &nominal,
                                     const std::vector<std::vector<double>> &variations)
    {
        syst::Envelope out;
        if (variations.empty())
            return out;

        out.down = nominal;
        out.up = nominal;
        for (const auto &variation : variations)
        {
            if (variation.size() != nominal.size())
                continue;
            for (std::size_t bin = 0; bin < nominal.size(); ++bin)
            {
                out.down[bin] = std::min(out.down[bin], variation[bin]);
                out.up[bin] = std::max(out.up[bin], variation[bin]);
            }
        }
        return out;
    }

    FamilyCache build_family_cache(const UniverseAccumulator &family,
                                   const std::vector<double> &nominal,
                                   int nbins,
                                   const syst::SystematicsOptions &options)
    {
        FamilyCache out;
        out.branch_name = family.branch_name;
        out.n_variations = static_cast<long long>(family.n_universes);
        out.sigma.assign(static_cast<std::size_t>(nbins), 0.0);

        if (family.n_universes == 0)
            return out;

#if defined(AMARANTIN_HAVE_EIGEN)
        const Eigen::Map<const MatrixRowMajor> universes(family.histograms.data(),
                                                         nbins,
                                                         static_cast<Eigen::Index>(family.n_universes));
        const Eigen::Map<const Eigen::VectorXd> nominal_vec(nominal.data(), nbins);
        const MatrixRowMajor deltas = universes.colwise() - nominal_vec;
        const Eigen::MatrixXd covariance =
            (deltas * deltas.transpose()) / static_cast<double>(family.n_universes);
        const Eigen::VectorXd sigma =
            covariance.diagonal().cwiseMax(0.0).cwiseSqrt();
        for (int bin = 0; bin < nbins; ++bin)
            out.sigma[static_cast<std::size_t>(bin)] = sigma(bin);

        if (options.persist_covariance)
        {
            out.covariance.resize(static_cast<std::size_t>(nbins * nbins));
            for (int row = 0; row < nbins; ++row)
            {
                for (int col = 0; col < nbins; ++col)
                    out.covariance[static_cast<std::size_t>(row * nbins + col)] = covariance(row, col);
            }
        }

        if (options.enable_eigenmode_compression)
        {
            Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(covariance);
            if (solver.info() != Eigen::Success)
                throw std::runtime_error("SystematicsEngine: eigenmode compression failed");

            const Eigen::VectorXd eigenvalues = solver.eigenvalues();
            const Eigen::MatrixXd eigenvectors = solver.eigenvectors();
            const double total_variance = std::max(0.0, eigenvalues.cwiseMax(0.0).sum());

            std::vector<int> selected;
            double captured = 0.0;
            for (int idx = static_cast<int>(eigenvalues.size()) - 1; idx >= 0; --idx)
            {
                const double value = std::max(0.0, eigenvalues(idx));
                if (value <= 0.0)
                    continue;
                selected.push_back(idx);
                captured += value;
                if ((options.max_eigenmodes > 0 && static_cast<int>(selected.size()) >= options.max_eigenmodes) ||
                    (total_variance > 0.0 && captured / total_variance >= options.eigenmode_fraction))
                    break;
            }

            out.eigen_rank = static_cast<int>(selected.size());
            out.eigenvalues.reserve(selected.size());
            out.eigenmodes.assign(static_cast<std::size_t>(nbins) * selected.size(), 0.0);
            for (std::size_t col = 0; col < selected.size(); ++col)
            {
                const int idx = selected[col];
                const double value = std::max(0.0, eigenvalues(idx));
                out.eigenvalues.push_back(value);
                const Eigen::VectorXd mode = eigenvectors.col(idx) * std::sqrt(value);
                for (int row = 0; row < nbins; ++row)
                    out.eigenmodes[static_cast<std::size_t>(row * selected.size() + col)] = mode(row);
            }
        }
#else
        for (int row = 0; row < nbins; ++row)
        {
            const std::size_t row_offset = static_cast<std::size_t>(row) * family.n_universes;
            double variance = 0.0;
            for (std::size_t universe = 0; universe < family.n_universes; ++universe)
            {
                const double delta = family.histograms[row_offset + universe] - nominal[static_cast<std::size_t>(row)];
                variance += delta * delta;
            }
            out.sigma[static_cast<std::size_t>(row)] =
                std::sqrt(variance / static_cast<double>(family.n_universes));
        }
#endif
        return out;
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

    CacheEntry build_cache_entry(TTree *nominal_tree,
                                 const syst::HistogramSpec &fine_spec,
                                 const syst::SystematicsOptions &options,
                                 const std::vector<TTree *> &detector_trees)
    {
        const SampleComputation nominal_sample = compute_sample(nominal_tree, fine_spec, options);

        CacheEntry entry;
        entry.version = kSystematicsCacheVersion;
        entry.branch_expr = fine_spec.branch_expr;
        entry.selection_expr = fine_spec.selection_expr;
        entry.nbins = fine_spec.nbins;
        entry.xmin = fine_spec.xmin;
        entry.xmax = fine_spec.xmax;
        entry.nominal = nominal_sample.nominal;

        std::vector<std::vector<double>> detector_histograms;
        detector_histograms.reserve(detector_trees.size());
        for (TTree *tree : detector_trees)
        {
            const SampleComputation variation = compute_sample(tree, fine_spec, syst::SystematicsOptions{});
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
            const syst::Envelope detector = detector_envelope(entry.nominal, detector_histograms);
            entry.detector_down = detector.down;
            entry.detector_up = detector.up;
        }

        if (nominal_sample.genie)
            entry.genie = build_family_cache(*nominal_sample.genie, entry.nominal, fine_spec.nbins, options);
        if (nominal_sample.flux)
            entry.flux = build_family_cache(*nominal_sample.flux, entry.nominal, fine_spec.nbins, options);
        if (nominal_sample.reint)
            entry.reint = build_family_cache(*nominal_sample.reint, entry.nominal, fine_spec.nbins, options);

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

    syst::UniverseFamilyResult family_result_from_cache(const FamilyCache &family,
                                                        const CacheEntry &entry,
                                                        const syst::HistogramSpec &target_spec,
                                                              bool build_full_covariance)
    {
        syst::UniverseFamilyResult out;
        out.branch_name = family.branch_name;
        out.n_universes = static_cast<std::size_t>(family.n_variations);
        out.eigen_rank = family.eigen_rank;
        out.eigenvalues = family.eigenvalues;
        if (family.empty())
            return out;

        const std::vector<double> nominal = rebin_vector(entry.nominal,
                                                         entry.nbins,
                                                         entry.xmin,
                                                         entry.xmax,
                                                         target_spec);

        if (!family.eigenmodes.empty() && family.eigen_rank > 0)
        {
#if defined(AMARANTIN_HAVE_EIGEN)
            const MatrixRowMajor rebin = build_rebin_matrix(entry.nbins,
                                                            entry.xmin,
                                                            entry.xmax,
                                                            target_spec.nbins,
                                                            target_spec.xmin,
                                                            target_spec.xmax);
            const Eigen::Map<const MatrixRowMajor> modes(family.eigenmodes.data(),
                                                         entry.nbins,
                                                         family.eigen_rank);
            const MatrixRowMajor rebinned_modes = rebin * modes;
            out.eigenmodes.resize(static_cast<std::size_t>(target_spec.nbins * family.eigen_rank), 0.0);
            out.sigma.assign(static_cast<std::size_t>(target_spec.nbins), 0.0);
            for (int row = 0; row < target_spec.nbins; ++row)
            {
                double variance = 0.0;
                for (int col = 0; col < family.eigen_rank; ++col)
                {
                    const double value = rebinned_modes(row, col);
                    out.eigenmodes[static_cast<std::size_t>(row * family.eigen_rank + col)] = value;
                    variance += value * value;
                }
                out.sigma[static_cast<std::size_t>(row)] = std::sqrt(std::max(0.0, variance));
            }
            if (build_full_covariance)
            {
                const Eigen::MatrixXd covariance = rebinned_modes * rebinned_modes.transpose();
                out.covariance.resize(static_cast<std::size_t>(target_spec.nbins * target_spec.nbins), 0.0);
                for (int row = 0; row < target_spec.nbins; ++row)
                {
                    for (int col = 0; col < target_spec.nbins; ++col)
                        out.covariance[static_cast<std::size_t>(row * target_spec.nbins + col)] = covariance(row, col);
                }
            }
#else
            out.sigma = rebin_vector(family.sigma, entry.nbins, entry.xmin, entry.xmax, target_spec);
            if (build_full_covariance)
                out.covariance = rebin_vector(family.covariance,
                                              entry.nbins * entry.nbins,
                                              0.0,
                                              static_cast<double>(entry.nbins * entry.nbins),
                                              target_spec);
#endif
        }
        else if (!family.covariance.empty())
        {
#if defined(AMARANTIN_HAVE_EIGEN)
            const MatrixRowMajor rebin = build_rebin_matrix(entry.nbins,
                                                            entry.xmin,
                                                            entry.xmax,
                                                            target_spec.nbins,
                                                            target_spec.xmin,
                                                            target_spec.xmax);
            const Eigen::Map<const MatrixRowMajor> covariance(family.covariance.data(),
                                                              entry.nbins,
                                                              entry.nbins);
            const Eigen::MatrixXd rebinned = rebin * covariance * rebin.transpose();
            out.covariance.resize(static_cast<std::size_t>(target_spec.nbins * target_spec.nbins), 0.0);
            out.sigma.assign(static_cast<std::size_t>(target_spec.nbins), 0.0);
            for (int row = 0; row < target_spec.nbins; ++row)
            {
                out.sigma[static_cast<std::size_t>(row)] = std::sqrt(std::max(0.0, rebinned(row, row)));
                for (int col = 0; col < target_spec.nbins; ++col)
                    out.covariance[static_cast<std::size_t>(row * target_spec.nbins + col)] = rebinned(row, col);
            }
#else
            out.sigma = rebin_vector(family.sigma, entry.nbins, entry.xmin, entry.xmax, target_spec);
            if (build_full_covariance)
                out.covariance = family.covariance;
#endif
        }
        else
        {
            out.sigma = rebin_vector(family.sigma,
                                     entry.nbins,
                                     entry.xmin,
                                     entry.xmax,
                                     target_spec);
        }

        out.envelope.down = nominal;
        out.envelope.up = nominal;
        for (std::size_t bin = 0; bin < nominal.size() && bin < out.sigma.size(); ++bin)
        {
            out.envelope.down[bin] = std::max(0.0, nominal[bin] - out.sigma[bin]);
            out.envelope.up[bin] = nominal[bin] + out.sigma[bin];
        }
        return out;
    }

    syst::SystematicsResult result_from_cache(const CacheEntry &entry,
                                              const std::string &cache_key,
                                              const syst::HistogramSpec &target_spec,
                                              const syst::SystematicsOptions &options,
                                                    bool loaded_from_persistent_cache)
    {
        syst::SystematicsResult result;
        result.cache_key = cache_key;
        result.cached_nbins = entry.nbins;
        result.loaded_from_persistent_cache = loaded_from_persistent_cache;
        result.nominal = rebin_vector(entry.nominal,
                                      entry.nbins,
                                      entry.xmin,
                                      entry.xmax,
                                      target_spec);

        if (entry.detector_template_count > 0 && !entry.detector_templates.empty())
        {
            const std::vector<std::vector<double>> detector_histograms =
                rebin_detector_templates(entry, target_spec);
            result.detector = detector_envelope(result.nominal, detector_histograms);
        }
        else if (!entry.detector_down.empty() && !entry.detector_up.empty())
        {
            result.detector.down = rebin_vector(entry.detector_down,
                                                entry.nbins,
                                                entry.xmin,
                                                entry.xmax,
                                                target_spec);
            result.detector.up = rebin_vector(entry.detector_up,
                                              entry.nbins,
                                              entry.xmin,
                                              entry.xmax,
                                              target_spec);
        }

        if (!entry.genie.empty())
            result.genie = family_result_from_cache(entry.genie, entry, target_spec, options.build_full_covariance);
        if (!entry.flux.empty())
            result.flux = family_result_from_cache(entry.flux, entry, target_spec, options.build_full_covariance);
        if (!entry.reint.empty())
            result.reint = family_result_from_cache(entry.reint, entry, target_spec, options.build_full_covariance);

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
        return stable_hash_hex(encode_options_for_cache(spec, options));
    }

    SystematicsResult evaluate(EventListIO &eventlist,
                               const std::string &sample_key,
                               const HistogramSpec &spec,
                               const SystematicsOptions &options)
    {
        if (sample_key.empty())
            throw std::runtime_error("SystematicsEngine: sample_key is required");

        const std::string eval_key = evaluation_cache_key(eventlist, sample_key, spec, options);
        if (options.enable_memory_cache)
        {
            std::lock_guard<std::mutex> lock(memory_cache_mutex());
            const auto it = memory_cache_store().find(eval_key);
            if (it != memory_cache_store().end())
                return it->second;
        }

        const HistogramSpec fine_spec = fine_spec_for(spec, options);
        const std::string persistent_key = cache_key(spec, options);

        bool loaded_from_persistent_cache = false;
        CacheEntry entry;

        const bool use_persistent_cache = options.persistent_cache != CachePolicy::kMemoryOnly;
        const bool can_write_persistent_cache = eventlist.mode() != EventListIO::Mode::kRead;

        if (use_persistent_cache &&
            options.persistent_cache != CachePolicy::kRebuild &&
            eventlist.has_systematics_cache(sample_key, persistent_key))
        {
            entry = eventlist.read_systematics_cache(sample_key, persistent_key);
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
            entry.detector_sample_keys = options.detector_sample_keys;

            if (use_persistent_cache && can_write_persistent_cache)
                eventlist.write_systematics_cache(sample_key, persistent_key, entry);
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
                                 const CacheBuildOptions &options)
    {
        if (!options.active())
            return;
        if (eventlist.mode() == EventListIO::Mode::kRead)
            throw std::runtime_error("syst::build_systematics_cache: event list must be writable");

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

            (void)evaluate(eventlist,
                           request.sample_key,
                           spec,
                           sysopt);
        }

        eventlist.flush();
    }

    SystematicsResult SystematicsEngine::evaluate(EventListIO &eventlist,
                                                  const std::string &sample_key,
                                                  const HistogramSpec &spec,
                                                  const SystematicsOptions &options)
    {
        return syst::evaluate(eventlist, sample_key, spec, options);
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
