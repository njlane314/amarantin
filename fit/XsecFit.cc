#include "XsecFit.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
    constexpr double kYieldFloor = 1e-9;
    constexpr int kGoldenIterations = 24;

    struct ProfilePoint
    {
        double mu = 1.0;
        double objective = std::numeric_limits<double>::infinity();
        std::vector<double> nuisance_values;
    };

    double clamp_value(double value, double lower, double upper)
    {
        return std::max(lower, std::min(value, upper));
    }

    const DistributionIO::Family &family_for(const ChannelIO::Process &process,
                                             fit::FamilyKind family)
    {
        switch (family)
        {
            case fit::FamilyKind::kGenie:
                return process.genie;
            case fit::FamilyKind::kFlux:
                return process.flux;
            case fit::FamilyKind::kReint:
                return process.reint;
        }
        throw std::runtime_error("fit::family_for: unknown family kind");
    }

    int family_mode_count(const DistributionIO::Family &family)
    {
        if (family.eigen_rank > 0 && !family.eigenmodes.empty())
            return family.eigen_rank;
        if (!family.sigma.empty())
            return 1;
        return 0;
    }

    double family_mode_value(const DistributionIO::Family &family,
                             int mode_index,
                             int bin)
    {
        if (mode_index < 0)
            throw std::runtime_error("fit::family_mode_value: mode_index must not be negative");

        if (family.eigen_rank > 0 && !family.eigenmodes.empty())
        {
            if (mode_index >= family.eigen_rank)
                throw std::runtime_error("fit::family_mode_value: mode_index out of range");
            const std::size_t index =
                static_cast<std::size_t>(bin * family.eigen_rank + mode_index);
            if (index >= family.eigenmodes.size())
                throw std::runtime_error("fit::family_mode_value: eigenmode payload is truncated");
            return family.eigenmodes[index];
        }

        if (!family.sigma.empty())
        {
            if (mode_index != 0)
                throw std::runtime_error("fit::family_mode_value: sigma-only family exposes only one mode");
            if (static_cast<std::size_t>(bin) >= family.sigma.size())
                throw std::runtime_error("fit::family_mode_value: sigma payload is truncated");
            return family.sigma[static_cast<std::size_t>(bin)];
        }

        throw std::runtime_error("fit::family_mode_value: family has no usable fit payload");
    }

    void validate_model(const fit::Model &model)
    {
        if (!model.channel)
            throw std::runtime_error("fit::validate_model: model.channel must not be null");
        if (model.signal_process.empty())
            throw std::runtime_error("fit::validate_model: signal_process must not be empty");
        if (model.channel->data.size() != static_cast<std::size_t>(model.channel->spec.nbins))
            throw std::runtime_error("fit::validate_model: channel data size does not match nbins");
        if (!model.channel->find_process(model.signal_process))
            throw std::runtime_error("fit::validate_model: signal process is not present in the channel");
        if (model.mu_upper < model.mu_lower)
            throw std::runtime_error("fit::validate_model: mu bounds are inverted");

        for (const auto &process : model.channel->processes)
        {
            if (process.kind == ChannelIO::ProcessKind::kData)
                continue;
            if (process.nominal.size() != static_cast<std::size_t>(model.channel->spec.nbins))
                throw std::runtime_error("fit::validate_model: process nominal size does not match nbins");
        }

        for (const auto &nuisance : model.nuisances)
        {
            if (nuisance.upper < nuisance.lower)
                throw std::runtime_error("fit::validate_model: nuisance bounds are inverted");
            if (nuisance.constrained && nuisance.prior_sigma <= 0.0)
                throw std::runtime_error("fit::validate_model: constrained nuisance prior_sigma must be positive");

            for (const auto &term : nuisance.terms)
            {
                const ChannelIO::Process *process = model.channel->find_process(term.process_name);
                if (!process)
                    throw std::runtime_error("fit::validate_model: nuisance term references unknown process");
                const DistributionIO::Family &family = family_for(*process, term.family);
                const int mode_count = family_mode_count(family);
                if (term.mode_index < 0 || term.mode_index >= mode_count)
                    throw std::runtime_error("fit::validate_model: nuisance term mode_index out of range");
            }
        }
    }

    std::vector<double> initial_nuisance_values(const fit::Model &model)
    {
        std::vector<double> out;
        out.reserve(model.nuisances.size());
        for (const auto &nuisance : model.nuisances)
            out.push_back(clamp_value(nuisance.start_value, nuisance.lower, nuisance.upper));
        return out;
    }

    double process_bin_yield(const fit::Model &model,
                             const ChannelIO::Process &process,
                             int bin,
                             double mu,
                             const std::vector<double> &nuisance_values)
    {
        double value = process.nominal.at(static_cast<std::size_t>(bin));

        for (std::size_t nuisance_index = 0; nuisance_index < model.nuisances.size(); ++nuisance_index)
        {
            const double theta = nuisance_values[nuisance_index];
            const auto &nuisance = model.nuisances[nuisance_index];
            for (const auto &term : nuisance.terms)
            {
                if (term.process_name != process.name)
                    continue;
                const DistributionIO::Family &family = family_for(process, term.family);
                value += term.coefficient *
                         family_mode_value(family, term.mode_index, bin) *
                         theta;
            }
        }

        if (process.name == model.signal_process)
            value *= mu;

        if (!std::isfinite(value))
            throw std::runtime_error("fit::process_bin_yield: non-finite process prediction");
        return std::max(0.0, value);
    }

    void accumulate_prediction(const fit::Model &model,
                               double mu,
                               const std::vector<double> &nuisance_values,
                               std::vector<double> &signal,
                               std::vector<double> &background,
                               std::vector<double> &total)
    {
        const int nbins = model.channel->spec.nbins;
        signal.assign(static_cast<std::size_t>(nbins), 0.0);
        background.assign(static_cast<std::size_t>(nbins), 0.0);
        total.assign(static_cast<std::size_t>(nbins), 0.0);

        for (int bin = 0; bin < nbins; ++bin)
        {
            for (const auto &process : model.channel->processes)
            {
                if (process.kind == ChannelIO::ProcessKind::kData)
                    continue;

                const double value = process_bin_yield(model, process, bin, mu, nuisance_values);
                if (process.name == model.signal_process)
                    signal[static_cast<std::size_t>(bin)] += value;
                else
                    background[static_cast<std::size_t>(bin)] += value;
                total[static_cast<std::size_t>(bin)] += value;
            }

            total[static_cast<std::size_t>(bin)] =
                std::max(kYieldFloor, total[static_cast<std::size_t>(bin)]);
        }
    }

    double objective_for_coordinate(const fit::Model &model,
                                    double mu,
                                    std::vector<double> &nuisance_values,
                                    std::size_t nuisance_index,
                                    double trial_value)
    {
        nuisance_values[nuisance_index] = trial_value;
        return fit::objective(model, mu, nuisance_values);
    }

    double golden_section_coordinate(const fit::Model &model,
                                     double mu,
                                     std::vector<double> &nuisance_values,
                                     std::size_t nuisance_index)
    {
        const auto &nuisance = model.nuisances[nuisance_index];
        if (nuisance.fixed || nuisance.upper == nuisance.lower)
            return clamp_value(nuisance.start_value, nuisance.lower, nuisance.upper);

        const double phi = 0.5 * (1.0 + std::sqrt(5.0));
        double left = nuisance.lower;
        double right = nuisance.upper;
        double x1 = right - (right - left) / phi;
        double x2 = left + (right - left) / phi;
        double f1 = objective_for_coordinate(model, mu, nuisance_values, nuisance_index, x1);
        double f2 = objective_for_coordinate(model, mu, nuisance_values, nuisance_index, x2);

        for (int iter = 0; iter < kGoldenIterations; ++iter)
        {
            if (f1 <= f2)
            {
                right = x2;
                x2 = x1;
                f2 = f1;
                x1 = right - (right - left) / phi;
                f1 = objective_for_coordinate(model, mu, nuisance_values, nuisance_index, x1);
            }
            else
            {
                left = x1;
                x1 = x2;
                f1 = f2;
                x2 = left + (right - left) / phi;
                f2 = objective_for_coordinate(model, mu, nuisance_values, nuisance_index, x2);
            }
        }

        const double best = (f1 <= f2) ? x1 : x2;
        nuisance_values[nuisance_index] = best;
        return best;
    }

    ProfilePoint profile_at_mu(const fit::Model &model,
                               double mu,
                               const std::vector<double> &seed,
                               const fit::FitOptions &options,
                               bool freeze_all,
                               const std::vector<double> *frozen_values)
    {
        ProfilePoint out;
        out.mu = clamp_value(mu, model.mu_lower, model.mu_upper);
        out.nuisance_values = seed;
        if (out.nuisance_values.size() != model.nuisances.size())
            out.nuisance_values = initial_nuisance_values(model);

        for (std::size_t i = 0; i < model.nuisances.size(); ++i)
        {
            const auto &nuisance = model.nuisances[i];
            const double value = freeze_all && frozen_values
                                     ? (*frozen_values)[i]
                                     : out.nuisance_values[i];
            out.nuisance_values[i] = clamp_value(value, nuisance.lower, nuisance.upper);
        }

        if (!freeze_all)
        {
            for (int pass = 0; pass < options.nuisance_passes; ++pass)
            {
                double max_delta = 0.0;
                for (std::size_t nuisance_index = 0; nuisance_index < model.nuisances.size(); ++nuisance_index)
                {
                    const double before = out.nuisance_values[nuisance_index];
                    const double after =
                        golden_section_coordinate(model, out.mu, out.nuisance_values, nuisance_index);
                    max_delta = std::max(max_delta, std::abs(after - before));
                }

                if (max_delta < options.tolerance)
                    break;
            }
        }

        out.objective = fit::objective(model, out.mu, out.nuisance_values);
        return out;
    }

    ProfilePoint optimise_mu(const fit::Model &model,
                             const std::vector<double> &seed,
                             const fit::FitOptions &options,
                             bool freeze_all,
                             const std::vector<double> *frozen_values)
    {
        const int scan_points = std::max(8, options.scan_points);
        double best_mu = model.mu_lower;
        ProfilePoint best_point = profile_at_mu(model,
                                                best_mu,
                                                seed,
                                                options,
                                                freeze_all,
                                                frozen_values);

        std::vector<ProfilePoint> coarse_points;
        coarse_points.reserve(static_cast<std::size_t>(scan_points + 1));
        for (int i = 0; i <= scan_points; ++i)
        {
            const double fraction = static_cast<double>(i) / static_cast<double>(scan_points);
            const double mu = model.mu_lower + (model.mu_upper - model.mu_lower) * fraction;
            ProfilePoint point = profile_at_mu(model,
                                               mu,
                                               seed,
                                               options,
                                               freeze_all,
                                               frozen_values);
            coarse_points.push_back(point);
            if (point.objective < best_point.objective)
            {
                best_mu = mu;
                best_point = point;
            }
        }

        int best_index = 0;
        for (int i = 0; i <= scan_points; ++i)
        {
            if (std::abs(coarse_points[static_cast<std::size_t>(i)].mu - best_mu) < 1e-12)
            {
                best_index = i;
                break;
            }
        }

        double left = (best_index > 0)
                          ? coarse_points[static_cast<std::size_t>(best_index - 1)].mu
                          : model.mu_lower;
        double right = (best_index < scan_points)
                           ? coarse_points[static_cast<std::size_t>(best_index + 1)].mu
                           : model.mu_upper;

        const double phi = 0.5 * (1.0 + std::sqrt(5.0));
        double x1 = right - (right - left) / phi;
        double x2 = left + (right - left) / phi;
        ProfilePoint p1 = profile_at_mu(model, x1, best_point.nuisance_values, options, freeze_all, frozen_values);
        ProfilePoint p2 = profile_at_mu(model, x2, best_point.nuisance_values, options, freeze_all, frozen_values);

        for (int iter = 0; iter < kGoldenIterations; ++iter)
        {
            if (p1.objective <= p2.objective)
            {
                right = x2;
                x2 = x1;
                p2 = p1;
                x1 = right - (right - left) / phi;
                p1 = profile_at_mu(model, x1, p2.nuisance_values, options, freeze_all, frozen_values);
            }
            else
            {
                left = x1;
                x1 = x2;
                p1 = p2;
                x2 = left + (right - left) / phi;
                p2 = profile_at_mu(model, x2, p1.nuisance_values, options, freeze_all, frozen_values);
            }
        }

        return (p1.objective <= p2.objective) ? p1 : p2;
    }

    double scan_crossing(const fit::Model &model,
                         const fit::FitOptions &options,
                         const ProfilePoint &best_point,
                         bool upward,
                         bool freeze_all,
                         const std::vector<double> *frozen_values)
    {
        const double target = best_point.objective + 1.0;
        const double start = best_point.mu;
        const double bound = upward ? model.mu_upper : model.mu_lower;
        if ((upward && bound <= start) || (!upward && bound >= start))
            return 0.0;

        double previous_mu = start;
        double previous_value = best_point.objective;
        const int scan_points = std::max(16, options.scan_points);
        for (int step = 1; step <= scan_points; ++step)
        {
            const double fraction = static_cast<double>(step) / static_cast<double>(scan_points);
            const double mu = upward
                                  ? start + (bound - start) * fraction
                                  : start - (start - bound) * fraction;
            const ProfilePoint point =
                profile_at_mu(model, mu, best_point.nuisance_values, options, freeze_all, frozen_values);

            if (point.objective >= target)
            {
                double left = previous_mu;
                double right = mu;
                double left_value = previous_value;
                double right_value = point.objective;
                for (int iter = 0; iter < 32; ++iter)
                {
                    const double mid = 0.5 * (left + right);
                    const ProfilePoint mid_point =
                        profile_at_mu(model, mid, best_point.nuisance_values, options, freeze_all, frozen_values);
                    if (mid_point.objective >= target)
                    {
                        right = mid;
                        right_value = mid_point.objective;
                    }
                    else
                    {
                        left = mid;
                        left_value = mid_point.objective;
                    }

                    if (std::abs(right - left) < options.tolerance ||
                        std::abs(right_value - left_value) < options.tolerance)
                        break;
                }

                const double crossing = 0.5 * (left + right);
                return upward ? (crossing - start) : (start - crossing);
            }

            previous_mu = mu;
            previous_value = point.objective;
        }

        return 0.0;
    }
}

