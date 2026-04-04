#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "SignalStrengthFit.hh"

namespace
{
    [[noreturn]] void fail(const std::string &message)
    {
        throw std::runtime_error("fit_rigorous_check: " + message);
    }

    void require(bool condition, const std::string &message)
    {
        if (!condition)
            fail(message);
    }

    bool approx(double lhs, double rhs, double tolerance = 1e-6)
    {
        return std::fabs(lhs - rhs) <= tolerance;
    }

    void require_close(double actual,
                       double expected,
                       const std::string &label,
                       double tolerance = 1e-6)
    {
        if (!approx(actual, expected, tolerance))
        {
            fail(label + ": expected " + std::to_string(expected) +
                 ", got " + std::to_string(actual));
        }
    }

    void require_close_vector(const std::vector<double> &actual,
                              const std::vector<double> &expected,
                              const std::string &label,
                              double tolerance = 1e-6)
    {
        require(actual.size() == expected.size(),
                label + ": size mismatch");
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            require_close(actual[i],
                          expected[i],
                          label + "[" + std::to_string(i) + "]",
                          tolerance);
        }
    }

    void require_throws(const std::function<void()> &fn,
                        const std::string &needle,
                        const std::string &label)
    {
        try
        {
            fn();
        }
        catch (const std::exception &error)
        {
            const std::string message = error.what();
            if (message.find(needle) == std::string::npos)
                fail(label + ": unexpected exception message: " + message);
            return;
        }

        fail(label + ": expected an exception");
    }

    fit::Spec make_spec(int nbins = 1)
    {
        fit::Spec spec;
        spec.channel_key = "channel";
        spec.branch_expr = "x";
        spec.selection_expr = "1";
        spec.nbins = nbins;
        spec.xmin = 0.0;
        spec.xmax = static_cast<double>(nbins);
        return spec;
    }

    fit::Process make_process(const std::string &name,
                              fit::ProcessKind kind,
                              const std::vector<double> &nominal)
    {
        fit::Process process;
        process.name = name;
        process.kind = kind;
        process.source_keys = {name};
        process.nominal = nominal;
        return process;
    }

    fit::Channel make_simple_channel()
    {
        fit::Channel channel;
        channel.spec = make_spec();
        channel.data = {15.0};
        channel.processes.push_back(
            make_process("signal", fit::ProcessKind::kSignal, {10.0}));
        channel.processes.push_back(
            make_process("background", fit::ProcessKind::kBackground, {5.0}));
        return channel;
    }

    fit::Channel make_family_channel(const fit::Family &family)
    {
        fit::Channel channel;
        channel.spec = make_spec(2);
        channel.data = {15.0, 12.0};

        fit::Process signal =
            make_process("signal", fit::ProcessKind::kSignal, {10.0, 8.0});
        signal.genie = family;
        channel.processes.push_back(signal);
        channel.processes.push_back(
            make_process("background", fit::ProcessKind::kBackground, {5.0, 4.0}));
        return channel;
    }

    fit::Result run_problem(const fit::Channel &channel,
                            const std::string &measurement_name)
    {
        fit::Problem problem = fit::make_independent_problem(channel, 1.0, 5.0);
        problem.measurement_name = measurement_name;
        problem.mu_lower = 0.0;
        problem.mu_upper = 5.0;
        return fit::profile_signal_strength(problem);
    }

    int count_names_containing(const std::vector<std::string> &names,
                               const std::string &needle)
    {
        return static_cast<int>(std::count_if(
            names.begin(),
            names.end(),
            [&](const std::string &name)
            {
                return name.find(needle) != std::string::npos;
            }));
    }

    void test_simple_profile_and_prediction()
    {
        const fit::Result result =
            run_problem(make_simple_channel(), "fit_rigorous_simple");

        require(result.converged, "simple fit should converge");
        require(result.minimizer_status == 0, "simple fit minimizer status");
        require_close(result.mu_hat, 1.0, "simple fit mu_hat", 1e-3);
        require(result.nuisance_names.empty(),
                "simple fit should not create nuisance parameters");
        require(count_names_containing(result.parameter_names, "mu") == 1,
                "simple fit should expose the POI");
        require_close_vector(result.predicted_signal,
                             {10.0},
                             "simple fit predicted signal",
                             1e-3);
        require_close_vector(result.predicted_background,
                             {5.0},
                             "simple fit predicted background",
                             1e-3);
        require_close_vector(result.predicted_total,
                             {15.0},
                             "simple fit predicted total",
                             1e-3);
    }

    void test_covariance_first_family_modes()
    {
        fit::Family family;
        family.branch_name = "weightsGenie";
        family.covariance = {
            1.0, 0.8,
            0.8, 1.0,
        };

        const fit::Result result =
            run_problem(make_family_channel(family), "fit_rigorous_covariance");

        require(result.converged, "covariance-backed family fit should converge");
        require(result.minimizer_status == 0,
                "covariance-backed family fit minimizer status");
        require(result.nuisance_names.size() == 2,
                "family covariance should materialise two mode nuisances");
        require(count_names_containing(result.nuisance_names, "weightsGenie") == 2,
                "covariance-derived nuisances should retain the family branch name");
    }

    void test_sigma_fallback_is_diagonal()
    {
        fit::Family family;
        family.branch_name = "weightsGenie";
        family.sigma = {1.0, 2.0};

        const fit::Result result =
            run_problem(make_family_channel(family), "fit_rigorous_sigma");

        require(result.converged, "sigma-backed family fit should converge");
        require(result.minimizer_status == 0,
                "sigma-backed family fit minimizer status");
        require(result.nuisance_names.size() == 2,
                "sigma-only family fallback should create one diagonal nuisance per bin");
        require(count_names_containing(result.nuisance_names, "weightsGenie") == 2,
                "sigma-only nuisances should retain the family branch name");
    }

    void test_validate_problem_rejects_malformed_family_inputs()
    {
        {
            fit::Family family;
            family.covariance = {
                1.0, 0.8,
                0.8, 1.0,
            };

            require_throws(
                [&]()
                {
                    (void)run_problem(make_family_channel(family),
                                      "fit_rigorous_missing_branch");
                },
                "branch_name is required",
                "family payloads should require a branch_name");
        }

        {
            fit::Family family;
            family.branch_name = "weightsGenie";
            family.covariance = {1.0};

            require_throws(
                [&]()
                {
                    (void)run_problem(make_family_channel(family),
                                      "fit_rigorous_bad_covariance");
                },
                "covariance size does not match channel bin count",
                "truncated family covariance should fail");
        }
    }
}

int main()
{
    try
    {
        test_simple_profile_and_prediction();
        test_covariance_first_family_modes();
        test_sigma_fallback_is_diagonal();
        test_validate_problem_rejects_malformed_family_inputs();
        std::cout << "fit_rigorous_check=ok\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
