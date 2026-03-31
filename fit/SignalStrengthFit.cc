#include "SignalStrengthFit.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Math/Factory.h"
#include "Math/Functor.h"
#include "Math/Minimizer.h"

namespace
{
    constexpr double kYieldFloor = 1e-9;
    constexpr double kDefaultStep = 0.1;
    constexpr int kCrossingBisectionIterations = 48;

    struct ProfilePoint
    {
        bool converged = false;
        int minimizer_status = -1;
        double edm = 0.0;
        double mu = 1.0;
        double objective = std::numeric_limits<double>::infinity();
        std::vector<double> nuisance_values;
        std::vector<std::string> parameter_names;
        std::vector<double> parameter_values;
        std::vector<double> covariance;
    };

    struct IntervalEstimate
    {
        double error = std::numeric_limits<double>::quiet_NaN();
        bool found = false;
    };

    double clamp_value(double value, double lower, double upper)
    {
        return std::max(lower, std::min(value, upper));
    }

    const DistributionIO::Family &family_for(const fit::Process &process,
                                             fit::SourceKind source)
    {
        switch (source)
        {
            case fit::SourceKind::kGenieMode:
                return process.genie;
            case fit::SourceKind::kFluxMode:
                return process.flux;
            case fit::SourceKind::kReintMode:
                return process.reint;
            default:
                break;
        }
        throw std::runtime_error("fit::family_for: source is not a family mode");
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

    bool has_detector_templates(const fit::Process &process)
    {
        return process.detector_template_count > 0 &&
               !process.detector_templates.empty();
    }

    double detector_template_value(const fit::Process &process,
                                   int template_index,
                                   int bin)
    {
        if (!has_detector_templates(process))
            throw std::runtime_error("fit::detector_template_value: process has no detector templates");
        if (template_index < 0 || template_index >= process.detector_template_count)
            throw std::runtime_error("fit::detector_template_value: template_index out of range");

        const std::size_t index =
            static_cast<std::size_t>(template_index * process.nominal.size() + static_cast<std::size_t>(bin));
        if (index >= process.detector_templates.size())
            throw std::runtime_error("fit::detector_template_value: detector template payload is truncated");
        return process.detector_templates[index];
    }

    bool has_detector_envelope(const fit::Process &process)
    {
        return !process.detector_down.empty() &&
               !process.detector_up.empty();
    }

    bool has_total_envelope(const fit::Process &process)
    {
        return !process.total_down.empty() &&
               !process.total_up.empty();
    }

    double envelope_morphed_value(const std::vector<double> &nominal,
                                  const std::vector<double> &down,
                                  const std::vector<double> &up,
                                  int bin,
                                  double theta)
    {
        const double nominal_value = nominal.at(static_cast<std::size_t>(bin));
        const double down_value = down.at(static_cast<std::size_t>(bin));
        const double up_value = up.at(static_cast<std::size_t>(bin));

        // Interpolate the explicit down / nominal / up templates with a smooth
        // quadratic inside [-1, 1], then continue with linear tails.
        const double a = 0.5 * (up_value + down_value - 2.0 * nominal_value);
        const double b = 0.5 * (up_value - down_value);
        if (theta >= -1.0 && theta <= 1.0)
            return nominal_value + b * theta + a * theta * theta;

        const double slope_up = b + 2.0 * a;
        const double slope_down = b - 2.0 * a;
        if (theta > 1.0)
            return up_value + (theta - 1.0) * slope_up;
        return down_value + (theta + 1.0) * slope_down;
    }

    double envelope_shift(const std::vector<double> &nominal,
                          const std::vector<double> &down,
                          const std::vector<double> &up,
                          int bin,
                          double theta)
    {
        return envelope_morphed_value(nominal, down, up, bin, theta) -
               nominal.at(static_cast<std::size_t>(bin));
    }

    std::string mode_name(const char *label,
                          const DistributionIO::Family &family,
                          int mode_index)
    {
        std::string name(label);
        if (!family.branch_name.empty())
        {
            name += ":";
            name += family.branch_name;
        }
        name += ":mode";
        name += std::to_string(mode_index);
        return name;
    }

    std::string detector_name(const fit::Process &process, int template_index)
    {
        if (template_index >= 0 &&
            static_cast<std::size_t>(template_index) < process.detector_sample_keys.size() &&
            !process.detector_sample_keys[static_cast<std::size_t>(template_index)].empty())
        {
            return "detector:" + process.detector_sample_keys[static_cast<std::size_t>(template_index)];
        }

        return "detector:" + process.name + ":template" + std::to_string(template_index);
    }

    std::string detector_group_name(const fit::Process &process)
    {
        std::string name = "detector";
        bool first = true;
        for (const auto &key : process.detector_sample_keys)
        {
            if (key.empty())
                continue;
            name += first ? ":" : "+";
            name += key;
            first = false;
        }
        if (first)
        {
            name += ":";
            name += process.name;
        }
        return name;
    }

    std::vector<double> initial_nuisance_values(const fit::Problem &problem)
    {
        std::vector<double> values;
        values.reserve(problem.nuisances.size());
        for (const auto &nuisance : problem.nuisances)
            values.push_back(clamp_value(nuisance.start_value, nuisance.lower, nuisance.upper));
        return values;
    }

    void fill_covariance(ROOT::Math::Minimizer &minimizer,
                         int n_parameters,
                         std::vector<double> &covariance)
    {
        covariance.assign(static_cast<std::size_t>(n_parameters * n_parameters), 0.0);
        for (int row = 0; row < n_parameters; ++row)
        {
            for (int col = 0; col < n_parameters; ++col)
            {
                covariance[static_cast<std::size_t>(row * n_parameters + col)] =
                    minimizer.CovMatrix(static_cast<unsigned int>(row),
                                        static_cast<unsigned int>(col));
            }
        }
    }

    void validate_problem(const fit::Problem &problem)
    {
        if (!problem.channel)
            throw std::runtime_error("fit::validate_problem: problem.channel must not be null");
        if (problem.signal_process.empty())
            throw std::runtime_error("fit::validate_problem: signal_process must not be empty");
        if (problem.mu_upper < problem.mu_lower)
            throw std::runtime_error("fit::validate_problem: mu bounds are inverted");
        if (problem.channel->data.size() != static_cast<std::size_t>(problem.channel->spec.nbins))
            throw std::runtime_error("fit::validate_problem: channel data size does not match nbins");
        if (!problem.channel->find_process(problem.signal_process))
            throw std::runtime_error("fit::validate_problem: signal process is not present in the channel");

        for (const auto &process : problem.channel->processes)
        {
            if (process.kind == fit::ProcessKind::kData)
                continue;

            const std::size_t nbins = static_cast<std::size_t>(problem.channel->spec.nbins);
            if (process.nominal.size() != nbins)
                throw std::runtime_error("fit::validate_problem: process nominal size does not match nbins");
            if (!process.sumw2.empty() && process.sumw2.size() != nbins)
                throw std::runtime_error("fit::validate_problem: process sumw2 size does not match nbins");
            if (has_detector_envelope(process) &&
                (process.detector_down.size() != nbins || process.detector_up.size() != nbins))
            {
                throw std::runtime_error("fit::validate_problem: detector envelope size does not match nbins");
            }
            if (has_total_envelope(process) &&
                (process.total_down.size() != nbins || process.total_up.size() != nbins))
            {
                throw std::runtime_error("fit::validate_problem: total envelope size does not match nbins");
            }
            if (has_detector_templates(process))
            {
                const std::size_t expected =
                    static_cast<std::size_t>(process.detector_template_count) * nbins;
                if (process.detector_templates.size() != expected)
                    throw std::runtime_error("fit::validate_problem: detector template payload is truncated");
            }
            if (has_detector_templates(process) &&
                !process.detector_sample_keys.empty() &&
                static_cast<int>(process.detector_sample_keys.size()) != process.detector_template_count)
            {
                throw std::runtime_error("fit::validate_problem: detector_sample_keys size does not match detector_template_count");
            }
        }

        for (const auto &nuisance : problem.nuisances)
        {
            if (nuisance.upper < nuisance.lower)
                throw std::runtime_error("fit::validate_problem: nuisance bounds are inverted");
            if (nuisance.constrained && nuisance.prior_sigma <= 0.0)
                throw std::runtime_error("fit::validate_problem: constrained nuisance prior_sigma must be positive");

            for (const auto &term : nuisance.terms)
            {
                const fit::Process *process = problem.channel->find_process(term.process_name);
                if (!process)
                    throw std::runtime_error("fit::validate_problem: nuisance term references unknown process");

                switch (term.source)
                {
                    case fit::SourceKind::kGenieMode:
                    case fit::SourceKind::kFluxMode:
                    case fit::SourceKind::kReintMode:
                    {
                        const DistributionIO::Family &family = family_for(*process, term.source);
                        const int mode_count = family_mode_count(family);
                        if (term.index < 0 || term.index >= mode_count)
                            throw std::runtime_error("fit::validate_problem: family nuisance term mode_index out of range");
                        break;
                    }
                    case fit::SourceKind::kDetectorTemplate:
                        if (!has_detector_templates(*process) ||
                            term.index < 0 ||
                            term.index >= process->detector_template_count)
                        {
                            throw std::runtime_error("fit::validate_problem: detector template nuisance term out of range");
                        }
                        break;
                    case fit::SourceKind::kDetectorEnvelope:
                        if (!has_detector_envelope(*process))
                            throw std::runtime_error("fit::validate_problem: detector envelope nuisance term references missing payload");
                        break;
                    case fit::SourceKind::kStatBin:
                        if (term.index < 0 ||
                            static_cast<std::size_t>(term.index) >= process->sumw2.size())
                        {
                            throw std::runtime_error("fit::validate_problem: stat nuisance term bin out of range");
                        }
                        break;
                    case fit::SourceKind::kTotalEnvelope:
                        if (!has_total_envelope(*process))
                            throw std::runtime_error("fit::validate_problem: total-envelope nuisance term references missing payload");
                        break;
                }
            }
        }
    }

    double term_shift(const fit::Process &process,
                      const fit::ShiftTerm &term,
                      int bin,
                      double theta)
    {
        switch (term.source)
        {
            case fit::SourceKind::kGenieMode:
            case fit::SourceKind::kFluxMode:
            case fit::SourceKind::kReintMode:
            {
                const DistributionIO::Family &family = family_for(process, term.source);
                return term.coefficient *
                       family_mode_value(family, term.index, bin) *
                       theta;
            }
            case fit::SourceKind::kDetectorTemplate:
            {
                const double nominal = process.nominal.at(static_cast<std::size_t>(bin));
                const double varied = detector_template_value(process, term.index, bin);
                return term.coefficient * (varied - nominal) * theta;
            }
            case fit::SourceKind::kDetectorEnvelope:
                return term.coefficient *
                       envelope_shift(process.nominal,
                                      process.detector_down,
                                      process.detector_up,
                                      bin,
                                      theta);
            case fit::SourceKind::kStatBin:
                if (term.index != bin)
                    return 0.0;
                return term.coefficient *
                       std::sqrt(std::max(0.0, process.sumw2.at(static_cast<std::size_t>(bin)))) *
                       theta;
            case fit::SourceKind::kTotalEnvelope:
                return term.coefficient *
                       envelope_shift(process.nominal,
                                      process.total_down,
                                      process.total_up,
                                      bin,
                                      theta);
        }

        throw std::runtime_error("fit::term_shift: unknown nuisance source");
    }

    double process_bin_yield(const fit::Problem &problem,
                             const fit::Process &process,
                             int bin,
                             double mu,
                             const double *nuisance_values)
    {
        double value = process.nominal.at(static_cast<std::size_t>(bin));

        for (std::size_t nuisance_index = 0; nuisance_index < problem.nuisances.size(); ++nuisance_index)
        {
            const double theta = nuisance_values ? nuisance_values[nuisance_index] : 0.0;
            const auto &nuisance = problem.nuisances[nuisance_index];
            for (const auto &term : nuisance.terms)
            {
                if (term.process_name != process.name)
                    continue;
                value += term_shift(process, term, bin, theta);
            }
        }

        if (process.name == problem.signal_process)
            value *= mu;

        if (!std::isfinite(value))
            throw std::runtime_error("fit::process_bin_yield: non-finite process prediction");
        return std::max(0.0, value);
    }

    void accumulate_prediction(const fit::Problem &problem,
                               double mu,
                               const double *nuisance_values,
                               std::vector<double> &signal,
                               std::vector<double> &background,
                               std::vector<double> &total)
    {
        const int nbins = problem.channel->spec.nbins;
        signal.assign(static_cast<std::size_t>(nbins), 0.0);
        background.assign(static_cast<std::size_t>(nbins), 0.0);
        total.assign(static_cast<std::size_t>(nbins), 0.0);

        for (int bin = 0; bin < nbins; ++bin)
        {
            for (const auto &process : problem.channel->processes)
            {
                if (process.kind == fit::ProcessKind::kData)
                    continue;

                const double value = process_bin_yield(problem, process, bin, mu, nuisance_values);
                if (process.name == problem.signal_process)
                    signal[static_cast<std::size_t>(bin)] += value;
                else
                    background[static_cast<std::size_t>(bin)] += value;
                total[static_cast<std::size_t>(bin)] += value;
            }

            total[static_cast<std::size_t>(bin)] =
                std::max(kYieldFloor, total[static_cast<std::size_t>(bin)]);
        }
    }

    double objective_unchecked(const fit::Problem &problem,
                               double mu,
                               const double *nuisance_values)
    {
        std::vector<double> signal;
        std::vector<double> background;
        std::vector<double> total;
        accumulate_prediction(problem, mu, nuisance_values, signal, background, total);

        double q = 0.0;
        for (int bin = 0; bin < problem.channel->spec.nbins; ++bin)
        {
            const double n = problem.channel->data.at(static_cast<std::size_t>(bin));
            const double lambda = total.at(static_cast<std::size_t>(bin));
            if (n > 0.0)
                q += 2.0 * (lambda - n - n * std::log(lambda / n));
            else
                q += 2.0 * lambda;
        }

        for (std::size_t i = 0; i < problem.nuisances.size(); ++i)
        {
            const auto &nuisance = problem.nuisances[i];
            if (!nuisance.constrained)
                continue;
            const double theta = nuisance_values ? nuisance_values[i] : 0.0;
            const double pull = (theta - nuisance.prior_center) / nuisance.prior_sigma;
            q += pull * pull;
        }

        return q;
    }

    class ProfileObjective
    {
    public:
        ProfileObjective(const fit::Problem &problem, double mu)
            : problem_(problem), mu_(mu)
        {
        }

        double operator()(const double *parameters) const
        {
            return objective_unchecked(problem_, mu_, parameters);
        }

    private:
        const fit::Problem &problem_;
        double mu_ = 1.0;
    };

    class JointObjective
    {
    public:
        explicit JointObjective(const fit::Problem &problem)
            : problem_(problem)
        {
        }

        double operator()(const double *parameters) const
        {
            const std::size_t n_nuisances = problem_.nuisances.size();
            const double mu = parameters[n_nuisances];
            return objective_unchecked(problem_, mu, parameters);
        }

    private:
        const fit::Problem &problem_;
    };

    std::unique_ptr<ROOT::Math::Minimizer> make_minimizer(const fit::FitOptions &options)
    {
        std::unique_ptr<ROOT::Math::Minimizer> minimizer(
            ROOT::Math::Factory::CreateMinimizer("Minuit2", "Migrad"));
        if (!minimizer)
            throw std::runtime_error("fit::make_minimizer: failed to create Minuit2 minimizer");

        minimizer->SetMaxIterations(options.max_iterations);
        minimizer->SetMaxFunctionCalls(options.max_function_calls);
        minimizer->SetTolerance(options.tolerance);
        minimizer->SetPrintLevel(options.print_level);
        minimizer->SetStrategy(options.strategy);
        minimizer->SetErrorDef(1.0);
        return minimizer;
    }

    void set_nuisance_variable(ROOT::Math::Minimizer &minimizer,
                               unsigned int index,
                               const fit::Nuisance &nuisance,
                               double value)
    {
        const double start = clamp_value(value, nuisance.lower, nuisance.upper);
        const double step = nuisance.step > 0.0 ? nuisance.step : kDefaultStep;

        if (nuisance.fixed || nuisance.upper == nuisance.lower)
        {
            minimizer.SetFixedVariable(index, nuisance.name.c_str(), start);
            return;
        }

        minimizer.SetLimitedVariable(index,
                                     nuisance.name.c_str(),
                                     start,
                                     step,
                                     nuisance.lower,
                                     nuisance.upper);
    }

    ProfilePoint minimise_at_mu(const fit::Problem &problem,
                                double mu,
                                const std::vector<double> &seed,
                                const fit::FitOptions &options)
    {
        ProfilePoint out;
        out.mu = clamp_value(mu, problem.mu_lower, problem.mu_upper);
        out.nuisance_values =
            seed.size() == problem.nuisances.size() ? seed : initial_nuisance_values(problem);
        out.parameter_names.reserve(problem.nuisances.size());

        if (problem.nuisances.empty())
        {
            out.converged = true;
            out.minimizer_status = 0;
            out.objective = objective_unchecked(problem, out.mu, nullptr);
            return out;
        }

        std::unique_ptr<ROOT::Math::Minimizer> minimizer = make_minimizer(options);
        ProfileObjective objective(problem, out.mu);
        ROOT::Math::Functor functor(objective, static_cast<unsigned int>(problem.nuisances.size()));
        minimizer->SetFunction(functor);

        for (std::size_t i = 0; i < problem.nuisances.size(); ++i)
        {
            set_nuisance_variable(*minimizer,
                                  static_cast<unsigned int>(i),
                                  problem.nuisances[i],
                                  out.nuisance_values[i]);
            out.parameter_names.push_back(problem.nuisances[i].name);
        }

        out.converged = minimizer->Minimize();
        out.minimizer_status = minimizer->Status();
        out.edm = minimizer->Edm();
        out.objective = minimizer->MinValue();

        const double *parameters = minimizer->X();
        out.nuisance_values.assign(parameters, parameters + problem.nuisances.size());
        out.parameter_values = out.nuisance_values;

        if (options.run_hesse)
        {
            minimizer->Hesse();
            fill_covariance(*minimizer,
                            static_cast<int>(problem.nuisances.size()),
                            out.covariance);
        }

        return out;
    }

    ProfilePoint frozen_at_mu(const fit::Problem &problem,
                              double mu,
                              const std::vector<double> &frozen_values)
    {
        ProfilePoint out;
        out.mu = clamp_value(mu, problem.mu_lower, problem.mu_upper);
        out.converged = true;
        out.minimizer_status = 0;
        out.nuisance_values =
            frozen_values.size() == problem.nuisances.size() ? frozen_values : initial_nuisance_values(problem);
        out.objective = objective_unchecked(problem, out.mu, out.nuisance_values.data());
        return out;
    }

    ProfilePoint minimise_joint(const fit::Problem &problem,
                                const fit::FitOptions &options)
    {
        ProfilePoint out;
        out.mu = clamp_value(problem.mu_start, problem.mu_lower, problem.mu_upper);
        out.nuisance_values = initial_nuisance_values(problem);
        out.parameter_names.reserve(problem.nuisances.size() + 1);

        std::unique_ptr<ROOT::Math::Minimizer> minimizer = make_minimizer(options);
        JointObjective objective(problem);
        const unsigned int n_parameters =
            static_cast<unsigned int>(problem.nuisances.size() + 1);
        ROOT::Math::Functor functor(objective, n_parameters);
        minimizer->SetFunction(functor);

        for (std::size_t i = 0; i < problem.nuisances.size(); ++i)
        {
            set_nuisance_variable(*minimizer,
                                  static_cast<unsigned int>(i),
                                  problem.nuisances[i],
                                  out.nuisance_values[i]);
            out.parameter_names.push_back(problem.nuisances[i].name);
        }

        const double mu_range = std::max(problem.mu_upper - problem.mu_lower, 1.0);
        const double mu_step = std::max(kDefaultStep, 0.05 * mu_range);
        minimizer->SetLimitedVariable(static_cast<unsigned int>(problem.nuisances.size()),
                                      "mu",
                                      out.mu,
                                      mu_step,
                                      problem.mu_lower,
                                      problem.mu_upper);
        out.parameter_names.push_back("mu");

        out.converged = minimizer->Minimize();
        out.minimizer_status = minimizer->Status();
        out.edm = minimizer->Edm();
        out.objective = minimizer->MinValue();

        const double *parameters = minimizer->X();
        out.nuisance_values.assign(parameters, parameters + problem.nuisances.size());
        out.mu = parameters[problem.nuisances.size()];
        out.parameter_values.assign(parameters, parameters + n_parameters);

        if (options.run_hesse)
        {
            minimizer->Hesse();
            fill_covariance(*minimizer,
                            static_cast<int>(n_parameters),
                            out.covariance);
        }

        return out;
    }

    IntervalEstimate scan_crossing(const fit::Problem &problem,
                                   const fit::FitOptions &options,
                                   const ProfilePoint &best_point,
                                   bool upward,
                                   bool freeze_nuisances)
    {
        IntervalEstimate out;
        const double target = best_point.objective + 1.0;
        const double start = best_point.mu;
        const double bound = upward ? problem.mu_upper : problem.mu_lower;

        if ((upward && bound <= start) || (!upward && bound >= start))
            return out;

        ProfilePoint previous_point = best_point;
        const int scan_points = std::max(16, options.scan_points);
        for (int step = 1; step <= scan_points; ++step)
        {
            const double fraction = static_cast<double>(step) / static_cast<double>(scan_points);
            const double mu = upward
                                  ? start + (bound - start) * fraction
                                  : start - (start - bound) * fraction;

            ProfilePoint point = freeze_nuisances
                                     ? frozen_at_mu(problem, mu, best_point.nuisance_values)
                                     : minimise_at_mu(problem, mu, previous_point.nuisance_values, options);

            if (point.objective >= target)
            {
                ProfilePoint left_point = previous_point;
                ProfilePoint right_point = point;

                for (int iter = 0; iter < kCrossingBisectionIterations; ++iter)
                {
                    const double mid_mu = 0.5 * (left_point.mu + right_point.mu);
                    const std::vector<double> &seed =
                        freeze_nuisances ? best_point.nuisance_values : left_point.nuisance_values;

                    ProfilePoint mid_point = freeze_nuisances
                                                 ? frozen_at_mu(problem, mid_mu, best_point.nuisance_values)
                                                 : minimise_at_mu(problem, mid_mu, seed, options);

                    if (mid_point.objective >= target)
                        right_point = mid_point;
                    else
                        left_point = mid_point;

                    if (std::abs(right_point.mu - left_point.mu) < options.tolerance)
                        break;
                }

                const double crossing = 0.5 * (left_point.mu + right_point.mu);
                out.error = upward ? (crossing - start) : (start - crossing);
                out.found = true;
                return out;
            }

            previous_point = point;
        }

        return out;
    }

    std::size_t ensure_nuisance(fit::Problem &problem,
                                std::map<std::string, std::size_t> &indices,
                                const std::string &name)
    {
        const auto it = indices.find(name);
        if (it != indices.end())
            return it->second;

        fit::Nuisance nuisance;
        nuisance.name = name;
        problem.nuisances.push_back(std::move(nuisance));
        const std::size_t index = problem.nuisances.size() - 1;
        indices[name] = index;
        return index;
    }

    bool process_has_family_payload(const fit::Process &process)
    {
        return family_mode_count(process.genie) > 0 ||
               family_mode_count(process.flux) > 0 ||
               family_mode_count(process.reint) > 0;
    }

    bool process_has_detector_payload(const fit::Process &process)
    {
        return has_detector_templates(process) || has_detector_envelope(process);
    }
}

namespace fit
{
    const char *source_kind_name(SourceKind source)
    {
        switch (source)
        {
            case SourceKind::kGenieMode:
                return "genie";
            case SourceKind::kFluxMode:
                return "flux";
            case SourceKind::kReintMode:
                return "reint";
            case SourceKind::kDetectorTemplate:
                return "detector_template";
            case SourceKind::kDetectorEnvelope:
                return "detector_envelope";
            case SourceKind::kStatBin:
                return "stat_bin";
            case SourceKind::kTotalEnvelope:
                return "total_envelope";
        }

        return "unknown";
    }

