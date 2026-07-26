#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "CliPaths.hh"
#include "DatasetIO.hh"
#include "EventListBuild.hh"
#include "EventListIO.hh"

namespace
{
    struct HelpRequested final {};

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
              "[--event-tree <name>] [--subrun-tree <name>] "
              "<output.root> <dataset.root>\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_eventlist: invalid arguments");
    }

    [[noreturn]] void print_usage_and_throw_missing_value(const char *option,
                                                          const char *description)
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_eventlist: " + std::string(option) +
                                 " requires " + description);
    }

    [[noreturn]] void print_usage_and_throw_conflicting_selection_modes()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_eventlist: --preset and --selection are mutually exclusive");
    }

    void print_command_error(const std::string &message)
    {
        static const std::string prefix = "mk_eventlist: ";
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

    CliOptions parse_args(int argc, char **argv)
    {
        CliOptions options;
        bool saw_preset = false;
        bool saw_selection = false;

        int i = 1;
        for (; i < argc; ++i)
        {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg == "-h" || arg == "--help")
            {
                print_usage(std::cout);
                throw HelpRequested{};
            }
            if (arg == "--preset")
            {
                if (++i >= argc || looks_like_option_token(argv[i]))
                    print_usage_and_throw_missing_value("--preset", "a name");
                if (saw_selection)
                    print_usage_and_throw_conflicting_selection_modes();
                options.selection_name = argv[i] ? argv[i] : "";
                options.explicit_selection = false;
                saw_preset = true;
                continue;
            }
            if (arg == "--selection")
            {
                if (++i >= argc || looks_like_option_token(argv[i]))
                    print_usage_and_throw_missing_value("--selection", "an expression");
                if (saw_preset)
                    print_usage_and_throw_conflicting_selection_modes();
                options.selection_expr = argv[i] ? argv[i] : "";
                options.selection_name = "raw";
                options.explicit_selection = true;
                saw_selection = true;
                continue;
            }
            if (arg == "--event-tree")
            {
                if (++i >= argc || looks_like_option_token(argv[i]))
                    print_usage_and_throw_missing_value("--event-tree", "a name");
                options.event_tree_name = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--subrun-tree")
            {
                if (++i >= argc || looks_like_option_token(argv[i]))
                    print_usage_and_throw_missing_value("--subrun-tree", "a name");
                options.subrun_tree_name = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg.rfind("--", 0) == 0)
                throw std::runtime_error("mk_eventlist: unknown option: " + arg);
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
        cli::require_distinct_output_path(
            "mk_eventlist", options.output_path, "dataset", options.dataset_path);

        DatasetIO dataset(options.dataset_path);
        std::string selection_expr = options.selection_expr;
        if (!options.explicit_selection && options.selection_name != "raw")
            selection_expr.clear();

        ana::BuildConfig build_config;
        build_config.event_tree_name = options.event_tree_name;
        build_config.subrun_tree_name = options.subrun_tree_name;
        build_config.selection_expr = selection_expr;
        build_config.selection_name = options.selection_name;

        cli::write_file_atomically(
            options.output_path,
            "mk_eventlist: failed to publish output ROOT file",
            [&](const std::string &temporary_output_path)
            {
                EventListIO event_list(temporary_output_path, EventListIO::Mode::kWrite);
                ana::build_event_list(dataset, event_list, build_config);
            });

        std::cout << "mk_eventlist: wrote " << options.output_path
                  << " from dataset " << options.dataset_path << "\n";
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
