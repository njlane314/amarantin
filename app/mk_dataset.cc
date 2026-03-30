#include <exception>
#include <fstream>
#include <iostream>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

#include "DatasetIO.hh"
#include "SampleDef.hh"
#include "SampleIO.hh"

namespace
{
    struct SampleArg
    {
        std::string key;
        std::string path;
    };

    struct CliOptions
    {
        std::string output_path;
        std::string context;
        std::string defs_path;
        std::string manifest_path;
        std::vector<SampleArg> samples;
    };

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_dataset [--defs <sample.defs>] [--manifest <dataset.manifest>] "
              "<output.root> <context> <sample-key=sample.root> [sample-key=sample.root ...]\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_dataset: invalid arguments");
    }

    SampleArg parse_sample_arg(const std::string &arg)
    {
        const std::string::size_type pos = arg.find('=');
        if (pos == std::string::npos || pos == 0 || pos + 1 >= arg.size())
            throw std::runtime_error("mk_dataset: expected sample argument of the form key=path");

        SampleArg out;
        out.key = arg.substr(0, pos);
        out.path = arg.substr(pos + 1);
        return out;
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

    std::vector<SampleArg> read_manifest(const std::string &path)
    {
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error("mk_dataset: failed to open manifest: " + path);

        std::vector<SampleArg> out;
        std::string line;
        int line_number = 0;
        while (std::getline(input, line))
        {
            ++line_number;
            const std::string trimmed = trim_copy(strip_comment(line));
            if (trimmed.empty())
                continue;

            if (trimmed.find('=') != std::string::npos)
            {
                out.push_back(parse_sample_arg(trimmed));
                continue;
            }

            const std::vector<std::string> fields = split_fields(trimmed);
            if (fields.size() != 2)
            {
                throw std::runtime_error("mk_dataset: expected 2 fields in manifest at line " +
                                         std::to_string(line_number) + " in " + path);
            }

            out.push_back(SampleArg{fields[0], fields[1]});
        }

        return out;
    }

    struct LogicalSample
    {
        std::string key;
        std::vector<std::string> paths;
    };

    std::vector<LogicalSample> group_samples(const std::vector<SampleArg> &samples)
    {
        std::vector<LogicalSample> out;
        for (const auto &sample : samples)
        {
            auto it = std::find_if(out.begin(), out.end(),
                                   [&](const LogicalSample &logical) { return logical.key == sample.key; });
            if (it == out.end())
            {
                out.push_back(LogicalSample{sample.key, {sample.path}});
                continue;
            }
            it->paths.push_back(sample.path);
        }
        return out;
    }

    [[noreturn]] void throw_merge_conflict(const std::string &key,
                                           const std::string &field,
                                           const std::string &lhs,
                                           const std::string &rhs)
    {
        throw std::runtime_error("mk_dataset: cannot merge sample key " + key +
                                 " because " + field + " differs (" + lhs + " vs " + rhs + ")");
    }

    template <class EnumType>
    void require_equal(const std::string &key,
                       const char *field,
                       EnumType lhs,
                       EnumType rhs,
                       const char *(*name_fn)(EnumType))
    {
        if (lhs != rhs)
            throw_merge_conflict(key, field, name_fn(lhs), name_fn(rhs));
    }

    double merged_normalisation(const std::vector<DatasetIO::Sample> &parts,
                                double subrun_pot_sum,
                                double db_tortgt_pot_sum)
    {
        if (subrun_pot_sum > 0.0 && std::isfinite(subrun_pot_sum))
        {
            if (db_tortgt_pot_sum > 0.0 && std::isfinite(db_tortgt_pot_sum))
                return db_tortgt_pot_sum / subrun_pot_sum;
            return 1.0;
        }

        double candidate = 0.0;
        for (const auto &part : parts)
        {
            if (!(part.normalisation > 0.0) || !std::isfinite(part.normalisation))
                continue;
            if (!(candidate > 0.0))
            {
                candidate = part.normalisation;
                continue;
            }

            const double scale = std::max({1.0, std::fabs(candidate), std::fabs(part.normalisation)});
            if (std::fabs(candidate - part.normalisation) > 1e-12 * scale)
                return 1.0;
        }

        return candidate > 0.0 ? candidate : 1.0;
    }

    DatasetIO::Sample merge_samples(const std::string &key,
                                    const std::vector<DatasetIO::Sample> &parts)
    {
        if (parts.empty())
            throw std::runtime_error("mk_dataset: cannot merge empty sample group for key " + key);

        DatasetIO::Sample merged = parts.front();
        merged.subrun_pot_sum = 0.0;
        merged.db_tortgt_pot_sum = 0.0;
        merged.normalisation = 1.0;
        merged.provenance_list.clear();
        merged.root_files.clear();

        for (const auto &part : parts)
        {
            require_equal(key, "origin", merged.origin, part.origin, DatasetIO::Sample::origin_name);
            require_equal(key, "variation", merged.variation, part.variation, DatasetIO::Sample::variation_name);
            require_equal(key, "beam", merged.beam, part.beam, DatasetIO::Sample::beam_name);
            require_equal(key, "polarity", merged.polarity, part.polarity, DatasetIO::Sample::polarity_name);

            merged.subrun_pot_sum += part.subrun_pot_sum;
            merged.db_tortgt_pot_sum += part.db_tortgt_pot_sum;
            merged.provenance_list.insert(merged.provenance_list.end(),
                                          part.provenance_list.begin(),
                                          part.provenance_list.end());
            merged.root_files.insert(merged.root_files.end(),
                                     part.root_files.begin(),
                                     part.root_files.end());
        }

        std::sort(merged.root_files.begin(), merged.root_files.end());
        merged.root_files.erase(std::unique(merged.root_files.begin(), merged.root_files.end()),
                                merged.root_files.end());
        merged.normalisation =
            merged_normalisation(parts, merged.subrun_pot_sum, merged.db_tortgt_pot_sum);
        return merged;
    }

    CliOptions parse_args(int argc, char **argv)
    {
        if (argc < 4)
            print_usage_and_throw();

        CliOptions options;
        int i = 1;
        for (; i < argc; ++i)
        {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg == "--defs")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.defs_path = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--manifest")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.manifest_path = argv[i] ? argv[i] : "";
                continue;
            }
            break;
        }

        if (argc - i < 2)
            print_usage_and_throw();

        options.output_path = argv[i] ? argv[i] : "";
        options.context = argv[i + 1] ? argv[i + 1] : "";

        for (i += 2; i < argc; ++i)
            options.samples.push_back(parse_sample_arg(argv[i] ? argv[i] : ""));

        if (options.samples.empty() && options.manifest_path.empty())
            print_usage_and_throw();

        return options;
    }
}

