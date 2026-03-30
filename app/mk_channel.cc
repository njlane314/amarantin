#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ChannelIO.hh"
#include "DistributionIO.hh"

namespace
{
    enum class InputMode
    {
        kLegacy,
        kManifest
    };

    struct CliOptions
    {
        InputMode mode = InputMode::kLegacy;
        std::string output_path;
        std::string dist_path;
        std::string channel_key;
        std::string manifest_path;
        std::string signal_sample_key;
        std::string background_sample_key;
        std::string selection_expr;
        std::string signal_cache_key;
        std::string background_cache_key;
        std::string data_bins_csv;
        std::string signal_name = "signal";
        std::string background_name = "background";
        bool allow_zero_data = false;
        bool legacy_process_flags_used = false;
    };

    struct ManifestRow
    {
        std::string name;
        ChannelIO::ProcessKind kind = ChannelIO::ProcessKind::kBackground;
        std::string sample_key;
        std::string cache_key;
        int line_number = 0;
    };

    struct LoadedSource
    {
        std::string name;
        ChannelIO::ProcessKind kind = ChannelIO::ProcessKind::kBackground;
        DistributionIO::Entry entry;
    };

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_channel [--selection <expr>] [--data-bins <csv>] "
              "[--signal-cache <key>] [--background-cache <key>] "
              "[--signal-name <name>] [--background-name <name>] "
              "[--allow-zero-data] "
              "<output.root> <input.dists.root> <signal-sample-key> "
              "<background-sample-key> <channel-key>\n"
              "   or: mk_channel [--selection <expr>] [--data-bins <csv>] "
              "[--manifest <channel.manifest>] [--allow-zero-data] "
              "<output.root> <input.dists.root> <channel-key>\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_channel: invalid arguments");
    }

    std::string trim_copy(const std::string &input)
    {
        const std::string::size_type first = input.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return "";

        const std::string::size_type last = input.find_last_not_of(" \t\r\n");
        return input.substr(first, last - first + 1);
    }

    std::string strip_comment(const std::string &line)
    {
        const std::string::size_type pos = line.find('#');
        if (pos == std::string::npos)
            return line;
        return line.substr(0, pos);
    }

    std::vector<std::string> split_fields(const std::string &line)
    {
        std::istringstream input(line);
        std::vector<std::string> out;
        std::string field;
        while (input >> field)
            out.push_back(field);
        return out;
    }

    std::string lower_copy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string normalise_optional_token(const std::string &token)
    {
        return token == "-" ? std::string() : token;
    }

    ChannelIO::ProcessKind manifest_kind_from_token(const std::string &token)
    {
        const std::string key = lower_copy(token);
        if (key == "signal" || key == "sig")
            return ChannelIO::ProcessKind::kSignal;
        if (key == "background" || key == "bkg" || key == "bg")
            return ChannelIO::ProcessKind::kBackground;
        if (key == "data")
            return ChannelIO::ProcessKind::kData;
        throw std::runtime_error("mk_channel: manifest kind must be signal, background, or data");
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
                                       bool allow_zero_data,
                                       const char *missing_context)
    {
        if (csv.empty())
        {
            if (!allow_zero_data)
                throw std::runtime_error(missing_context);
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

    void require_matching_specs(const DistributionIO::Entry &reference,
                                const DistributionIO::Entry &candidate,
                                const std::string &context)
    {
        if (reference.spec.nbins != candidate.spec.nbins ||
            reference.spec.xmin != candidate.spec.xmin ||
            reference.spec.xmax != candidate.spec.xmax)
        {
            throw std::runtime_error("mk_channel: " + context +
                                     " does not share histogram binning with the channel reference");
        }

        if (reference.spec.branch_expr != candidate.spec.branch_expr)
        {
            throw std::runtime_error("mk_channel: " + context +
                                     " does not share branch_expr with the channel reference");
        }

        if (reference.spec.selection_expr != candidate.spec.selection_expr)
        {
            throw std::runtime_error("mk_channel: " + context +
                                     " does not share selection_expr with the channel reference");
        }
    }

    std::string resolve_selection_expr(const DistributionIO::Entry &reference,
                                       const std::string &requested)
    {
        const std::string &cached = reference.spec.selection_expr;
        if (requested.empty())
            return cached;
        if (cached.empty())
            return requested;
        if (requested != cached)
            throw std::runtime_error("mk_channel: requested selection does not match cached distributions");
        return cached;
    }

    void add_bins_in_place(std::vector<double> &target,
                           const std::vector<double> &source,
                           const std::string &context)
    {
        if (target.size() != source.size())
            throw std::runtime_error("mk_channel: " + context + " has incompatible bin payload size");

        for (std::size_t i = 0; i < target.size(); ++i)
            target[i] += source[i];
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

    std::vector<ManifestRow> read_manifest(const std::string &path)
    {
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error("mk_channel: failed to open manifest: " + path);

        std::vector<ManifestRow> rows;
        std::string line;
        int line_number = 0;
        while (std::getline(input, line))
        {
            ++line_number;
            const std::string trimmed = trim_copy(strip_comment(line));
            if (trimmed.empty())
                continue;

            const std::vector<std::string> fields = split_fields(trimmed);
            if (fields.size() < 3 || fields.size() > 4)
            {
                throw std::runtime_error("mk_channel: expected 3 or 4 fields in manifest at line " +
                                         std::to_string(line_number) + " in " + path);
            }

            ManifestRow row;
            row.name = fields[0];
            row.kind = manifest_kind_from_token(fields[1]);
            row.sample_key = fields[2];
            row.cache_key = (fields.size() == 4) ? normalise_optional_token(fields[3]) : std::string();
            row.line_number = line_number;

            if (row.name.empty())
                throw std::runtime_error("mk_channel: empty process name in manifest at line " +
                                         std::to_string(line_number));
            if (row.sample_key.empty())
                throw std::runtime_error("mk_channel: empty sample key in manifest at line " +
                                         std::to_string(line_number));

            rows.push_back(std::move(row));
        }

        return rows;
    }

    std::vector<LoadedSource> load_manifest_sources(const DistributionIO &dist,
                                                    const std::vector<ManifestRow> &rows)
    {
        std::vector<LoadedSource> sources;
        sources.reserve(rows.size());

        std::vector<std::string> seen_process_names;
        for (const auto &row : rows)
        {
            if (row.kind != ChannelIO::ProcessKind::kData)
            {
                if (std::find(seen_process_names.begin(), seen_process_names.end(), row.name) != seen_process_names.end())
                {
                    throw std::runtime_error("mk_channel: duplicate non-data process name in manifest: " +
                                             row.name);
                }
                seen_process_names.push_back(row.name);
            }

            LoadedSource source;
            source.name = row.name;
            source.kind = row.kind;
            source.entry = dist.read(row.sample_key, pick_cache_key(dist, row.sample_key, row.cache_key));
            sources.push_back(std::move(source));
        }

        return sources;
    }

    ChannelIO::Channel build_legacy_channel(const CliOptions &options,
                                            const DistributionIO &dist)
    {
        const DistributionIO::Entry signal =
            dist.read(options.signal_sample_key,
                      pick_cache_key(dist, options.signal_sample_key, options.signal_cache_key));
        const DistributionIO::Entry background =
            dist.read(options.background_sample_key,
                      pick_cache_key(dist,
                                     options.background_sample_key,
                                     options.background_cache_key));

        require_matching_specs(signal, background, "background input");

        ChannelIO::Channel channel;
        channel.spec.channel_key = options.channel_key;
        channel.spec.branch_expr = signal.spec.branch_expr;
        channel.spec.selection_expr = resolve_selection_expr(signal, options.selection_expr);
        channel.spec.nbins = signal.spec.nbins;
        channel.spec.xmin = signal.spec.xmin;
        channel.spec.xmax = signal.spec.xmax;
        channel.data = parse_bins_csv(options.data_bins_csv,
                                      signal.spec.nbins,
                                      options.allow_zero_data,
                                      "mk_channel: data-bins is required unless --allow-zero-data is set");
        channel.processes.push_back(
            make_process(signal, options.signal_name, ChannelIO::ProcessKind::kSignal));
        channel.processes.push_back(
            make_process(background, options.background_name, ChannelIO::ProcessKind::kBackground));
        return channel;
    }

    ChannelIO::Channel build_manifest_channel(const CliOptions &options,
                                              const DistributionIO &dist)
    {
        const std::vector<ManifestRow> rows = read_manifest(options.manifest_path);
        if (rows.empty())
            throw std::runtime_error("mk_channel: manifest does not contain any channel inputs");

        const std::vector<LoadedSource> sources = load_manifest_sources(dist, rows);
        const DistributionIO::Entry &reference = sources.front().entry;
        for (std::size_t i = 1; i < sources.size(); ++i)
        {
            require_matching_specs(reference,
                                   sources[i].entry,
                                   "manifest input " + sources[i].name);
        }

        ChannelIO::Channel channel;
        channel.spec.channel_key = options.channel_key;
        channel.spec.branch_expr = reference.spec.branch_expr;
        channel.spec.selection_expr = resolve_selection_expr(reference, options.selection_expr);
        channel.spec.nbins = reference.spec.nbins;
        channel.spec.xmin = reference.spec.xmin;
        channel.spec.xmax = reference.spec.xmax;

        std::vector<double> observed_data(static_cast<std::size_t>(reference.spec.nbins), 0.0);
        bool have_observed_data = false;
        for (const auto &source : sources)
        {
            if (source.kind == ChannelIO::ProcessKind::kData)
            {
                add_bins_in_place(observed_data,
                                  source.entry.nominal,
                                  "manifest data input " + source.name);
                channel.data_source_keys.push_back(source.entry.spec.sample_key);
                have_observed_data = true;
                continue;
            }

            channel.processes.push_back(make_process(source.entry, source.name, source.kind));
        }

        if (channel.processes.empty())
            throw std::runtime_error("mk_channel: manifest must include at least one signal or background row");

        if (have_observed_data)
        {
            if (!options.data_bins_csv.empty())
                throw std::runtime_error("mk_channel: data-bins cannot be combined with manifest data rows");
            channel.data = std::move(observed_data);
        }
        else
        {
            channel.data = parse_bins_csv(options.data_bins_csv,
                                          reference.spec.nbins,
                                          options.allow_zero_data,
                                          "mk_channel: data-bins is required unless the manifest contains data rows or --allow-zero-data is set");
        }

        return channel;
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
                options.legacy_process_flags_used = true;
                continue;
            }
            if (arg == "--background-cache")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.background_cache_key = argv[i] ? argv[i] : "";
                options.legacy_process_flags_used = true;
                continue;
            }
            if (arg == "--signal-name")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.signal_name = argv[i] ? argv[i] : "";
                options.legacy_process_flags_used = true;
                continue;
            }
            if (arg == "--background-name")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.background_name = argv[i] ? argv[i] : "";
                options.legacy_process_flags_used = true;
                continue;
            }
            if (arg == "--manifest")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.manifest_path = argv[i] ? argv[i] : "";
                options.mode = InputMode::kManifest;
                continue;
            }
            if (arg == "--allow-zero-data")
            {
                options.allow_zero_data = true;
                continue;
            }
            break;
        }

        if (options.mode == InputMode::kManifest)
        {
            if (options.legacy_process_flags_used)
            {
                throw std::runtime_error(
                    "mk_channel: --manifest cannot be combined with legacy signal/background-specific flags");
            }
            if (argc - i != 3)
                print_usage_and_throw();

            options.output_path = argv[i] ? argv[i] : "";
            options.dist_path = argv[i + 1] ? argv[i + 1] : "";
            options.channel_key = argv[i + 2] ? argv[i + 2] : "";
            return options;
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
        const ChannelIO::Channel channel =
            (options.mode == InputMode::kManifest)
                ? build_manifest_channel(options, dist)
                : build_legacy_channel(options, dist);

        ChannelIO chio(options.output_path, ChannelIO::Mode::kUpdate);
        chio.write_metadata({options.dist_path, 1});
        chio.write(channel.spec.channel_key, channel);
        chio.flush();

        std::cout << "mk_channel: wrote " << options.output_path
                  << " channel " << channel.spec.channel_key
                  << " from " << options.dist_path;
        if (options.mode == InputMode::kManifest)
            std::cout << " using manifest " << options.manifest_path;
        std::cout << "\n";
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
