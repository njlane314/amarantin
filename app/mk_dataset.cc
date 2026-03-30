#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DatasetIO.hh"
#include "SampleIO.hh"
#include "SampleBook.hh"

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
        std::string book_path;
        std::vector<SampleArg> samples;
    };

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_dataset [--book <sample-book.json>] "
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

    CliOptions parse_args(int argc, char **argv)
    {
        if (argc < 4)
            print_usage_and_throw();

        CliOptions options;
        int i = 1;
        for (; i < argc; ++i)
        {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg == "--book")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.book_path = argv[i] ? argv[i] : "";
                continue;
            }
            break;
        }

        if (argc - i < 3)
            print_usage_and_throw();

        options.output_path = argv[i] ? argv[i] : "";
        options.context = argv[i + 1] ? argv[i + 1] : "";

        for (i += 2; i < argc; ++i)
            options.samples.push_back(parse_sample_arg(argv[i] ? argv[i] : ""));

        return options;
    }
}

int main(int argc, char **argv)
{
    try
    {
        const CliOptions options = parse_args(argc, argv);
        const bool have_book = !options.book_path.empty();
        const SampleBook book = have_book ? SampleBook::read(options.book_path)
                                          : SampleBook{};

        DatasetIO dataset(options.output_path, options.context);
        for (const auto &sample_arg : options.samples)
        {
            SampleIO sample;
            sample.read(sample_arg.path);

            DatasetIO::Sample entry = sample.to_dataset_sample();
            if (have_book)
                book.stamp(sample_arg.key, entry);
            dataset.add_sample(sample_arg.key, entry);
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