namespace fit
{
    const char *family_kind_name(FamilyKind family)
    {
        switch (family)
        {
            case FamilyKind::kGenie:
                return "genie";
            case FamilyKind::kFlux:
                return "flux";
            case FamilyKind::kReint:
                return "reint";
        }
        return "unknown";
    }

    Model make_independent_model(const ChannelIO::Channel &channel,
                                 const std::string &signal_process,
                                 double mu_start,
                                 double mu_upper)
    {
        Model model;
        model.channel = &channel;
        model.signal_process = signal_process;
        model.mu_start = mu_start;
        model.mu_upper = mu_upper;

        for (const auto &process : channel.processes)
        {
            if (process.kind == ChannelIO::ProcessKind::kData)
                continue;

            const std::pair<FamilyKind, const DistributionIO::Family *> families[] = {
                {FamilyKind::kGenie, &process.genie},
                {FamilyKind::kFlux, &process.flux},
                {FamilyKind::kReint, &process.reint},
            };

            for (const auto &entry : families)
            {
                const int mode_count = family_mode_count(*entry.second);
                for (int mode = 0; mode < mode_count; ++mode)
                {
                    NuisanceSpec nuisance;
                    nuisance.name = process.name + ":" + family_kind_name(entry.first) +
                                    ":mode" + std::to_string(mode);
                    nuisance.terms.push_back(
                        NuisanceTerm{process.name, entry.first, mode, 1.0});
                    model.nuisances.push_back(std::move(nuisance));
                }
            }
        }

        return model;
    }

