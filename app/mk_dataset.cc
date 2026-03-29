#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DatasetIO.hh"
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
        std::vector<SampleArg> samples;
    };

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_dataset <output.root> <context> <sample-key=sample.root> [sample-key=sample.root ...]\n";
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

    CliOptions parse_args(int argc, char **argv)
    {
        if (argc < 4)
            print_usage_and_throw();

        CliOptions options;
        options.output_path = argv[1] ? argv[1] : "";
        options.context = argv[2] ? argv[2] : "";

        for (int i = 3; i < argc; ++i)
            options.samples.push_back(parse_sample_arg(argv[i] ? argv[i] : ""));

        return options;
    }
}

int main(int argc, char **argv)
{
    try
    {
        const CliOptions options = parse_args(argc, argv);

        DatasetIO dataset(options.output_path, options.context);
        for (const auto &sample_arg : options.samples)
        {
            SampleIO sample;
            sample.read(sample_arg.path);
            dataset.add_sample(sample_arg.key, sample.to_dataset_sample());
        }

        std::cout << "mk_dataset: wrote " << options.output_path
                  << " with " << options.samples.size() << " samples\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "mk_dataset: " << e.what() << "\n";
        return 1;
    }
}