    const fit::Process *Channel::find_process(const std::string &name) const
    {
        for (const auto &process : processes)
        {
            if (process.name == name)
                return &process;
        }
        return nullptr;
    }

    Process *Channel::find_process(const std::string &name)
    {
        for (auto &process : processes)
        {
            if (process.name == name)
                return &process;
        }
        return nullptr;
    }

    Problem make_independent_problem(const Channel &channel,
                                     const std::string &signal_process,
                                     double mu_start,
                                     double mu_upper)
    {
        Problem problem;
        problem.channel = &channel;
        problem.signal_process = signal_process;
        problem.mu_start = mu_start;
        problem.mu_upper = mu_upper;

        std::map<std::string, std::size_t> nuisance_indices;

        for (const auto &process : channel.processes)
        {
            if (process.kind == ProcessKind::kData)
                continue;

            const std::pair<SourceKind, const DistributionIO::Family *> families[] = {
                {SourceKind::kGenieMode, &process.genie},
                {SourceKind::kFluxMode, &process.flux},
                {SourceKind::kReintMode, &process.reint},
            };

            for (const auto &entry : families)
            {
                const int mode_count = family_mode_count(*entry.second);
                for (int mode = 0; mode < mode_count; ++mode)
                {
                    const std::string name = mode_name(source_kind_name(entry.first),
                                                       *entry.second,
                                                       mode);
                    const std::size_t nuisance_index =
                        ensure_nuisance(problem, nuisance_indices, name);
                    problem.nuisances[nuisance_index].terms.push_back(
                        ShiftTerm{process.name, entry.first, mode, 1.0});
                }
            }

            if (has_detector_templates(process))
            {
                for (int row = 0; row < process.detector_template_count; ++row)
                {
                    const std::string name = detector_name(process, row);
                    const std::size_t nuisance_index =
                        ensure_nuisance(problem, nuisance_indices, name);
                    problem.nuisances[nuisance_index].terms.push_back(
                        ShiftTerm{process.name, SourceKind::kDetectorTemplate, row, 1.0});
                }
            }
            else if (has_detector_envelope(process))
            {
                const std::string name = detector_group_name(process);
                const std::size_t nuisance_index =
                    ensure_nuisance(problem, nuisance_indices, name);
                problem.nuisances[nuisance_index].terms.push_back(
                    ShiftTerm{process.name, SourceKind::kDetectorEnvelope, 0, 1.0});
            }

            for (std::size_t bin = 0; bin < process.sumw2.size(); ++bin)
            {
                if (process.sumw2[bin] <= 0.0)
                    continue;

                fit::Nuisance nuisance;
                nuisance.name = "stat:" + process.name + ":bin" + std::to_string(bin);
                nuisance.terms.push_back(
                    ShiftTerm{process.name, SourceKind::kStatBin, static_cast<int>(bin), 1.0});
                problem.nuisances.push_back(std::move(nuisance));
            }

            if (!process_has_family_payload(process) &&
                !process_has_detector_payload(process) &&
                has_total_envelope(process))
            {
                fit::Nuisance nuisance;
                nuisance.name = "total:" + process.name;
                nuisance.terms.push_back(
                    ShiftTerm{process.name, SourceKind::kTotalEnvelope, 0, 1.0});
                problem.nuisances.push_back(std::move(nuisance));
            }
        }

        return problem;
    }

