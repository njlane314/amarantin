#include "bits/Detail.hh"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>

namespace
{
    using MatrixRowMajor = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

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

    std::vector<std::vector<double>> unpack_universe_histograms(
        const std::vector<double> &payload,
        int target_nbins,
        long long n_variations)
    {
        if (payload.empty() || n_variations <= 0)
            return {};

        const auto n_universes = static_cast<Eigen::Index>(n_variations);
        if (payload.size() != static_cast<std::size_t>(target_nbins) *
                                  static_cast<std::size_t>(n_universes))
        {
            throw std::runtime_error("syst: retained universe histogram payload is truncated");
        }

        const MatrixRowMajor rebinned(
            Eigen::Map<const MatrixRowMajor>(
                payload.data(),
                target_nbins,
                n_universes));

        std::vector<std::vector<double>> out(
            static_cast<std::size_t>(n_universes),
            std::vector<double>(static_cast<std::size_t>(rebinned.rows()), 0.0));
        for (Eigen::Index universe = 0; universe < n_universes; ++universe)
        {
            for (Eigen::Index bin = 0; bin < rebinned.rows(); ++bin)
                out[static_cast<std::size_t>(universe)][static_cast<std::size_t>(bin)] = rebinned(bin, universe);
        }
        return out;
    }

    std::vector<double> flatten_covariance(const Eigen::MatrixXd &covariance)
    {
        std::vector<double> out(static_cast<std::size_t>(covariance.rows() * covariance.cols()), 0.0);
        for (Eigen::Index row = 0; row < covariance.rows(); ++row)
        {
            for (Eigen::Index col = 0; col < covariance.cols(); ++col)
            {
                out[static_cast<std::size_t>(row * covariance.cols() + col)] = covariance(row, col);
            }
        }
        return out;
    }

    std::vector<double> sigma_from_covariance(const Eigen::MatrixXd &covariance)
    {
        std::vector<double> sigma(static_cast<std::size_t>(covariance.rows()), 0.0);
        for (Eigen::Index row = 0; row < covariance.rows(); ++row)
            sigma[static_cast<std::size_t>(row)] = std::sqrt(std::max(0.0, covariance(row, row)));
        return sigma;
    }

