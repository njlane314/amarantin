#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ChannelIO.hh"
#include "SignalStrengthFit.hh"

namespace
{
    struct CliOptions
    {
        std::string channel_path;
        std::string channel_key;
        std::string signal_process = "signal";
        std::string output_path;
        double mu_start = 1.0;
        double mu_lower = 0.0;
        double mu_upper = 5.0;
        int max_iterations = 10000;
        int max_function_calls = 100000;
        int scan_points = 48;
        int strategy = 1;
        int print_level = -1;
        double tolerance = 1e-4;
        bool compute_stat_only_interval = true;
        bool run_hesse = true;
        bool allow_zero_data = false;
    };

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_xsec_fit [--signal-process <name>] [--output <path>] "
              "[--mu-start <value>] [--mu-lower <value>] [--mu-upper <value>] "
              "[--max-iterations <n>] [--max-function-calls <n>] "
              "[--scan-points <n>] [--strategy <n>] [--print-level <n>] "
              "[--tolerance <value>] [--no-stat-interval] [--no-hesse] "
              "[--allow-zero-data] "
              "<input.channels.root> <channel-key>\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_xsec_fit: invalid arguments");
    }

    std::string format_bins_csv(const std::vector<double> &values)
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(6);
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i != 0)
                os << ",";
            os << values[i];
        }
        return os.str();
    }

    const char *format_bool(bool value)
    {
        return value ? "true" : "false";
    }

    bool all_zero_bins(const std::vector<double> &values)
    {
        for (double value : values)
        {
            if (value != 0.0)
                return false;
        }
        return true;
    }

    std::string format_report(const std::string &channel_path,
                              const std::string &channel_key,
                              const fit::Problem &problem,
                              const fit::Result &result)
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(6);
        os << "channel_path: " << channel_path << "\n";
        os << "channel_key: " << channel_key << "\n";
        os << "signal_process: " << problem.signal_process << "\n";
        os << "converged: " << format_bool(result.converged) << "\n";
        os << "minimizer_status: " << result.minimizer_status << "\n";
        os << "edm: " << result.edm << "\n";
        os << "q_min: " << result.objective << "\n";
        os << "mu_hat: " << result.mu_hat << "\n";
        os << "mu_err_total_down_found: " << format_bool(result.mu_err_total_down_found) << "\n";
        os << "mu_err_total_down: " << result.mu_err_total_down << "\n";
        os << "mu_err_total_up_found: " << format_bool(result.mu_err_total_up_found) << "\n";
        os << "mu_err_total_up: " << result.mu_err_total_up << "\n";
        os << "mu_err_stat_down_found: " << format_bool(result.mu_err_stat_down_found) << "\n";
        os << "mu_err_stat_down: " << result.mu_err_stat_down << "\n";
        os << "mu_err_stat_up_found: " << format_bool(result.mu_err_stat_up_found) << "\n";
        os << "mu_err_stat_up: " << result.mu_err_stat_up << "\n";
        os << "sigma_relation: sigma_fit = mu_hat * sigma_nominal\n";
        os << "observed_bins: " << format_bins_csv(problem.channel->data) << "\n";
        os << "predicted_signal_bins: " << format_bins_csv(result.predicted_signal) << "\n";
        os << "predicted_background_bins: " << format_bins_csv(result.predicted_background) << "\n";
        os << "predicted_total_bins: " << format_bins_csv(result.predicted_total) << "\n";
        os << "nuisance_count: " << result.nuisance_names.size() << "\n";
        for (std::size_t i = 0; i < result.nuisance_names.size(); ++i)
            os << "nuisance[" << i << "]: " << result.nuisance_names[i]
               << "=" << result.nuisance_values[i] << "\n";
        os << "parameter_count: " << result.parameter_names.size() << "\n";
        for (std::size_t i = 0; i < result.parameter_names.size(); ++i)
            os << "parameter[" << i << "]: " << result.parameter_names[i]
               << "=" << result.parameter_values[i] << "\n";
        if (!result.covariance.empty() && !result.parameter_names.empty())
        {
            const std::size_t n = result.parameter_names.size();
            for (std::size_t row = 0; row < n; ++row)
            {
                std::vector<double> row_values;
                row_values.reserve(n);
                for (std::size_t col = 0; col < n; ++col)
                {
                    row_values.push_back(
                        result.covariance[static_cast<std::size_t>(row * n + col)]);
                }
                os << "covariance_row[" << row << "]: " << format_bins_csv(row_values) << "\n";
            }
        }
        return os.str();
    }

    CliOptions parse_args(int argc, char **argv)
    {
        CliOptions options;
        int i = 1;
        for (; i < argc; ++i)
        {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg == "-h" || arg == "--help")
            {
                print_usage(std::cout);
                throw std::runtime_error("");
            }
            if (arg == "--signal-process")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.signal_process = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--output")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.output_path = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--mu-start")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.mu_start = std::stod(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--mu-lower")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.mu_lower = std::stod(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--mu-upper")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.mu_upper = std::stod(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--max-iterations")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.max_iterations = std::stoi(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--max-function-calls")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.max_function_calls = std::stoi(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--scan-points")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.scan_points = std::stoi(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--strategy")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.strategy = std::stoi(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--print-level")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.print_level = std::stoi(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--tolerance")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.tolerance = std::stod(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--no-stat-interval")
            {
                options.compute_stat_only_interval = false;
                continue;
            }
            if (arg == "--no-hesse")
            {
                options.run_hesse = false;
                continue;
            }
            if (arg == "--allow-zero-data")
            {
                options.allow_zero_data = true;
                continue;
            }
            break;
        }

        if (argc - i != 2)
            print_usage_and_throw();

        options.channel_path = argv[i] ? argv[i] : "";
        options.channel_key = argv[i + 1] ? argv[i + 1] : "";

        if (options.signal_process.empty())
            throw std::runtime_error("mk_xsec_fit: signal-process must not be empty");
        return options;
    }
}

