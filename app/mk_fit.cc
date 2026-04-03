#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "DistributionIO.hh"
#include "SignalStrengthFit.hh"

namespace
{
    constexpr const char *kProgramName = "mk_fit";

    std::string cli_prefix()
    {
        return std::string(kProgramName) + ": ";
    }

    std::string cli_error(const std::string &message)
    {
        return cli_prefix() + message;
    }

    bool has_cli_prefix(const std::string &message)
    {
        return message.rfind(cli_prefix(), 0) == 0;
    }

    enum class InputMode
    {
        kLegacy,
        kManifest
    };

    struct CliOptions
    {
        InputMode mode = InputMode::kLegacy;
        std::string dist_path;
        std::string channel_key;
        std::string manifest_path;
        std::string signal_sample_key;
        std::string background_sample_key;
        std::string selection_expr;
        std::string signal_cache_key;
        std::string background_cache_key;
        std::string data_bins_csv;
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
        bool legacy_process_flags_used = false;
    };

    struct ManifestRow
    {
        std::string fit_channel_key;
        std::string name;
        fit::ProcessKind kind = fit::ProcessKind::kBackground;
        std::string sample_key;
        std::string cache_key;
        int line_number = 0;
    };

    struct LoadedSource
    {
        std::string fit_channel_key;
        std::string name;
        fit::ProcessKind kind = fit::ProcessKind::kBackground;
        DistributionIO::Spectrum spectrum;
    };

