#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ChannelIO.hh"
#include "XsecFit.hh"

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
        int max_iterations = 10;
        int nuisance_passes = 8;
        int scan_points = 48;
        double tolerance = 1e-4;
        bool compute_stat_only_interval = true;
    };

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_xsec_fit [--signal-process <name>] [--output <path>] "
              "[--mu-start <value>] [--mu-lower <value>] [--mu-upper <value>] "
              "[--max-iterations <n>] [--nuisance-passes <n>] [--scan-points <n>] "
              "[--tolerance <value>] [--no-stat-interval] "
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

    std::string format_report(const std::string &channel_path,
                              const std::string &channel_key,
                              const fit::Model &model,
                              const fit::Result &result)
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(6);
        os << "channel_path: " << channel_path << "\n";
        os << "channel_key: " << channel_key << "\n";
        os << "signal_process: " << model.signal_process << "\n";
        os << "converged: " << (result.converged ? "true" : "false") << "\n";
        os << "q_min: " << result.objective << "\n";
        os << "mu_hat: " << result.mu_hat << "\n";
        os << "mu_err_total_down: " << result.mu_err_total_down << "\n";
        os << "mu_err_total_up: " << result.mu_err_total_up << "\n";
        os << "mu_err_stat_down: " << result.mu_err_stat_down << "\n";
        os << "mu_err_stat_up: " << result.mu_err_stat_up << "\n";
        os << "sigma_relation: sigma_fit = mu_hat * sigma_nominal\n";
        os << "observed_bins: " << format_bins_csv(model.channel->data) << "\n";
        os << "predicted_signal_bins: " << format_bins_csv(result.predicted_signal) << "\n";
        os << "predicted_background_bins: " << format_bins_csv(result.predicted_background) << "\n";
        os << "predicted_total_bins: " << format_bins_csv(result.predicted_total) << "\n";
        os << "nuisance_count: " << result.nuisance_names.size() << "\n";
        for (std::size_t i = 0; i < result.nuisance_names.size(); ++i)
            os << "nuisance[" << i << "]: " << result.nuisance_names[i]
               << "=" << result.nuisance_values[i] << "\n";
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
            if (arg == "--nuisance-passes")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.nuisance_passes = std::stoi(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--scan-points")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.scan_points = std::stoi(argv[i] ? argv[i] : "");
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

        fit::Model model =
            fit::make_independent_model(channel, options.signal_process, options.mu_start, options.mu_upper);
        model.mu_lower = options.mu_lower;
        model.mu_upper = options.mu_upper;

        fit::FitOptions fit_options;
        fit_options.max_iterations = options.max_iterations;
        fit_options.nuisance_passes = options.nuisance_passes;
        fit_options.scan_points = options.scan_points;
        fit_options.tolerance = options.tolerance;
        fit_options.compute_stat_only_interval = options.compute_stat_only_interval;

        const fit::Result result = fit::profile_xsec(model, fit_options);
        const std::string report =
            format_report(options.channel_path, options.channel_key, model, result);

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
