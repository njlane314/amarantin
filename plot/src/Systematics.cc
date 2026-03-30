#include "Systematics.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "TH1D.h"
#include "TTree.h"
#include "TTreeFormula.h"

#if defined(AMARANTIN_HAVE_EIGEN)
#include <Eigen/Dense>
#endif

namespace
{
    constexpr const char *kCentralWeightBranch = "__w__";

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

    int find_bin(const plot_utils::HistogramSpec &spec, double value)
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

    struct CacheKey
    {
        std::string path;
        std::string sample_key;
        std::string branch_expr;
        int nbins = 0;
        double xmin = 0.0;
        double xmax = 0.0;
        std::string selection_expr;

        bool enable_detector = false;
        std::vector<std::string> detector_sample_keys;
        bool enable_genie = false;
        bool enable_flux = false;
        bool enable_reint = false;
        bool build_full_covariance = false;
        bool retain_universe_histograms = false;

        bool operator==(const CacheKey &other) const
        {
            return path == other.path &&
                   sample_key == other.sample_key &&
                   branch_expr == other.branch_expr &&
                   nbins == other.nbins &&
                   xmin == other.xmin &&
                   xmax == other.xmax &&
                   selection_expr == other.selection_expr &&
                   enable_detector == other.enable_detector &&
                   detector_sample_keys == other.detector_sample_keys &&
                   enable_genie == other.enable_genie &&
                   enable_flux == other.enable_flux &&
                   enable_reint == other.enable_reint &&
                   build_full_covariance == other.build_full_covariance &&
                   retain_universe_histograms == other.retain_universe_histograms;
        }
    };

    struct CacheKeyHash
    {
        static void hash_combine(std::size_t &seed, std::size_t value)
        {
            seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
        }

        std::size_t operator()(const CacheKey &key) const
        {
            std::size_t seed = 0;
            hash_combine(seed, std::hash<std::string>{}(key.path));
            hash_combine(seed, std::hash<std::string>{}(key.sample_key));
            hash_combine(seed, std::hash<std::string>{}(key.branch_expr));
            hash_combine(seed, std::hash<int>{}(key.nbins));
            hash_combine(seed, std::hash<double>{}(key.xmin));
            hash_combine(seed, std::hash<double>{}(key.xmax));
            hash_combine(seed, std::hash<std::string>{}(key.selection_expr));
            hash_combine(seed, std::hash<bool>{}(key.enable_detector));
            for (const auto &sample_key : key.detector_sample_keys)
                hash_combine(seed, std::hash<std::string>{}(sample_key));
            hash_combine(seed, std::hash<bool>{}(key.enable_genie));
            hash_combine(seed, std::hash<bool>{}(key.enable_flux));
            hash_combine(seed, std::hash<bool>{}(key.enable_reint));
            hash_combine(seed, std::hash<bool>{}(key.build_full_covariance));
            hash_combine(seed, std::hash<bool>{}(key.retain_universe_histograms));
            return seed;
        }
    };

    struct UniverseAccumulator
    {
        std::string branch_name;
        std::vector<unsigned short> *raw = nullptr;
        std::size_t n_universes = 0;
        std::vector<double> histograms;

        bool active() const
        {
            return !branch_name.empty() && raw != nullptr;
        }

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
            (void)nbins;
            ensure_size(nbins);
            if (n_universes == 0 || !raw)
                return;

