#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "DatasetIO.hh"
#include "EventListIO.hh"
#include "EventListSelection.hh"

namespace
{
    struct CliOptions
    {
        std::string output_path;
        std::string dataset_path;
        std::string event_tree_name = "EventSelectionFilter";
        std::string subrun_tree_name = "SubRun";
        std::string selection_expr = "selected != 0";
        std::string selection_name = "raw";
        bool explicit_selection = false;
    };

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_eventlist [--preset <name> | --selection <expr>] "
              "[--event-tree <name>] [--subrun-tree <name>] <output.root> <dataset.root>\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_eventlist: invalid arguments");
    }

    CliOptions parse_args(int argc, char **argv)
    {
        if (argc < 3)
            print_usage_and_throw();

        CliOptions options;

        int i = 1;
        for (; i < argc; ++i)
        {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg == "--preset")
            {
                if (++i >= argc) print_usage_and_throw();
                options.selection_name = argv[i] ? argv[i] : "";
                options.explicit_selection = false;
                continue;
            }
            if (arg == "--selection")
            {
                if (++i >= argc) print_usage_and_throw();
                options.selection_expr = argv[i] ? argv[i] : "";
                options.selection_name = "raw";
                options.explicit_selection = true;
                continue;
            }
            if (arg == "--event-tree")
            {
                if (++i >= argc) print_usage_and_throw();
                options.event_tree_name = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--subrun-tree")
            {
                if (++i >= argc) print_usage_and_throw();
                options.subrun_tree_name = argv[i] ? argv[i] : "";
                continue;
            }
            break;
        }

        if (argc - i != 2)
            print_usage_and_throw();

        options.output_path = argv[i] ? argv[i] : "";
        options.dataset_path = argv[i + 1] ? argv[i + 1] : "";
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

        std::string selection_expr = options.selection_expr;
        if (!options.explicit_selection && options.selection_name != "raw")
            selection_expr.clear();

        event_list.skim(dataset,
                        options.event_tree_name,
                        options.subrun_tree_name,
                        selection_expr,
                        options.selection_name);

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