    std::vector<double> predict_bins(const Model &model,
                                     double mu,
                                     const std::vector<double> &nuisance_values)
    {
        validate_model(model);
        std::vector<double> signal;
        std::vector<double> background;
        std::vector<double> total;
        accumulate_prediction(model, mu, nuisance_values, signal, background, total);
        return total;
    }

    double objective(const Model &model,
                     double mu,
                     const std::vector<double> &nuisance_values)
    {
        validate_model(model);
        if (nuisance_values.size() != model.nuisances.size())
            throw std::runtime_error("fit::objective: nuisance_values size does not match model.nuisances");

        std::vector<double> signal;
        std::vector<double> background;
        std::vector<double> total;
        accumulate_prediction(model, mu, nuisance_values, signal, background, total);

        double q = 0.0;
        for (int bin = 0; bin < model.channel->spec.nbins; ++bin)
        {
            const double n = model.channel->data.at(static_cast<std::size_t>(bin));
            const double lambda = total.at(static_cast<std::size_t>(bin));
            if (n > 0.0)
                q += 2.0 * (lambda - n - n * std::log(lambda / n));
            else
                q += 2.0 * lambda;
        }

        for (std::size_t i = 0; i < model.nuisances.size(); ++i)
        {
            const auto &nuisance = model.nuisances[i];
            if (!nuisance.constrained)
                continue;
            const double pull =
                (nuisance_values[i] - nuisance.prior_center) / nuisance.prior_sigma;
            q += pull * pull;
        }

        return q;
    }