    void print_usage(std::ostream &os)
    {
        os << "usage: " << kProgramName
           << " [--signal-process <name>] [--output <path>] "
              "[--mu-start <value>] [--mu-lower <value>] [--mu-upper <value>] "
              "[--max-iterations <n>] [--max-function-calls <n>] "
              "[--scan-points <n>] [--strategy <n>] [--print-level <n>] "
              "[--tolerance <value>] [--no-stat-interval] [--no-hesse] "
              "[--selection <expr>] [--data-bins <csv>] [--signal-cache <key>] "
              "[--background-cache <key>] [--allow-zero-data] "
              "<input.dists.root> <signal-sample-key> <background-sample-key> "
              "<channel-key>\n"
              "   or: " << kProgramName
           << " [fit flags] [--selection <expr>] "
              "[--data-bins <csv>] [--manifest <fit.manifest>] "
              "[--allow-zero-data] <input.dists.root> <channel-key>\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error(cli_error("invalid arguments"));
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

    bool is_manifest_kind_token(const std::string &token)
    {
        const std::string key = lower_copy(token);
        return key == "signal" || key == "sig" ||
               key == "background" || key == "bkg" || key == "bg" ||
               key == "data";
    }

    fit::ProcessKind manifest_kind_from_token(const std::string &token)
    {
        const std::string key = lower_copy(token);
        if (key == "signal" || key == "sig")
            return fit::ProcessKind::kSignal;
        if (key == "background" || key == "bkg" || key == "bg")
            return fit::ProcessKind::kBackground;
        if (key == "data")
            return fit::ProcessKind::kData;
        throw std::runtime_error(cli_error("manifest kind must be signal, background, or data"));
    }

    std::string pick_cache_key(const DistributionIO &dist,
                               const std::string &sample_key,
                               const std::string &requested_key)
    {
        if (!requested_key.empty())
        {
            if (!dist.has(sample_key, requested_key))
                throw std::runtime_error(cli_error("requested cache key is not present for sample"));
            return requested_key;
        }

        const std::vector<std::string> keys = dist.dist_keys(sample_key);
        if (keys.empty())
            throw std::runtime_error(cli_error("no cached distributions found for sample"));
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
            throw std::runtime_error(cli_error("data bin count does not match cached histogram binning"));
        return out;
    }

    void require_matching_specs(const DistributionIO::Spectrum &reference,
                                const DistributionIO::Spectrum &candidate,
                                const std::string &context)
    {
        if (reference.spec.nbins != candidate.spec.nbins ||
            reference.spec.xmin != candidate.spec.xmin ||
            reference.spec.xmax != candidate.spec.xmax)
        {
            throw std::runtime_error(cli_error(context +
                                               " does not share histogram binning with the fit reference"));
        }

        if (reference.spec.branch_expr != candidate.spec.branch_expr)
        {
            throw std::runtime_error(cli_error(context +
                                               " does not share branch_expr with the fit reference"));
        }

        if (reference.spec.selection_expr != candidate.spec.selection_expr)
        {
            throw std::runtime_error(cli_error(context +
                                               " does not share selection_expr with the fit reference"));
        }
    }

    std::string resolve_selection_expr(const DistributionIO::Spectrum &reference,
                                       const std::string &requested)
    {
        const std::string &cached = reference.spec.selection_expr;
        if (requested.empty())
            return cached;
        if (cached.empty())
            return requested;
        if (requested != cached)
            throw std::runtime_error(cli_error("requested selection does not match cached distributions"));
        return cached;
    }

    void add_bins_in_place(std::vector<double> &target,
                           const std::vector<double> &source,
                           const std::string &context)
    {
        if (target.size() != source.size())
            throw std::runtime_error(cli_error(context + " has incompatible bin payload size"));

        for (std::size_t i = 0; i < target.size(); ++i)
            target[i] += source[i];
    }

    fit::Process make_process(const DistributionIO::Spectrum &spectrum,
                              const std::string &name,
                              fit::ProcessKind kind)
    {
        fit::Process process;
        process.name = name;
        process.kind = kind;
        process.source_keys = {spectrum.spec.sample_key};
        process.detector_source_labels = spectrum.detector_source_labels;
        process.detector_sample_keys = spectrum.detector_sample_keys;
        process.genie_knob_source_labels = spectrum.genie_knob_source_labels;
        process.nominal = spectrum.nominal;
        process.sumw2 = spectrum.sumw2;
        process.detector_shift_vectors = spectrum.detector_shift_vectors;
        process.detector_source_count = spectrum.detector_source_count;
        process.genie_knob_shift_vectors = spectrum.genie_knob_shift_vectors;
        process.genie_knob_source_count = spectrum.genie_knob_source_count;
        process.detector_down = spectrum.detector_down;
        process.detector_up = spectrum.detector_up;
        process.detector_templates = spectrum.detector_templates;
        process.detector_template_count = spectrum.detector_template_count;
        process.genie = spectrum.genie;
        process.flux = spectrum.flux;
        process.reint = spectrum.reint;
        process.total_down = spectrum.total_down;
        process.total_up = spectrum.total_up;
        return process;
    }

    std::vector<ManifestRow> read_manifest(const std::string &path,
                                           const std::string &default_channel_key)
    {
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error(cli_error("failed to open manifest: " + path));

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
            if (fields.size() < 3 || fields.size() > 5)
            {
                throw std::runtime_error(
                    cli_error("expected 3, 4, or 5 fields in manifest at line " +
                              std::to_string(line_number) + " in " + path));
            }

            ManifestRow row;
            if (fields.size() <= 4)
            {
                row.fit_channel_key = default_channel_key;
                row.name = fields[0];
                row.kind = manifest_kind_from_token(fields[1]);
                row.sample_key = fields[2];
                row.cache_key = (fields.size() == 4) ? normalise_optional_token(fields[3]) : std::string();
            }
            else
            {
                if (default_channel_key.empty())
                {
                    throw std::runtime_error(
                        cli_error("multi-channel manifest rows require a non-empty fit key"));
                }
                if (!is_manifest_kind_token(fields[2]))
                {
                    throw std::runtime_error(
                        cli_error("5-field manifest rows must use: fit-channel process-name kind sample-key cache-key"));
                }
                row.fit_channel_key = fields[0];
                row.name = fields[1];
                row.kind = manifest_kind_from_token(fields[2]);
                row.sample_key = fields[3];
                row.cache_key = normalise_optional_token(fields[4]);
            }
            row.line_number = line_number;

            if (row.fit_channel_key.empty())
            {
                throw std::runtime_error(
                    cli_error("empty fit channel key in manifest at line " +
                              std::to_string(line_number)));
            }
            if (row.name.empty())
                throw std::runtime_error(
                    cli_error("empty process name in manifest at line " +
                              std::to_string(line_number)));
            if (row.sample_key.empty())
                throw std::runtime_error(
                    cli_error("empty sample key in manifest at line " +
                              std::to_string(line_number)));

            rows.push_back(std::move(row));
        }

        return rows;
    }