            const std::size_t offset = static_cast<std::size_t>(bin) * n_universes;
            const std::size_t n = std::min(n_universes, raw->size());
            for (std::size_t universe = 0; universe < n; ++universe)
            {
                histograms[offset + universe] +=
                    base_weight * decode_universe_weight((*raw)[universe]);
            }
        }
    };

    struct SampleComputation
    {
        std::vector<double> nominal;
        std::optional<UniverseAccumulator> genie;
        std::optional<UniverseAccumulator> flux;
        std::optional<UniverseAccumulator> reint;
    };

    std::mutex &cache_mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::unordered_map<CacheKey, plot_utils::SystematicsResult, CacheKeyHash> &cache_store()
    {
        static std::unordered_map<CacheKey, plot_utils::SystematicsResult, CacheKeyHash> cache;
        return cache;
    }

    CacheKey make_cache_key(const EventListIO &eventlist,
                            const std::string &sample_key,
                            const plot_utils::HistogramSpec &spec,
                            const plot_utils::SystematicsOptions &options)
    {
        CacheKey key;
        key.path = eventlist.path();
        key.sample_key = sample_key;
        key.branch_expr = spec.branch_expr;
        key.nbins = spec.nbins;
        key.xmin = spec.xmin;
        key.xmax = spec.xmax;
        key.selection_expr = spec.selection_expr;
        key.enable_detector = options.enable_detector;
        key.detector_sample_keys = options.detector_sample_keys;
        key.enable_genie = options.enable_genie;
        key.enable_flux = options.enable_flux;
        key.enable_reint = options.enable_reint;
        key.build_full_covariance = options.build_full_covariance;
        key.retain_universe_histograms = options.retain_universe_histograms;
        return key;
    }

    SampleComputation compute_sample(TTree *tree,
                                     const plot_utils::HistogramSpec &spec,
                                     const plot_utils::SystematicsOptions &options)
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

    plot_utils::Envelope detector_envelope(const std::vector<double> &nominal,
                                           const std::vector<std::vector<double>> &variations)
    {
        plot_utils::Envelope out;
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

    plot_utils::UniverseFamilyResult build_universe_result(const UniverseAccumulator &family,
                                                           const std::vector<double> &nominal,
                                                           int nbins,
                                                           bool build_full_covariance,
                                                           bool retain_universe_histograms)
    {
        plot_utils::UniverseFamilyResult out;
        out.branch_name = family.branch_name;
        out.n_universes = family.n_universes;
        if (family.n_universes == 0)
            return out;

        out.envelope.down = nominal;
        out.envelope.up = nominal;
        out.sigma.assign(static_cast<std::size_t>(nbins), 0.0);

        if (retain_universe_histograms)
            out.universe_histograms.assign(family.n_universes, std::vector<double>(static_cast<std::size_t>(nbins), 0.0));

        if (build_full_covariance)
            out.covariance.assign(static_cast<std::size_t>(nbins * nbins), 0.0);

        for (int bin = 0; bin < nbins; ++bin)
        {
            const std::size_t row_offset = static_cast<std::size_t>(bin) * family.n_universes;
            double min_value = nominal[static_cast<std::size_t>(bin)];
            double max_value = nominal[static_cast<std::size_t>(bin)];

            for (std::size_t universe = 0; universe < family.n_universes; ++universe)
            {
                const double value = family.histograms[row_offset + universe];
                min_value = std::min(min_value, value);
                max_value = std::max(max_value, value);
                if (retain_universe_histograms)
                    out.universe_histograms[universe][static_cast<std::size_t>(bin)] = value;
            }

            out.envelope.down[static_cast<std::size_t>(bin)] = min_value;
            out.envelope.up[static_cast<std::size_t>(bin)] = max_value;
        }

#if defined(AMARANTIN_HAVE_EIGEN)
        using MatrixRowMajor = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
        const Eigen::Map<const MatrixRowMajor> histograms(family.histograms.data(),
                                                          nbins,
                                                          static_cast<Eigen::Index>(family.n_universes));
        const Eigen::Map<const Eigen::VectorXd> nominal_map(nominal.data(), nbins);
        const MatrixRowMajor deltas = histograms.colwise() - nominal_map;
        const Eigen::VectorXd sigma =
            (deltas.array().square().rowwise().mean()).sqrt().matrix();
        for (int bin = 0; bin < nbins; ++bin)
            out.sigma[static_cast<std::size_t>(bin)] = sigma(bin);

        if (build_full_covariance)
        {
            const Eigen::MatrixXd covariance =
                (deltas * deltas.transpose()) / static_cast<double>(family.n_universes);
            for (int row = 0; row < nbins; ++row)
            {
                for (int col = 0; col < nbins; ++col)
                    out.covariance[static_cast<std::size_t>(row * nbins + col)] = covariance(row, col);
            }
        }
#else
        for (int row = 0; row < nbins; ++row)
        {
            double variance = 0.0;
            const std::size_t row_offset = static_cast<std::size_t>(row) * family.n_universes;
            for (std::size_t universe = 0; universe < family.n_universes; ++universe)
            {
                const double delta = family.histograms[row_offset + universe] - nominal[static_cast<std::size_t>(row)];
                variance += delta * delta;
            }
            out.sigma[static_cast<std::size_t>(row)] =
                std::sqrt(variance / static_cast<double>(family.n_universes));
        }

        if (build_full_covariance)
        {
            for (int row = 0; row < nbins; ++row)
            {
                const std::size_t row_offset = static_cast<std::size_t>(row) * family.n_universes;
                for (int col = 0; col < nbins; ++col)
                {
                    const std::size_t col_offset = static_cast<std::size_t>(col) * family.n_universes;
                    double covariance = 0.0;
                    for (std::size_t universe = 0; universe < family.n_universes; ++universe)
                    {
                        const double delta_row =
                            family.histograms[row_offset + universe] - nominal[static_cast<std::size_t>(row)];
                        const double delta_col =
                            family.histograms[col_offset + universe] - nominal[static_cast<std::size_t>(col)];
                        covariance += delta_row * delta_col;
                    }
                    out.covariance[static_cast<std::size_t>(row * nbins + col)] =
                        covariance / static_cast<double>(family.n_universes);
                }
            }
        }
#endif

        return out;
    }

    void accumulate_sigma2(std::vector<double> &buffer, const std::vector<double> &sigma)
    {
        if (buffer.empty())
            buffer.assign(sigma.size(), 0.0);
        for (std::size_t i = 0; i < sigma.size(); ++i)
            buffer[i] += sigma[i] * sigma[i];
    }

    void accumulate_detector_sigma2(std::vector<double> &up_buffer,
                                    std::vector<double> &down_buffer,
                                    const std::vector<double> &nominal,
                                    const plot_utils::Envelope &detector)
    {
        if (detector.empty())
            return;
        if (up_buffer.empty())
        {
            up_buffer.assign(nominal.size(), 0.0);
            down_buffer.assign(nominal.size(), 0.0);
        }

        for (std::size_t bin = 0; bin < nominal.size(); ++bin)
        {
            const double up_shift = std::max(0.0, detector.up[bin] - nominal[bin]);
            const double down_shift = std::max(0.0, nominal[bin] - detector.down[bin]);
            up_buffer[bin] += up_shift * up_shift;
            down_buffer[bin] += down_shift * down_shift;
        }
    }
}

