#include <exception>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "CliPaths.hh"
#include "SampleIO.hh"

namespace
{
    struct HelpRequested final {};

    struct CliOptions
    {
        std::string output_path;
        std::string list_path;
        std::string manifest_path;
        std::string sample;
        std::string origin = "data";
        std::string variation = "nominal";
        std::string beam = "numi";
        std::string polarity;
        std::string run_db_path;
        bool use_manifest = false;
    };

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_sample [--run-db <path>] --sample <name> --manifest <sample.manifest> <output.root> [origin] [variation] [beam] [polarity]\n";
        os << "       mk_sample [--run-db <path>] <output.root> <input.list> [origin] [variation] [beam] [polarity]\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_sample: invalid arguments");
    }

    void print_command_error(const std::string &message)
    {
        static const std::string prefix = "mk_sample: ";
        if (message.rfind(prefix, 0) == 0)
        {
            std::cerr << message << "\n";
            return;
        }
        std::cerr << prefix << message << "\n";
    }

    bool looks_like_option_token(const char *arg)
    {
        if (!arg)
            return true;
        const std::string value = arg;
        return value == "-h" || value == "--help" || value.rfind("--", 0) == 0;
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

    std::vector<std::string> read_list_file(const std::string &path)
    {
        std::ifstream file(path);
        if (!file)
            throw std::runtime_error("mk_sample: failed to open input path file: " + path);

        std::vector<std::string> paths;
        std::string line;
        while (std::getline(file, line))
        {
            const std::string trimmed = trim_copy(strip_comment(line));
            if (trimmed.empty())
                continue;
            paths.push_back(trimmed);
        }

        if (paths.empty())
            throw std::runtime_error("mk_sample: no input paths found in file: " + path);
        return paths;
    }

    std::vector<SampleIO::ShardInput> read_manifest(const std::string &path)
    {
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error("mk_sample: failed to open manifest: " + path);

        std::vector<SampleIO::ShardInput> shards;
        std::set<std::string> shard_names;
        std::string line;
        int line_number = 0;
        while (std::getline(input, line))
        {
            ++line_number;
            const std::string trimmed = trim_copy(strip_comment(line));
            if (trimmed.empty())
                continue;

            const std::vector<std::string> fields = split_fields(trimmed);
            if (fields.size() != 2)
            {
                throw std::runtime_error("mk_sample: expected 2 fields at line " +
                                         std::to_string(line_number) + " in " + path);
            }

            SampleIO::ShardInput shard;
            shard.shard = fields[0];
            shard.sample_list_path = fields[1];
            if (shard.shard.empty())
            {
                throw std::runtime_error("mk_sample: empty shard name at line " +
                                         std::to_string(line_number) + " in " + path);
            }
            if (!shard_names.insert(shard.shard).second)
            {
                throw std::runtime_error("mk_sample: duplicate shard name '" +
                                         shard.shard + "' in " + path);
            }

            shards.push_back(std::move(shard));
        }

        if (shards.empty())
            throw std::runtime_error("mk_sample: manifest is empty: " + path);
        return shards;
    }

    std::vector<SampleIO::ShardInput> resolve_shards(const CliOptions &options)
    {
        if (options.use_manifest)
        {
            if (trim_copy(options.sample).empty())
                throw std::runtime_error("mk_sample: sample name must not be empty");
            return read_manifest(options.manifest_path);
        }

        const std::string list_path = trim_copy(options.list_path);
        if (list_path.empty())
            throw std::runtime_error("mk_sample: input list path is empty");

        if (list_path.front() != '@')
            return {SampleIO::ShardInput{"", list_path}};

        const std::string path_file = trim_copy(list_path.substr(1));
        if (path_file.empty())
            throw std::runtime_error("mk_sample: input path file is empty");

        std::vector<SampleIO::ShardInput> shards;
        const auto paths = read_list_file(path_file);
        shards.reserve(paths.size());
        for (const auto &path : paths)
            shards.push_back(SampleIO::ShardInput{"", path});
        return shards;
    }

    CliOptions parse_args(int argc, char **argv)
    {
        CliOptions options;
        std::vector<std::string> positional;

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg == "-h" || arg == "--help")
            {
                print_usage(std::cout);
                throw HelpRequested{};
            }
            if (arg == "--run-db")
            {
                if (++i >= argc || looks_like_option_token(argv[i]))
                    throw std::runtime_error("mk_sample: --run-db requires a path");
                options.run_db_path = argv[i];
                continue;
            }
            if (arg == "--sample")
            {
                if (++i >= argc || looks_like_option_token(argv[i]))
                    throw std::runtime_error("mk_sample: --sample requires a name");
                options.sample = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--manifest")
            {
                if (++i >= argc || looks_like_option_token(argv[i]))
                    throw std::runtime_error("mk_sample: --manifest requires a path");
                options.manifest_path = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg.rfind("--", 0) == 0)
                throw std::runtime_error("mk_sample: unknown option: " + arg);
            positional.push_back(arg);
        }

        const bool have_sample = !options.sample.empty();
        const bool have_manifest = !options.manifest_path.empty();
        if (have_sample != have_manifest)
            throw std::runtime_error("mk_sample: --sample and --manifest must be provided together");
        options.use_manifest = have_sample && have_manifest;

        if (options.use_manifest)
        {
            if (positional.size() < 1 || positional.size() > 5)
                print_usage_and_throw();

            options.output_path = positional[0];
            if (positional.size() > 1) options.origin = positional[1];
            if (positional.size() > 2) options.variation = positional[2];
            if (positional.size() > 3) options.beam = positional[3];
            if (positional.size() > 4) options.polarity = positional[4];
        }
        else
        {
            if (positional.size() < 2 || positional.size() > 6)
                print_usage_and_throw();

            options.output_path = positional[0];
            options.list_path = positional[1];
            if (positional.size() > 2) options.origin = positional[2];
            if (positional.size() > 3) options.variation = positional[3];
            if (positional.size() > 4) options.beam = positional[4];
            if (positional.size() > 5) options.polarity = positional[5];
        }

        if (options.polarity.empty())
        {
            const SampleIO::Beam beam = SampleIO::beam_from(options.beam);
            options.polarity = (beam == SampleIO::Beam::kNuMI) ? "fhc" : "unknown";
        }

        return options;
    }

    void validate_input_paths(const CliOptions &options,
                              const std::vector<SampleIO::ShardInput> &shards)
    {
        if (options.use_manifest)
        {
            cli::require_distinct_output_path(
                "mk_sample", options.output_path, "manifest", options.manifest_path);
        }
        else
        {
            const std::string list_path = trim_copy(options.list_path);
            if (!list_path.empty() && list_path.front() == '@')
            {
                cli::require_distinct_output_path(
                    "mk_sample",
                    options.output_path,
                    "path list",
                    trim_copy(list_path.substr(1)));
            }
        }

        const std::string run_db_path = trim_copy(
            options.run_db_path.empty() ? SampleIO::default_run_db_path()
                                        : options.run_db_path);
        if (!run_db_path.empty())
        {
            cli::require_distinct_output_path(
                "mk_sample", options.output_path, "run database", run_db_path);
        }

        for (const auto &shard : shards)
        {
            cli::require_distinct_output_path(
                "mk_sample", options.output_path, "sample list", shard.sample_list_path);
        }
    }

    void validate_root_input_paths(const std::string &output_path,
                                   const SampleIO &sample)
    {
        for (const auto &shard : sample.shards_)
        {
            for (const auto &input_path : shard.files())
            {
                cli::require_distinct_output_path(
                    "mk_sample", output_path, "input ROOT", input_path);
            }
        }
    }
}

int main(int argc, char **argv)
{
    try
    {
        const CliOptions options = parse_args(argc, argv);
        const std::vector<SampleIO::ShardInput> shards = resolve_shards(options);
        validate_input_paths(options, shards);

        SampleIO sample;
        sample.build(options.use_manifest ? options.sample : std::string(),
                     shards,
                     options.origin,
                     options.variation,
                     options.beam,
                     options.polarity,
                     options.run_db_path);
        validate_root_input_paths(options.output_path, sample);
        cli::write_file_atomically(
            options.output_path,
            "mk_sample: failed to publish output ROOT file",
            [&](const std::string &temporary_output_path)
            {
                sample.write(temporary_output_path, options.output_path);
            });

        std::cout << "mk_sample: wrote " << options.output_path;
        if (options.use_manifest)
        {
            std::cout << " for sample " << options.sample
                      << " from manifest " << options.manifest_path;
        }
        else
        {
            std::cout << " from " << options.list_path;
        }
        std::cout << "\n";
        return 0;
    }
    catch (const HelpRequested &)
    {
        return 0;
    }
    catch (const std::exception &e)
    {
        print_command_error(e.what());
        return 1;
    }
}