    std::vector<LoadedSource> load_manifest_sources(const DistributionIO &dist,
                                                    const std::vector<ManifestRow> &rows)
    {
        std::vector<LoadedSource> sources;
        sources.reserve(rows.size());

        std::vector<std::pair<std::string, std::string>> seen_process_names;
        for (const auto &row : rows)
        {
            if (row.kind != fit::ProcessKind::kData)
            {
                const std::pair<std::string, std::string> key{row.fit_channel_key, row.name};
                if (std::find(seen_process_names.begin(), seen_process_names.end(), key) != seen_process_names.end())
                {
                    throw std::runtime_error(
                        cli_error("duplicate non-data process name in manifest channel " +
                                  row.fit_channel_key + ": " + row.name));
                }
                seen_process_names.push_back(key);
            }

            LoadedSource source;
            source.fit_channel_key = row.fit_channel_key;
            source.name = row.name;
            source.kind = row.kind;
            source.spectrum = dist.read(row.sample_key, pick_cache_key(dist, row.sample_key, row.cache_key));
            sources.push_back(std::move(source));
        }

        return sources;
    }

    fit::Channel build_legacy_channel(const CliOptions &options,
                                      const DistributionIO &dist)
    {
        const DistributionIO::Spectrum signal =
            dist.read(options.signal_sample_key,
                      pick_cache_key(dist, options.signal_sample_key, options.signal_cache_key));
        const DistributionIO::Spectrum background =
            dist.read(options.background_sample_key,
                      pick_cache_key(dist,
                                     options.background_sample_key,
                                     options.background_cache_key));

        require_matching_specs(signal, background, "background input");

        fit::Channel channel;
        channel.spec.channel_key = options.channel_key;
        channel.spec.branch_expr = signal.spec.branch_expr;
        channel.spec.selection_expr = resolve_selection_expr(signal, options.selection_expr);
        channel.spec.nbins = signal.spec.nbins;
        channel.spec.xmin = signal.spec.xmin;
        channel.spec.xmax = signal.spec.xmax;
        channel.data = parse_bins_csv(options.data_bins_csv,
                                      signal.spec.nbins,
                                      options.allow_zero_data,
                                      "data-bins is required unless --allow-zero-data is set");
        channel.processes.push_back(
            make_process(signal, options.signal_process, fit::ProcessKind::kSignal));
        channel.processes.push_back(
            make_process(background, "background", fit::ProcessKind::kBackground));
        return channel;
    }