    std::vector<int> select_fractional_modes(const Eigen::VectorXd &eigenvalues,
                                             int max_modes,
                                             double fraction)
    {
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
            if ((max_modes > 0 && static_cast<int>(selected.size()) >= max_modes) ||
                (total_variance > 0.0 && captured / total_variance >= fraction))
            {
                break;
            }
        }
        return selected;
    }

    std::vector<int> select_top_modes(const Eigen::VectorXd &eigenvalues,
                                      int mode_count)
    {
        std::vector<int> selected;
        if (mode_count <= 0)
            return selected;

        for (int idx = static_cast<int>(eigenvalues.size()) - 1; idx >= 0; --idx)
        {
            const double value = std::max(0.0, eigenvalues(idx));
            if (value <= 0.0)
                continue;

            selected.push_back(idx);
            if (static_cast<int>(selected.size()) >= mode_count)
                break;
        }
        return selected;
    }

    void fill_modes_from_indices(const Eigen::VectorXd &eigenvalues,
                                 const Eigen::MatrixXd &eigenvectors,
                                 const std::vector<int> &selected,
                                 int nrows,
                                 int &eigen_rank,
                                 std::vector<double> &out_eigenvalues,
                                 std::vector<double> &out_eigenmodes)
    {
        eigen_rank = static_cast<int>(selected.size());
        out_eigenvalues.clear();
        out_eigenmodes.clear();
        if (selected.empty())
            return;

        out_eigenvalues.reserve(selected.size());
        out_eigenmodes.assign(static_cast<std::size_t>(nrows) * selected.size(), 0.0);
        for (std::size_t col = 0; col < selected.size(); ++col)
        {
            const int idx = selected[col];
            const double value = std::max(0.0, eigenvalues(idx));
            out_eigenvalues.push_back(value);
            const Eigen::VectorXd mode = eigenvectors.col(idx) * std::sqrt(value);
            for (int row = 0; row < nrows; ++row)
                out_eigenmodes[static_cast<std::size_t>(row * selected.size() + col)] = mode(row);
        }
    }

    void fill_fractional_modes_from_covariance(const Eigen::MatrixXd &covariance,
                                               int max_modes,
                                               double fraction,
                                               int &eigen_rank,
                                               std::vector<double> &out_eigenvalues,
                                               std::vector<double> &out_eigenmodes)
    {
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(covariance);
        if (solver.info() != Eigen::Success)
            throw std::runtime_error("syst: eigenmode compression failed");

        const Eigen::VectorXd eigenvalues = solver.eigenvalues();
        const Eigen::MatrixXd eigenvectors = solver.eigenvectors();
        const std::vector<int> selected =
            select_fractional_modes(eigenvalues, max_modes, fraction);
        fill_modes_from_indices(eigenvalues,
                                eigenvectors,
                                selected,
                                static_cast<int>(covariance.rows()),
                                eigen_rank,
                                out_eigenvalues,
                                out_eigenmodes);
    }

    void fill_rank_limited_modes_from_covariance(const Eigen::MatrixXd &covariance,
                                                 int mode_count,
                                                 int &eigen_rank,
                                                 std::vector<double> &out_eigenvalues,
                                                 std::vector<double> &out_eigenmodes)
    {
        if (mode_count <= 0)
        {
            eigen_rank = 0;
            out_eigenvalues.clear();
            out_eigenmodes.clear();
            return;
        }

        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(covariance);
        if (solver.info() != Eigen::Success)
            throw std::runtime_error("syst: eigenmode compression failed");

        const Eigen::VectorXd eigenvalues = solver.eigenvalues();
        const Eigen::MatrixXd eigenvectors = solver.eigenvectors();
        const std::vector<int> selected = select_top_modes(eigenvalues, mode_count);
        fill_modes_from_indices(eigenvalues,
                                eigenvectors,
                                selected,
                                static_cast<int>(covariance.rows()),
                                eigen_rank,
                                out_eigenvalues,
                                out_eigenmodes);
    }

    Eigen::MatrixXd covariance_from_universe_histograms(const std::vector<std::vector<double>> &universes,
                                                        const std::vector<double> &nominal)
    {
        const int nbins = static_cast<int>(nominal.size());
        Eigen::MatrixXd covariance = Eigen::MatrixXd::Zero(nbins, nbins);
        if (universes.empty())
            return covariance;

        const Eigen::Map<const Eigen::VectorXd> nominal_vec(nominal.data(), nbins);
        for (const auto &universe_histogram : universes)
        {
            if (static_cast<int>(universe_histogram.size()) != nbins)
                throw std::runtime_error("syst: retained universe histogram size does not match nominal bins");
            const Eigen::Map<const Eigen::VectorXd> universe_vec(universe_histogram.data(), nbins);
            const Eigen::VectorXd delta = universe_vec - nominal_vec;
            covariance += delta * delta.transpose();
        }

        covariance /= static_cast<double>(universes.size());
        return covariance;
    }
}

namespace syst::detail
{
    FamilyCache build_family_cache(const UniverseAccumulator &family,
                                   const std::vector<double> &nominal,
                                   int nbins,
                                   const SystematicsOptions &options)
    {
        FamilyCache out;
        out.branch_name = family.branch_name;
        out.n_variations = static_cast<long long>(family.n_universes);
        out.sigma.assign(static_cast<std::size_t>(nbins), 0.0);
        if (options.retain_universe_histograms)
            out.universe_histograms = family.histograms;

        if (family.n_universes == 0)
            return out;

        const Eigen::Map<const MatrixRowMajor> universes(family.histograms.data(),
                                                         nbins,
                                                         static_cast<Eigen::Index>(family.n_universes));
        const Eigen::Map<const Eigen::VectorXd> nominal_vec(nominal.data(), nbins);
        const MatrixRowMajor deltas = universes.colwise() - nominal_vec;
        const Eigen::MatrixXd covariance =
            (deltas * deltas.transpose()) / static_cast<double>(family.n_universes);
        out.sigma = sigma_from_covariance(covariance);
        out.covariance = flatten_covariance(covariance);

        if (options.enable_eigenmode_compression)
        {
            fill_fractional_modes_from_covariance(covariance,
                                                  options.max_eigenmodes,
                                                  options.eigenmode_fraction,
                                                  out.eigen_rank,
                                                  out.eigenvalues,
                                                  out.eigenmodes);
        }
        return out;
    }

