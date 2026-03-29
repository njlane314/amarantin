#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "DatasetIO.hh"
#include "EventListIO.hh"

namespace
{
    struct CliOptions
    {
        std::string output_path;
        std::string dataset_path;
        std::string event_tree_name = "EventSelectionFilter";
        std::string subrun_tree_name = "SubRun";
        std::string selection_expr = "selected != 0";
    };

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_eventlist <output.root> <dataset.root> [event-tree] [subrun-tree] [selection]\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_eventlist: invalid arguments");
    }

    CliOptions parse_args(int argc, char **argv)
    {
        if (argc < 3 || argc > 6)
            print_usage_and_throw();

        CliOptions options;
        options.output_path = argv[1] ? argv[1] : "";
        options.dataset_path = argv[2] ? argv[2] : "";
        if (argc > 3) options.event_tree_name = argv[3];
        if (argc > 4) options.subrun_tree_name = argv[4];
        if (argc > 5) options.selection_expr = argv[5];
        return options;
    }
}

int main(int argc, char **argv)
{
    try
    {
        const CliOptions options = parse_args(argc, argv);

        DatasetIO dataset(options.dataset_path);
        EventListIO event_list(options.output_path, EventListIO::Mode::kWrite);
        event_list.skim(dataset,
                        options.event_tree_name,
                        options.subrun_tree_name,
                        options.selection_expr);

        std::cout << "mk_eventlist: wrote " << options.output_path
                  << " from dataset " << options.dataset_path << "\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "mk_eventlist: " << e.what() << "\n";
        return 1;
    }
}