    std::vector<fit::Channel> build_manifest_channels(const CliOptions &options,
                                                      const DistributionIO &dist)
    {
        const std::vector<ManifestRow> rows =
            read_manifest(options.manifest_path, options.channel_key);
        if (rows.empty())
            throw std::runtime_error(cli_error("manifest does not contain any fit inputs"));

        const std::vector<LoadedSource> sources = load_manifest_sources(dist, rows);
        std::vector<fit::Channel> channels;

        for (const auto &source : sources)
        {
            auto it = std::find_if(channels.begin(),
                                   channels.end(),
                                   [&](const fit::Channel &channel)
                                   {
                                       return channel.spec.channel_key == source.fit_channel_key;
                                   });

            if (it == channels.end())
            {
                fit::Channel channel;
                channel.spec.channel_key = source.fit_channel_key;
                channel.spec.branch_expr = source.spectrum.spec.branch_expr;
                channel.spec.selection_expr =
                    resolve_selection_expr(source.spectrum, options.selection_expr);
                channel.spec.nbins = source.spectrum.spec.nbins;
                channel.spec.xmin = source.spectrum.spec.xmin;
                channel.spec.xmax = source.spectrum.spec.xmax;
                channels.push_back(std::move(channel));
                it = std::prev(channels.end());
            }
            else
            {
                const std::string resolved_selection =
                    resolve_selection_expr(source.spectrum, options.selection_expr);
                if (it->spec.branch_expr != source.spectrum.spec.branch_expr ||
                    it->spec.selection_expr != resolved_selection ||
                    it->spec.nbins != source.spectrum.spec.nbins ||
                    it->spec.xmin != source.spectrum.spec.xmin ||
                    it->spec.xmax != source.spectrum.spec.xmax)
                {
                    throw std::runtime_error(
                        cli_error("manifest input " + source.fit_channel_key + "/" + source.name +
                                  " does not match the rest of its fit channel"));
                }
            }

            if (source.kind == fit::ProcessKind::kData)
            {
                if (it->data.empty())
                    it->data.assign(source.spectrum.nominal.size(), 0.0);
                add_bins_in_place(it->data,
                                  source.spectrum.nominal,
                                  "manifest data input " + source.fit_channel_key + "/" + source.name);
                it->data_source_keys.push_back(source.spectrum.spec.sample_key);
                continue;
            }

            it->processes.push_back(make_process(source.spectrum, source.name, source.kind));
        }

        if (channels.empty())
            throw std::runtime_error(cli_error("manifest does not define any fit channels"));

        const bool multi_channel = channels.size() > 1;
        for (auto &channel : channels)
        {
            if (channel.processes.empty())
            {
                throw std::runtime_error(
                    cli_error("manifest channel " + channel.spec.channel_key +
                              " must include at least one signal or background row"));
            }

            if (!channel.data.empty())
            {
                if (!options.data_bins_csv.empty())
                {
                    throw std::runtime_error(
                        cli_error("data-bins cannot be combined with manifest data rows"));
                }
                continue;
            }

            if (multi_channel && !options.data_bins_csv.empty())
            {
                throw std::runtime_error(
                    cli_error("data-bins only supports one fit channel; multi-channel manifests must provide data rows"));
            }

            channel.data = parse_bins_csv(options.data_bins_csv,
                                          channel.spec.nbins,
                                          options.allow_zero_data,
                                          "data-bins is required unless the manifest contains data rows or --allow-zero-data is set");
        }

        return channels;
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

    std::string format_strings_csv(const std::vector<std::string> &values)
    {
        if (values.empty())
            return "-";

        std::ostringstream os;
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

    bool all_zero_channels(const std::vector<fit::Channel> &channels)
    {
        for (const auto &channel : channels)
        {
            if (!all_zero_bins(channel.data))
                return false;
        }
        return true;
    }

    std::vector<std::string> collect_signal_process_names(const fit::Problem &problem)
    {
        std::vector<std::string> names;
        for (const auto &channel : problem.channels)
        {
            for (const auto &process : channel.processes)
            {
                if (process.kind != fit::ProcessKind::kSignal)
                    continue;
                if (std::find(names.begin(), names.end(), process.name) == names.end())
                    names.push_back(process.name);
            }
        }
        return names;
    }

    double quad_diff_or_nan(bool total_found,
                            double total,
                            bool stat_found,
                            double stat)
    {
        if (!total_found || !stat_found)
            return std::numeric_limits<double>::quiet_NaN();
        const double value = total * total - stat * stat;
        return (value > 0.0) ? std::sqrt(value) : 0.0;
    }

    std::string format_report(const std::string &distribution_path,
                              const DistributionIO::Metadata &metadata,
                              const std::string &channel_key,
                              const fit::Problem &problem,
                              const fit::Result &result)
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(6);
        const double mu_err_syst_down = quad_diff_or_nan(result.mu_err_total_down_found,
                                                         result.mu_err_total_down,
                                                         result.mu_err_stat_down_found,
                                                         result.mu_err_stat_down);
        const double mu_err_syst_up = quad_diff_or_nan(result.mu_err_total_up_found,
                                                       result.mu_err_total_up,
                                                       result.mu_err_stat_up_found,
                                                       result.mu_err_stat_up);
        os << "distribution_path: " << distribution_path << "\n";
        os << "distribution_build_version: " << metadata.build_version << "\n";
        os << "eventlist_path: "
           << (metadata.eventlist_path.empty() ? "-" : metadata.eventlist_path)
           << "\n";
        os << "channel_key: " << channel_key << "\n";
        const std::vector<std::string> signal_processes = collect_signal_process_names(problem);
        os << "signal_process: "
           << (signal_processes.empty() ? "-" : signal_processes.front()) << "\n";
        os << "signal_processes: " << format_strings_csv(signal_processes) << "\n";
        os << "poi_name: " << problem.poi_name << "\n";
        os << "converged: " << format_bool(result.converged) << "\n";
        os << "minimizer_status: " << result.minimizer_status << "\n";
        os << "minimizer_status_stat: " << result.minimizer_status_stat << "\n";
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
        os << "mu_err_syst_down: " << mu_err_syst_down << "\n";
        os << "mu_err_syst_up: " << mu_err_syst_up << "\n";
        os << "sigma_relation: sigma_fit = mu_hat * sigma_nominal\n";
        os << "channel_count: " << result.channels.size() << "\n";
        if (result.channels.size() == 1)
        {
            const auto &channel = result.channels.front();
            os << "selection_expr: "
               << (channel.selection_expr.empty() ? "-" : channel.selection_expr)
               << "\n";
            os << "observed_source_keys: "
               << format_strings_csv(channel.observed_source_keys) << "\n";
            os << "observed_bins: " << format_bins_csv(channel.observed) << "\n";
            os << "predicted_signal_bins: " << format_bins_csv(channel.predicted_signal) << "\n";
            os << "predicted_background_bins: " << format_bins_csv(channel.predicted_background) << "\n";
            os << "predicted_total_bins: " << format_bins_csv(channel.predicted_total) << "\n";
        }
        else
        {
            for (std::size_t i = 0; i < result.channels.size(); ++i)
            {
                const auto &channel = result.channels[i];
                os << "fit_channel[" << i << "]: " << channel.channel_key << "\n";
                os << "branch_expr[" << channel.channel_key << "]: "
                   << (channel.branch_expr.empty() ? "-" : channel.branch_expr) << "\n";
                os << "selection_expr[" << channel.channel_key << "]: "
                   << (channel.selection_expr.empty() ? "-" : channel.selection_expr) << "\n";
                os << "observed_source_keys[" << channel.channel_key << "]: "
                   << format_strings_csv(channel.observed_source_keys) << "\n";
                os << "observed_bins[" << channel.channel_key << "]: "
                   << format_bins_csv(channel.observed) << "\n";
                os << "predicted_signal_bins[" << channel.channel_key << "]: "
                   << format_bins_csv(channel.predicted_signal) << "\n";
                os << "predicted_background_bins[" << channel.channel_key << "]: "
                   << format_bins_csv(channel.predicted_background) << "\n";
                os << "predicted_total_bins[" << channel.channel_key << "]: "
                   << format_bins_csv(channel.predicted_total) << "\n";
            }
        }
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
                    cli_error("--manifest cannot be combined with legacy signal/background-specific cache flags"));
            }
            if (argc - i != 2)
                print_usage_and_throw();