    UniverseFamilyResult family_result_from_cache(const FamilyCache &family,
                                                  const CacheEntry &entry,
                                                  const HistogramSpec &target_spec,
                                                  bool build_full_covariance,
                                                  bool retain_universe_histograms)
    {
        UniverseFamilyResult out;
        out.branch_name = family.branch_name;
        out.n_universes = static_cast<std::size_t>(family.n_variations);
        out.eigen_rank = family.eigen_rank;
        out.eigenvalues = family.eigenvalues;
        if (family.empty())
            return out;

        const DistributionIO::HistogramSpec target_dist_spec =
            target_distribution_spec(entry.spec, target_spec);
        const std::vector<double> nominal =
            entry.rebinned_values(entry.nominal, target_dist_spec);

        if (retain_universe_histograms)
        {
            out.universe_histograms = unpack_universe_histograms(
                entry.rebinned_bin_major_payload(family.universe_histograms,
                                                 static_cast<int>(family.n_variations),
                                                 target_dist_spec),
                target_spec.nbins,
                family.n_variations);
        }

        if (!family.covariance.empty())
        {
            const std::vector<double> rebinned_covariance =
                entry.rebinned_covariance(family.covariance, target_dist_spec);
            const Eigen::MatrixXd rebinned =
                Eigen::Map<const MatrixRowMajor>(rebinned_covariance.data(),
                                                 target_spec.nbins,
                                                 target_spec.nbins);
            out.covariance = flatten_covariance(rebinned);
            out.sigma = sigma_from_covariance(rebinned);
            fill_rank_limited_modes_from_covariance(rebinned,
                                                    family.eigen_rank,
                                                    out.eigen_rank,
                                                    out.eigenvalues,
                                                    out.eigenmodes);
        }
        else if (!out.universe_histograms.empty())
        {
            const Eigen::MatrixXd covariance =
                covariance_from_universe_histograms(out.universe_histograms, nominal);
            out.covariance = flatten_covariance(covariance);
            out.sigma = sigma_from_covariance(covariance);
            fill_rank_limited_modes_from_covariance(covariance,
                                                    family.eigen_rank,
                                                    out.eigen_rank,
                                                    out.eigenvalues,
                                                    out.eigenmodes);
        }
        else if (!family.eigenmodes.empty() && family.eigen_rank > 0)
        {
            const std::vector<double> rebinned_mode_payload =
                entry.rebinned_bin_major_payload(family.eigenmodes,
                                                 family.eigen_rank,
                                                 target_dist_spec);
            const Eigen::Map<const MatrixRowMajor> rebinned_modes(rebinned_mode_payload.data(),
                                                                  target_spec.nbins,
                                                                  family.eigen_rank);
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
                out.covariance = flatten_covariance(covariance);
            }
        }
        else
        {
            out.sigma.assign(static_cast<std::size_t>(target_spec.nbins), 0.0);
            if (build_full_covariance)
            {
                out.covariance.assign(static_cast<std::size_t>(target_spec.nbins * target_spec.nbins), 0.0);
            }

            for (const auto &universe_histogram : out.universe_histograms)
            {
                for (std::size_t bin = 0; bin < universe_histogram.size() && bin < nominal.size(); ++bin)
                {
                    const double delta = universe_histogram[bin] - nominal[bin];
                    out.sigma[bin] += delta * delta;
                    if (build_full_covariance)
                    {
                        for (std::size_t other = 0; other < universe_histogram.size() && other < nominal.size(); ++other)
                        {
                            const double other_delta = universe_histogram[other] - nominal[other];
                            out.covariance[bin * nominal.size() + other] += delta * other_delta;
                        }
                    }
                }
            }
            if (!out.universe_histograms.empty())
            {
                const double denom = static_cast<double>(out.universe_histograms.size());
                for (double &sigma : out.sigma)
                    sigma = std::sqrt(std::max(0.0, sigma / denom));
                if (build_full_covariance)
                {
                    for (double &value : out.covariance)
                        value /= denom;
                }
            }
            else if (entry.same_binning(target_dist_spec))
            {
                out.sigma = family.sigma;
            }
            else
            {
                throw std::runtime_error(
                    "syst: cached sigma-only family " + family.branch_name +
                    " cannot be rebinned; rebuild the cache with covariance, eigenmodes, or retained universe histograms");
            }
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
}
