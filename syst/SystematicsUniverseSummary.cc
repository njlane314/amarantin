#include "bits/SystematicsInternal.hh"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <Eigen/Eigenvalues>

namespace
{
    std::vector<std::vector<double>> unpack_universe_histograms(
        const syst::detail::FamilyCache &family,
        int source_nbins,
        const syst::detail::MatrixRowMajor *rebin = nullptr)
    {
        if (family.universe_histograms.empty() || family.n_variations <= 0)
            return {};

        const auto n_universes = static_cast<Eigen::Index>(family.n_variations);
        const syst::detail::MatrixRowMajor rebinned =
            rebin
                ? (*rebin) * Eigen::Map<const syst::detail::MatrixRowMajor>(
                                family.universe_histograms.data(),
                                source_nbins,
                                n_universes)
                : syst::detail::MatrixRowMajor(
                      Eigen::Map<const syst::detail::MatrixRowMajor>(
                          family.universe_histograms.data(),
                          source_nbins,
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

        const std::vector<double> nominal = rebin_vector(entry.nominal,
                                                         entry.spec.nbins,
                                                         entry.spec.xmin,
                                                         entry.spec.xmax,
                                                         target_spec);
        const MatrixRowMajor rebin = build_rebin_matrix(entry.spec.nbins,
                                                        entry.spec.xmin,
                                                        entry.spec.xmax,
                                                        target_spec.nbins,
                                                        target_spec.xmin,
                                                        target_spec.xmax);

        if (retain_universe_histograms)
            out.universe_histograms = unpack_universe_histograms(family, entry.spec.nbins, &rebin);

        if (!family.eigenmodes.empty() && family.eigen_rank > 0)
        {
            const Eigen::Map<const MatrixRowMajor> modes(family.eigenmodes.data(),
                                                         entry.spec.nbins,
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
        }
        else if (!family.covariance.empty())
        {
            const Eigen::Map<const MatrixRowMajor> covariance(family.covariance.data(),
                                                              entry.spec.nbins,
                                                              entry.spec.nbins);
            const Eigen::MatrixXd rebinned = rebin * covariance * rebin.transpose();
            out.covariance.resize(static_cast<std::size_t>(target_spec.nbins * target_spec.nbins), 0.0);
            out.sigma.assign(static_cast<std::size_t>(target_spec.nbins), 0.0);
            for (int row = 0; row < target_spec.nbins; ++row)
            {
                out.sigma[static_cast<std::size_t>(row)] = std::sqrt(std::max(0.0, rebinned(row, row)));
                for (int col = 0; col < target_spec.nbins; ++col)
                    out.covariance[static_cast<std::size_t>(row * target_spec.nbins + col)] = rebinned(row, col);
            }
        }
        else
        {
            out.sigma.assign(static_cast<std::size_t>(target_spec.nbins), 0.0);
            for (const auto &universe_histogram : out.universe_histograms)
            {
                for (std::size_t bin = 0; bin < universe_histogram.size() && bin < nominal.size(); ++bin)
                {
                    const double delta = universe_histogram[bin] - nominal[bin];
                    out.sigma[bin] += delta * delta;
                }
            }
            if (!out.universe_histograms.empty())
            {
                const double denom = static_cast<double>(out.universe_histograms.size());
                for (double &sigma : out.sigma)
                    sigma = std::sqrt(std::max(0.0, sigma / denom));
            }
            else
            {
                out.sigma = rebin_vector(family.sigma,
                                         entry.spec.nbins,
                                         entry.spec.xmin,
                                         entry.spec.xmax,
                                         target_spec);
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