            options.dist_path = argv[i] ? argv[i] : "";
            options.channel_key = argv[i + 1] ? argv[i + 1] : "";
        }
        else
        {
            if (argc - i != 4)
                print_usage_and_throw();

            options.dist_path = argv[i] ? argv[i] : "";
            options.signal_sample_key = argv[i + 1] ? argv[i + 1] : "";
            options.background_sample_key = argv[i + 2] ? argv[i + 2] : "";
            options.channel_key = argv[i + 3] ? argv[i + 3] : "";
        }

        if (options.signal_process.empty())
            throw std::runtime_error(cli_error("signal-process must not be empty"));
        return options;
    }
}

int main(int argc, char **argv)
{
    try
    {
        const CliOptions options = parse_args(argc, argv);

        DistributionIO dist(options.dist_path, DistributionIO::Mode::kRead);
        const DistributionIO::Metadata metadata = dist.metadata();
        const std::vector<fit::Channel> channels =
            (options.mode == InputMode::kManifest)
                ? build_manifest_channels(options, dist)
                : std::vector<fit::Channel>{build_legacy_channel(options, dist)};

        if (!options.allow_zero_data && all_zero_channels(channels))
        {
            throw std::runtime_error(
                cli_error("observed bins are all zero; pass --allow-zero-data to fit an empty observation intentionally"));
        }

        fit::Problem problem =
            fit::make_independent_problem(channels, options.mu_start, options.mu_upper);
        problem.measurement_name = options.channel_key;
        problem.mu_lower = options.mu_lower;
        problem.mu_upper = options.mu_upper;

        fit::FitOptions fit_options;
        fit_options.max_iterations = options.max_iterations;
        fit_options.max_function_calls = options.max_function_calls;
        fit_options.strategy = options.strategy;
        fit_options.print_level = options.print_level;
        fit_options.tolerance = options.tolerance;
        fit_options.compute_stat_only_interval = options.compute_stat_only_interval;
        fit_options.run_hesse = options.run_hesse;

        const fit::Result result = fit::profile_signal_strength(problem, fit_options);
        const std::string report =
            format_report(options.dist_path, metadata, options.channel_key, problem, result);

        if (!options.output_path.empty())
        {
            std::ofstream out(options.output_path);
            if (!out)
                throw std::runtime_error(cli_error("failed to open output file"));
            out << report;
            std::cout << kProgramName << ": wrote " << options.output_path
                      << " from " << options.dist_path
                      << " channel " << options.channel_key;
            if (options.mode == InputMode::kManifest)
                std::cout << " using manifest " << options.manifest_path;
            std::cout << "\n";
            return 0;
        }

        std::cout << report;
        return 0;
    }
    catch (const std::exception &e)
    {
        const std::string message = e.what();
        if (message.empty())
            return 0;
        std::cerr << (has_cli_prefix(message) ? message : cli_error(message)) << "\n";
        return 1;
    }
}
