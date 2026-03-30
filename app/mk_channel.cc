#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ChannelIO.hh"
#include "DistributionIO.hh"

namespace
{
    struct CliOptions
    {
        std::string output_path;
        std::string dist_path;
        std::string signal_sample_key;
        std::string background_sample_key;
        std::string channel_key;
        std::string selection_expr;
        std::string signal_cache_key;
        std::string background_cache_key;
        std::string data_bins_csv;
        std::string signal_name = "signal";
        std::string background_name = "background";
        bool allow_zero_data = false;
    };

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_channel [--selection <expr>] [--data-bins <csv>] "
              "[--signal-cache <key>] [--background-cache <key>] "
              "[--allow-zero-data] "
              "[--signal-name <name>] [--background-name <name>] "
              "<output.root> <input.dists.root> <signal-sample-key> "
              "<background-sample-key> <channel-key>\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_channel: invalid arguments");
    }

    std::string pick_cache_key(const DistributionIO &dist,
                               const std::string &sample_key,
                               const std::string &requested_key)
    {
        if (!requested_key.empty())
        {
            if (!dist.has(sample_key, requested_key))
                throw std::runtime_error("mk_channel: requested cache key is not present for sample");
            return requested_key;
        }

        const std::vector<std::string> keys = dist.dist_keys(sample_key);
        if (keys.empty())
            throw std::runtime_error("mk_channel: no cached distributions found for sample");
        return keys.front();
    }

    std::vector<double> parse_bins_csv(const std::string &csv,
                                       int expected_nbins,
                                       bool allow_zero_data)
    {
        if (csv.empty())
        {
            if (!allow_zero_data)
                throw std::runtime_error("mk_channel: data-bins is required unless --allow-zero-data is set");
            return std::vector<double>(static_cast<std::size_t>(expected_nbins), 0.0);
        }

        std::vector<double> out;
        std::stringstream ss(csv);
        std::string token;
        while (std::getline(ss, token, ','))
        {
            if (token.empty())
                continue;
            out.push_back(std::stod(token));
        }

        if (static_cast<int>(out.size()) != expected_nbins)
            throw std::runtime_error("mk_channel: data bin count does not match cached histogram binning");
        return out;
    }

    void require_matching_inputs(const DistributionIO::Entry &signal,
                                 const DistributionIO::Entry &background)
    {
        if (signal.spec.nbins != background.spec.nbins ||
            signal.spec.xmin != background.spec.xmin ||
            signal.spec.xmax != background.spec.xmax)
        {
            throw std::runtime_error("mk_channel: signal and background caches do not share binning");
        }

        if (signal.spec.branch_expr != background.spec.branch_expr)
            throw std::runtime_error("mk_channel: signal and background caches do not share branch_expr");
        if (signal.spec.selection_expr != background.spec.selection_expr)
            throw std::runtime_error("mk_channel: signal and background caches do not share selection_expr");
    }

    std::string resolve_selection_expr(const DistributionIO::Entry &signal,
                                       const DistributionIO::Entry &background,
                                       const std::string &requested)
    {
        require_matching_inputs(signal, background);

        const std::string &cached = signal.spec.selection_expr;
        if (requested.empty())
            return cached;
        if (cached.empty())
            return requested;
        if (requested != cached)
            throw std::runtime_error("mk_channel: requested selection does not match cached distributions");
        return cached;
    }

    ChannelIO::Process make_process(const DistributionIO::Entry &entry,
                                    const std::string &name,
                                    ChannelIO::ProcessKind kind)
    {
        ChannelIO::Process process;
        process.name = name;
        process.kind = kind;
        process.source_keys = {entry.spec.sample_key};
        process.detector_sample_keys = entry.detector_sample_keys;
        process.nominal = entry.nominal;
        process.sumw2 = entry.sumw2;
        process.detector_down = entry.detector_down;
        process.detector_up = entry.detector_up;
        process.detector_templates = entry.detector_templates;
        process.detector_template_count = entry.detector_template_count;
        process.genie = entry.genie;
        process.flux = entry.flux;
        process.reint = entry.reint;
        process.total_down = entry.total_down;
        process.total_up = entry.total_up;
        return process;
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
            if (arg == "--selection")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.selection_expr = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--data-bins")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.data_bins_csv = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--signal-cache")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.signal_cache_key = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--background-cache")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.background_cache_key = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--signal-name")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.signal_name = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--background-name")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.background_name = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--allow-zero-data")
            {
                options.allow_zero_data = true;
                continue;
            }
            break;
        }

        if (argc - i != 5)
            print_usage_and_throw();

        options.output_path = argv[i] ? argv[i] : "";
        options.dist_path = argv[i + 1] ? argv[i + 1] : "";
        options.signal_sample_key = argv[i + 2] ? argv[i + 2] : "";
        options.background_sample_key = argv[i + 3] ? argv[i + 3] : "";
        options.channel_key = argv[i + 4] ? argv[i + 4] : "";

        if (options.signal_name.empty())
            throw std::runtime_error("mk_channel: signal-name must not be empty");
        if (options.background_name.empty())
            throw std::runtime_error("mk_channel: background-name must not be empty");

        return options;
    }
}

int main(int argc, char **argv)
{
    try
    {
        const CliOptions options = parse_args(argc, argv);

        DistributionIO dist(options.dist_path, DistributionIO::Mode::kRead);
        const DistributionIO::Entry signal =
            dist.read(options.signal_sample_key,
                      pick_cache_key(dist, options.signal_sample_key, options.signal_cache_key));
        const DistributionIO::Entry background =
            dist.read(options.background_sample_key,
                      pick_cache_key(dist,
                                     options.background_sample_key,
                                     options.background_cache_key));

        ChannelIO::Channel channel;
        channel.spec.channel_key = options.channel_key;
        channel.spec.branch_expr = signal.spec.branch_expr;
        channel.spec.selection_expr =
            resolve_selection_expr(signal, background, options.selection_expr);
        channel.spec.nbins = signal.spec.nbins;
        channel.spec.xmin = signal.spec.xmin;
        channel.spec.xmax = signal.spec.xmax;
        channel.data = parse_bins_csv(options.data_bins_csv,
                                      signal.spec.nbins,
                                      options.allow_zero_data);
        channel.processes.push_back(
            make_process(signal, options.signal_name, ChannelIO::ProcessKind::kSignal));
        channel.processes.push_back(
            make_process(background, options.background_name, ChannelIO::ProcessKind::kBackground));

        ChannelIO chio(options.output_path, ChannelIO::Mode::kUpdate);
        chio.write_metadata({options.dist_path, 1});
        chio.write(channel.spec.channel_key, channel);
        chio.flush();

        std::cout << "mk_channel: wrote " << options.output_path
                  << " channel " << options.channel_key
                  << " from " << options.dist_path << "\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        if (std::string(e.what()).empty())
            return 0;
        std::cerr << "mk_channel: " << e.what() << "\n";
        return 1;
    }
}