namespace plot_utils
{
    SystematicsResult SystematicsEngine::evaluate(const EventListIO &eventlist,
                                                  const std::string &sample_key,
                                                  const HistogramSpec &spec,
                                                  const SystematicsOptions &options)
    {
        if (sample_key.empty())
            throw std::runtime_error("SystematicsEngine: sample_key is required");

        const CacheKey key = make_cache_key(eventlist, sample_key, spec, options);
        if (options.enable_cache)
        {
            std::lock_guard<std::mutex> lock(cache_mutex());
            const auto it = cache_store().find(key);
            if (it != cache_store().end())
                return it->second;
        }

        TTree *nominal_tree = eventlist.selected_tree(sample_key);
        if (!nominal_tree)
            throw std::runtime_error("SystematicsEngine: missing nominal selected tree for sample " + sample_key);

        const SampleComputation nominal_sample = compute_sample(nominal_tree, spec, options);

        SystematicsResult result;
        result.nominal = nominal_sample.nominal;
        result.total_down = nominal_sample.nominal;
        result.total_up = nominal_sample.nominal;

        if (options.enable_detector && !options.detector_sample_keys.empty())
        {
            std::vector<std::vector<double>> detector_histograms;
            detector_histograms.reserve(options.detector_sample_keys.size());
            for (const auto &detector_sample_key : options.detector_sample_keys)
            {
                TTree *tree = eventlist.selected_tree(detector_sample_key);
                if (!tree)
                    continue;

                const SampleComputation variation = compute_sample(tree, spec, SystematicsOptions{});
                detector_histograms.push_back(variation.nominal);
            }
            result.detector = detector_envelope(result.nominal, detector_histograms);
        }

        if (options.enable_genie && nominal_sample.genie)
            result.genie = build_universe_result(*nominal_sample.genie,
                                                 result.nominal,
                                                 spec.nbins,
                                                 options.build_full_covariance,
                                                 options.retain_universe_histograms);
        if (options.enable_flux && nominal_sample.flux)
            result.flux = build_universe_result(*nominal_sample.flux,
                                                result.nominal,
                                                spec.nbins,
                                                options.build_full_covariance,
                                                options.retain_universe_histograms);
        if (options.enable_reint && nominal_sample.reint)
            result.reint = build_universe_result(*nominal_sample.reint,
                                                 result.nominal,
                                                 spec.nbins,
                                                 options.build_full_covariance,
                                                 options.retain_universe_histograms);

        std::vector<double> total_sigma2;
        std::vector<double> total_sigma2_down;
        accumulate_detector_sigma2(total_sigma2, total_sigma2_down, result.nominal, result.detector);
        if (result.genie)
        {
            accumulate_sigma2(total_sigma2, result.genie->sigma);
            accumulate_sigma2(total_sigma2_down, result.genie->sigma);
        }
        if (result.flux)
        {
            accumulate_sigma2(total_sigma2, result.flux->sigma);
            accumulate_sigma2(total_sigma2_down, result.flux->sigma);
        }
        if (result.reint)
        {
            accumulate_sigma2(total_sigma2, result.reint->sigma);
            accumulate_sigma2(total_sigma2_down, result.reint->sigma);
        }

        if (!total_sigma2.empty())
        {
            for (std::size_t bin = 0; bin < result.nominal.size(); ++bin)
            {
                result.total_up[bin] = result.nominal[bin] + std::sqrt(total_sigma2[bin]);
                result.total_down[bin] =
                    std::max(0.0, result.nominal[bin] - std::sqrt(total_sigma2_down[bin]));
            }
        }

        if (options.enable_cache)
        {
            std::lock_guard<std::mutex> lock(cache_mutex());
            cache_store()[key] = result;
        }

        return result;
    }

    void SystematicsEngine::clear_cache()
    {
        std::lock_guard<std::mutex> lock(cache_mutex());
        cache_store().clear();
    }

    std::unique_ptr<TH1D> SystematicsEngine::make_histogram(const HistogramSpec &spec,
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
}
