#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
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

    void test_simple_profile_and_prediction()
    {
        fit::Channel channel = make_simple_channel();
        fit::Problem problem = fit::make_independent_problem(channel, "signal", 1.0, 3.0);
        problem.mu_lower = 0.0;
        problem.mu_upper = 3.0;

        require(problem.nuisances.empty(),
                "simple problem should not create nuisances");

        require_close_vector(fit::predict_bins(problem, 1.0, {}),
                             {15.0},
                             "predict_bins(mu=1)");
        require_close_vector(fit::predict_bins(problem, 0.5, {}),
                             {10.0},
                             "predict_bins(mu=0.5)");
        require_close(fit::objective(problem, 1.0, {}),
                      0.0,
                      "objective at the exact prediction");

        fit::FitOptions options;
        options.scan_points = 24;
        options.run_hesse = true;
        const fit::Result result = fit::profile_signal_strength(problem, options);

        require(result.converged, "simple fit should converge");
        require(result.minimizer_status == 0, "simple fit minimizer status");
        require_close(result.mu_hat, 1.0, "simple fit mu_hat", 1e-3);
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
        require(result.parameter_names.size() == 1 &&
                    result.parameter_names.front() == "mu",
                "simple fit should expose only the mu parameter");
    }

    void test_independent_problem_builder_and_shared_nuisances()
    {
        fit::Channel channel;
        channel.spec = make_spec(2);
        channel.data = {0.0, 0.0};

        fit::Process signal = make_process("signal", fit::ProcessKind::kSignal, {10.0, 8.0});
        signal.detector_source_labels = {"sce"};
        signal.detector_shift_vectors = {2.0, 4.0};
        signal.detector_source_count = 1;
        signal.genie_knob_source_labels = {"agky"};
        signal.genie_knob_shift_vectors = {0.5, 1.0};
        signal.genie_knob_source_count = 1;
        signal.genie.branch_name = "weightsGenie";
        signal.genie.sigma = {0.1, 0.2};

        fit::Process background = make_process("background",
                                               fit::ProcessKind::kBackground,
                                               {5.0, 4.0});
        background.detector_source_labels = {"sce"};
        background.detector_shift_vectors = {1.0, 1.0};
        background.detector_source_count = 1;
        background.genie_knob_source_labels = {"agky"};
        background.genie_knob_shift_vectors = {0.25, 0.5};
        background.genie_knob_source_count = 1;
        background.genie.branch_name = "weightsGenie";
        background.genie.sigma = {0.3, 0.4};

        fit::Process aux = make_process("aux",
                                        fit::ProcessKind::kBackground,
                                        {1.0, 1.0});
        aux.total_down = {0.5, 0.5};
        aux.total_up = {1.5, 1.5};

        channel.processes = {signal, background, aux};

        fit::Problem problem = fit::make_independent_problem(channel, "signal", 1.0, 4.0);

        std::map<std::string, const fit::Nuisance *> nuisance_by_name;
        for (const auto &nuisance : problem.nuisances)
            nuisance_by_name[nuisance.name] = &nuisance;

        require(nuisance_by_name.size() == 4,
                "shared nuisance builder should create four nuisances");
        require(nuisance_by_name.count("genie:weightsGenie:mode0") == 1,
                "shared GENIE family nuisance missing");
        require(nuisance_by_name.count("genie_knob:agky") == 1,
                "shared GENIE knob nuisance missing");
        require(nuisance_by_name.count("detector:sce") == 1,
                "shared detector nuisance missing");
        require(nuisance_by_name.count("total:aux") == 1,
                "total-envelope fallback nuisance missing");

        require(nuisance_by_name["genie:weightsGenie:mode0"]->terms.size() == 2,
                "GENIE nuisance should span signal and background");
        require(nuisance_by_name["genie_knob:agky"]->terms.size() == 2,
                "GENIE knob nuisance should span signal and background");
        require(nuisance_by_name["detector:sce"]->terms.size() == 2,
                "detector nuisance should span signal and background");
        require(nuisance_by_name["total:aux"]->terms.size() == 1,
                "total-envelope nuisance should stay process-local");

        std::vector<double> nuisances(problem.nuisances.size(), 0.0);
        for (std::size_t i = 0; i < problem.nuisances.size(); ++i)
        {
            if (problem.nuisances[i].name == "detector:sce")
                nuisances[i] = 1.0;
            else if (problem.nuisances[i].name == "genie_knob:agky")
                nuisances[i] = 2.0;
            else if (problem.nuisances[i].name == "genie:weightsGenie:mode0")
                nuisances[i] = 1.0;
            else if (problem.nuisances[i].name == "total:aux")
                nuisances[i] = -1.0;
        }

        require_close_vector(fit::predict_bins(problem, 1.0, nuisances),
                             {20.4, 21.1},
                             "shared-nuisance prediction");
        require_close_vector(fit::predict_bins(problem, 2.0, nuisances),
                             {33.5, 35.3},
                             "signal-strength scaling with nuisances");
    }

    void test_validate_problem_rejects_malformed_inputs()
    {
        {
            fit::Channel channel = make_simple_channel();
            channel.processes[1].name = "signal";
            fit::Problem problem = fit::make_independent_problem(channel, "signal", 1.0, 3.0);
            require_throws(
                [&]() { (void)fit::predict_bins(problem, 1.0, std::vector<double>{}); },
                "duplicate non-data process name",
                "duplicate process names should fail");
        }

        {
            fit::Channel channel = make_simple_channel();
            fit::Problem problem = fit::make_independent_problem(channel, "signal", 1.0, 3.0);
            problem.signal_process = "background";
            require_throws(
                [&]() { (void)fit::objective(problem, 1.0, std::vector<double>{}); },
                "signal process must refer to a signal-kind process",
                "signal_process kind should be validated");
        }

        {
            fit::Channel channel = make_simple_channel();
            channel.processes[0].detector_down = {9.0};
            fit::Problem problem = fit::make_independent_problem(channel, "signal", 1.0, 3.0);
            require_throws(
                [&]() { (void)fit::predict_bins(problem, 1.0, std::vector<double>{}); },
                "detector envelope is incomplete",
                "partial detector envelope should fail");
        }

        {
            fit::Channel channel = make_simple_channel();
            channel.processes[0].total_up = {11.0};
            fit::Problem problem = fit::make_independent_problem(channel, "signal", 1.0, 3.0);
            require_throws(
                [&]() { (void)fit::predict_bins(problem, 1.0, std::vector<double>{}); },
                "total envelope is incomplete",
                "partial total envelope should fail");
        }

        {
            fit::Channel channel;
            channel.spec = make_spec(2);
            channel.data = {1.0, 1.0};
            fit::Process signal = make_process("signal", fit::ProcessKind::kSignal, {1.0, 1.0});
            signal.genie.sigma = {0.1, 0.2};
            channel.processes.push_back(signal);
            channel.processes.push_back(make_process("background",
                                                    fit::ProcessKind::kBackground,
                                                    {0.5, 0.5}));
            fit::Problem problem = fit::make_independent_problem(channel, "signal", 1.0, 3.0);
            require_throws(
                [&]() { (void)fit::predict_bins(problem, 1.0, std::vector<double>(problem.nuisances.size(), 0.0)); },
                "genie family branch_name is required when fit payload is present",
                "fit payloads should require a branch_name");
        }

        {
            fit::Channel channel;
            channel.spec = make_spec(2);
            channel.data = {1.0, 1.0};
            fit::Process signal = make_process("signal", fit::ProcessKind::kSignal, {1.0, 1.0});
            signal.genie.branch_name = "weightsGenie";
            signal.genie.sigma = {0.1};
            channel.processes.push_back(signal);
            channel.processes.push_back(make_process("background",
                                                    fit::ProcessKind::kBackground,
                                                    {0.5, 0.5}));
            fit::Problem problem = fit::make_independent_problem(channel, "signal", 1.0, 3.0);
            require_throws(
                [&]() { (void)fit::predict_bins(problem, 1.0, std::vector<double>(problem.nuisances.size(), 0.0)); },
                "genie family sigma size does not match nbins",
                "truncated family sigma should fail");
        }

        {
            fit::Channel channel = make_simple_channel();
            channel.spec.nbins = 0;
            channel.spec.xmax = 0.0;
            fit::Problem problem = fit::make_independent_problem(channel, "signal", 1.0, 3.0);
            require_throws(
                [&]() { (void)fit::objective(problem, 1.0, std::vector<double>{}); },
                "channel spec nbins must be positive",
                "invalid channel binning should fail");
        }

        {
            fit::Channel channel = make_simple_channel();
            channel.processes[0].detector_source_count = -1;
            fit::Problem problem = fit::make_independent_problem(channel, "signal", 1.0, 3.0);
            require_throws(
                [&]() { (void)fit::objective(problem, 1.0, std::vector<double>{}); },
                "detector_source_count must not be negative",
                "negative detector source counts should fail");
        }
    }
}

int main()
{
    try
    {
        test_simple_profile_and_prediction();
        test_independent_problem_builder_and_shared_nuisances();
        test_validate_problem_rejects_malformed_inputs();
        std::cout << "fit_rigorous_check=ok\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