int main(int argc, char **argv)
{
    try
    {
        const CliOptions options = parse_args(argc, argv);
        const bool have_defs = !options.defs_path.empty();
        const std::vector<ana::SampleDef> defs = have_defs ? ana::read_sample_defs(options.defs_path)
                                                           : std::vector<ana::SampleDef>{};
        std::vector<SampleArg> sample_args = options.samples;
        if (!options.manifest_path.empty())
        {
            const std::vector<SampleArg> manifest_args = read_manifest(options.manifest_path);
            sample_args.insert(sample_args.end(), manifest_args.begin(), manifest_args.end());
        }
        const std::vector<LogicalSample> logical_samples = group_samples(sample_args);

        DatasetIO dataset(options.output_path, options.context);
        for (const auto &logical : logical_samples)
        {
            std::vector<DatasetIO::Sample> concrete_samples;
            concrete_samples.reserve(logical.paths.size());
            for (const auto &path : logical.paths)
            {
                SampleIO sample;
                sample.read(path);
                concrete_samples.push_back(sample.to_dataset_sample());
            }

            DatasetIO::Sample entry = merge_samples(logical.key, concrete_samples);
            if (have_defs)
                ana::apply_sample_defs(defs, logical.key, entry);
            dataset.add_sample(logical.key, entry);
        }

        std::cout << "mk_dataset: wrote " << options.output_path
                  << " with " << logical_samples.size() << " logical samples";
        if (!options.manifest_path.empty())
            std::cout << " from manifest " << options.manifest_path;
        std::cout << "\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "mk_dataset: " << e.what() << "\n";
        return 1;
    }
}
