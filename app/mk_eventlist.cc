#include <cctype>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DatasetIO.hh"
#include "EventListBuild.hh"
#include "EventListIO.hh"
#include "Systematics.hh"

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

        bool cache_systematics = false;
        std::string cache_sample_key;
        std::string cache_branch_expr;
        int cache_nbins = 0;
        double cache_xmin = 0.0;
        double cache_xmax = 0.0;
        std::string cache_selection_expr;
        std::vector<std::string> cache_detector_sample_keys;
        int cache_fine_nbins = 0;
        bool cache_enable_genie = false;
        bool cache_enable_flux = false;
        bool cache_enable_reint = false;
        bool cache_overwrite = true;
    };

    std::vector<std::string> split_csv(const std::string &csv)
    {
        std::vector<std::string> out;
        std::string current;
        for (char c : csv)
        {
            if (c == ',')
            {
                if (!current.empty())
                {
                    out.push_back(current);
                    current.clear();
                }
                continue;
            }
            if (!std::isspace(static_cast<unsigned char>(c)))
                current.push_back(c);
        }
        if (!current.empty())
            out.push_back(current);
        return out;
    }

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_eventlist [--preset <name> | --selection <expr>] "
              "[--event-tree <name>] [--subrun-tree <name>] "
              "[--cache-systematics <sample-key> <branch-expr> <nbins> <xmin> <xmax>] "
              "[--cache-selection <expr>] [--cache-detvars <csv>] [--cache-fine-nbins <n>] "
              "[--cache-genie] [--cache-flux] [--cache-reint] [--cache-no-overwrite] "
              "<output.root> <dataset.root>\n";
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
            if (arg == "--cache-systematics")
            {
                if (i + 5 >= argc) print_usage_and_throw();
                options.cache_systematics = true;
                options.cache_sample_key = argv[++i] ? argv[i] : "";
                options.cache_branch_expr = argv[++i] ? argv[i] : "";
                options.cache_nbins = std::stoi(argv[++i] ? argv[i] : "");
                options.cache_xmin = std::stod(argv[++i] ? argv[i] : "");
                options.cache_xmax = std::stod(argv[++i] ? argv[i] : "");
                continue;
            }
            if (arg == "--cache-selection")
            {
                if (++i >= argc) print_usage_and_throw();
                options.cache_selection_expr = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--cache-detvars")
            {
                if (++i >= argc) print_usage_and_throw();
                options.cache_detector_sample_keys = split_csv(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--cache-fine-nbins")
            {
                if (++i >= argc) print_usage_and_throw();
                options.cache_fine_nbins = std::stoi(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--cache-genie")
            {
                options.cache_enable_genie = true;
                continue;
            }
            if (arg == "--cache-flux")
            {
                options.cache_enable_flux = true;
                continue;
            }
            if (arg == "--cache-reint")
            {
                options.cache_enable_reint = true;
                continue;
            }
            if (arg == "--cache-no-overwrite")
            {
                options.cache_overwrite = false;
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
        std::string selection_expr = options.selection_expr;
        if (!options.explicit_selection && options.selection_name != "raw")
            selection_expr.clear();

        {
            EventListIO event_list(options.output_path, EventListIO::Mode::kWrite);

            ana::BuildConfig build_config;
            build_config.event_tree_name = options.event_tree_name;
            build_config.subrun_tree_name = options.subrun_tree_name;
            build_config.selection_expr = selection_expr;
            build_config.selection_name = options.selection_name;

            ana::build_event_list(dataset, event_list, build_config);
        }

        if (options.cache_systematics)
        {
            EventListIO event_list(options.output_path, EventListIO::Mode::kUpdate);

            syst::CacheBuildOptions cache_options;
            cache_options.overwrite_existing = options.cache_overwrite;
            cache_options.cache_nbins = options.cache_fine_nbins;
            cache_options.enable_genie = options.cache_enable_genie;
            cache_options.enable_flux = options.cache_enable_flux;
            cache_options.enable_reint = options.cache_enable_reint;

            syst::CacheRequest request;
            request.sample_key = options.cache_sample_key;
            request.branch_expr = options.cache_branch_expr;
            request.nbins = options.cache_nbins;
            request.xmin = options.cache_xmin;
            request.xmax = options.cache_xmax;
            request.selection_expr = options.cache_selection_expr;
            request.detector_sample_keys = options.cache_detector_sample_keys;
            cache_options.requests.push_back(request);

            syst::build_systematics_cache(event_list, cache_options);
        }

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
