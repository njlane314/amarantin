#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "SampleIO.hh"

namespace
{
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
                throw std::runtime_error("");
            }
            if (arg == "--run-db")
            {
                if (i + 1 >= argc)
                    throw std::runtime_error("mk_sample: --run-db requires a path");
                options.run_db_path = argv[++i];
                continue;
            }
            if (arg == "--sample")
            {
                if (i + 1 >= argc)
                    throw std::runtime_error("mk_sample: --sample requires a name");
                options.sample = argv[++i] ? argv[i] : "";
                continue;
            }
            if (arg == "--manifest")
            {
                if (i + 1 >= argc)
                    throw std::runtime_error("mk_sample: --manifest requires a path");
                options.manifest_path = argv[++i] ? argv[i] : "";
                continue;
            }
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
}

int main(int argc, char **argv)
{
    try
    {
        const CliOptions options = parse_args(argc, argv);

        SampleIO sample;
        if (options.use_manifest)
        {
            sample.build_from_manifest(options.sample,
                                       options.manifest_path,
                                       options.origin,
                                       options.variation,
                                       options.beam,
                                       options.polarity,
                                       options.run_db_path);
        }
        else
        {
            sample.build(options.list_path,
                         options.origin,
                         options.variation,
                         options.beam,
                         options.polarity,
                         options.run_db_path);
        }
        sample.write(options.output_path);

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
    catch (const std::exception &e)
    {
        if (std::string(e.what()).empty())
            return 0;
        std::cerr << "mk_sample: " << e.what() << "\n";
        return 1;
    }
}