    std::vector<double> predict_bins(const Problem &problem,
                                     double mu,
                                     const std::vector<double> &nuisance_values)
    {
        validate_problem(problem);
        if (nuisance_values.size() != problem.nuisances.size())
            throw std::runtime_error("fit::predict_bins: nuisance_values size does not match problem.nuisances");

        std::vector<double> signal;
        std::vector<double> background;
        std::vector<double> total;
        accumulate_prediction(problem, mu, nuisance_values.data(), signal, background, total);
        return total;
    }

    double objective(const Problem &problem,
                     double mu,
                     const std::vector<double> &nuisance_values)
    {
        validate_problem(problem);
        if (nuisance_values.size() != problem.nuisances.size())
            throw std::runtime_error("fit::objective: nuisance_values size does not match problem.nuisances");

        return objective_unchecked(problem, mu, nuisance_values.data());
    }

    Result profile_signal_strength(const Problem &problem,
                                   const FitOptions &options)
    {
        validate_problem(problem);

        ProfilePoint best_point = minimise_joint(problem, options);

        const IntervalEstimate total_up =
            scan_crossing(problem, options, best_point, true, false);
        const IntervalEstimate total_down =
            scan_crossing(problem, options, best_point, false, false);

        IntervalEstimate stat_up;
        IntervalEstimate stat_down;
        if (options.compute_stat_only_interval)
        {
            stat_up = scan_crossing(problem, options, best_point, true, true);
            stat_down = scan_crossing(problem, options, best_point, false, true);
        }

        Result out;
        out.converged = best_point.converged;
        out.minimizer_status = best_point.minimizer_status;
        out.edm = best_point.edm;
        out.objective = best_point.objective;
        out.mu_hat = best_point.mu;
        out.mu_err_total_up = total_up.error;
        out.mu_err_total_down = total_down.error;
        out.mu_err_stat_up = stat_up.error;
        out.mu_err_stat_down = stat_down.error;
        out.mu_err_total_up_found = total_up.found;
        out.mu_err_total_down_found = total_down.found;
        out.mu_err_stat_up_found = stat_up.found;
        out.mu_err_stat_down_found = stat_down.found;
        out.nuisance_values = best_point.nuisance_values;
        out.parameter_names = best_point.parameter_names;
        out.parameter_values = best_point.parameter_values;
        out.covariance = best_point.covariance;

        out.nuisance_names.reserve(problem.nuisances.size());
        for (const auto &nuisance : problem.nuisances)
            out.nuisance_names.push_back(nuisance.name);

        accumulate_prediction(problem,
                              best_point.mu,
                              best_point.nuisance_values.data(),
                              out.predicted_signal,
                              out.predicted_background,
                              out.predicted_total);
        return out;
    }
}