    Result profile_xsec(const Model &model,
                        const FitOptions &options)
    {
        validate_model(model);

        ProfilePoint best_point;
        best_point.mu = clamp_value(model.mu_start, model.mu_lower, model.mu_upper);
        best_point.nuisance_values = initial_nuisance_values(model);
        best_point.objective = std::numeric_limits<double>::infinity();

        bool converged = false;
        for (int iter = 0; iter < options.max_iterations; ++iter)
        {
            const double previous_mu = best_point.mu;
            const double previous_objective = best_point.objective;

            best_point = optimise_mu(model,
                                     best_point.nuisance_values,
                                     options,
                                     false,
                                     nullptr);

            if (std::abs(best_point.mu - previous_mu) < options.tolerance &&
                std::abs(best_point.objective - previous_objective) < options.tolerance)
            {
                converged = true;
                break;
            }
        }

        // Make one final profiled pass at the converged mu.
        best_point = profile_at_mu(model,
                                   best_point.mu,
                                   best_point.nuisance_values,
                                   options,
                                   false,
                                   nullptr);

        Result out;
        out.converged = converged || model.nuisances.empty();
        out.objective = best_point.objective;
        out.mu_hat = best_point.mu;
        out.mu_err_total_up =
            scan_crossing(model, options, best_point, true, false, nullptr);
        out.mu_err_total_down =
            scan_crossing(model, options, best_point, false, false, nullptr);

        if (options.compute_stat_only_interval)
        {
            out.mu_err_stat_up =
                scan_crossing(model,
                              options,
                              best_point,
                              true,
                              true,
                              &best_point.nuisance_values);
            out.mu_err_stat_down =
                scan_crossing(model,
                              options,
                              best_point,
                              false,
                              true,
                              &best_point.nuisance_values);
        }

        out.nuisance_values = best_point.nuisance_values;
        out.nuisance_names.reserve(model.nuisances.size());
        for (const auto &nuisance : model.nuisances)
            out.nuisance_names.push_back(nuisance.name);

        accumulate_prediction(model,
                              best_point.mu,
                              best_point.nuisance_values,
                              out.predicted_signal,
                              out.predicted_background,
                              out.predicted_total);
        return out;
    }
}