int main(int argc, char **argv)
{
    try
    {
        const CliOptions options = parse_args(argc, argv);

        ChannelIO chio(options.channel_path, ChannelIO::Mode::kRead);
        const ChannelIO::Channel channel = chio.read(options.channel_key);
        if (!options.allow_zero_data && all_zero_bins(channel.data))
        {
            throw std::runtime_error(
                "mk_xsec_fit: observed bins are all zero; pass --allow-zero-data to fit an empty observation intentionally");
        }

        fit::Problem problem =
            fit::make_independent_problem(channel, options.signal_process, options.mu_start, options.mu_upper);
        problem.mu_lower = options.mu_lower;
        problem.mu_upper = options.mu_upper;

        fit::FitOptions fit_options;
        fit_options.max_iterations = options.max_iterations;
        fit_options.max_function_calls = options.max_function_calls;
        fit_options.scan_points = options.scan_points;
        fit_options.strategy = options.strategy;
        fit_options.print_level = options.print_level;
        fit_options.tolerance = options.tolerance;
        fit_options.compute_stat_only_interval = options.compute_stat_only_interval;
        fit_options.run_hesse = options.run_hesse;

        const fit::Result result = fit::profile_signal_strength(problem, fit_options);
        const std::string report =
            format_report(options.channel_path, options.channel_key, problem, result);

        if (!options.output_path.empty())
        {
            std::ofstream out(options.output_path);
            if (!out)
                throw std::runtime_error("mk_xsec_fit: failed to open output file");
            out << report;
            std::cout << "mk_xsec_fit: wrote " << options.output_path
                      << " from " << options.channel_path
                      << " channel " << options.channel_key << "\n";
            return 0;
        }

        std::cout << report;
        return 0;
    }
    catch (const std::exception &e)
    {
        if (std::string(e.what()).empty())
            return 0;
        std::cerr << "mk_xsec_fit: " << e.what() << "\n";
        return 1;
    }
}
