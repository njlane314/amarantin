#include <cctype>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DistributionIO.hh"
#include "EventListIO.hh"
#include "Systematics.hh"

namespace
{
    struct CliOptions
    {
        std::string output_path;
        std::string eventlist_path;
        std::string sample_key;
        std::string branch_expr;
        int nbins = 0;
        double xmin = 0.0;
        double xmax = 0.0;
        std::string selection_expr;
        std::vector<std::string> detector_sample_keys;
        int fine_nbins = 0;
        bool enable_genie = false;
        bool enable_flux = false;
        bool enable_reint = false;
        bool overwrite = true;
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
        os << "usage: mk_dist [--selection <expr>] [--detvars <csv>] [--fine-nbins <n>] "
              "[--genie] [--flux] [--reint] [--no-overwrite] "
              "<output.root> <eventlist.root> <sample-key> <branch-expr> <nbins> <xmin> <xmax>\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_dist: invalid arguments");
    }

    CliOptions parse_args(int argc, char **argv)
    {
        if (argc < 8)
            print_usage_and_throw();

        CliOptions options;

        int i = 1;
        for (; i < argc; ++i)
        {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg == "--selection")
            {
                if (++i >= argc) print_usage_and_throw();
                options.selection_expr = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--detvars")
            {
                if (++i >= argc) print_usage_and_throw();
                options.detector_sample_keys = split_csv(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--fine-nbins")
            {
                if (++i >= argc) print_usage_and_throw();
                options.fine_nbins = std::stoi(argv[i] ? argv[i] : "");
                continue;
            }
            if (arg == "--genie")
            {
                options.enable_genie = true;
                continue;
            }
            if (arg == "--flux")
            {
                options.enable_flux = true;
                continue;
            }
            if (arg == "--reint")
            {
                options.enable_reint = true;
                continue;
            }
            if (arg == "--no-overwrite")
            {
                options.overwrite = false;
                continue;
            }
            break;
        }

        if (argc - i != 7)
            print_usage_and_throw();

        options.output_path = argv[i] ? argv[i] : "";
        options.eventlist_path = argv[i + 1] ? argv[i + 1] : "";
        options.sample_key = argv[i + 2] ? argv[i + 2] : "";
        options.branch_expr = argv[i + 3] ? argv[i + 3] : "";
        options.nbins = std::stoi(argv[i + 4] ? argv[i + 4] : "");
        options.xmin = std::stod(argv[i + 5] ? argv[i + 5] : "");
        options.xmax = std::stod(argv[i + 6] ? argv[i + 6] : "");
        return options;
    }
}

int main(int argc, char **argv)
{
    try
    {
        const CliOptions options = parse_args(argc, argv);

        EventListIO event_list(options.eventlist_path, EventListIO::Mode::kRead);
        DistributionIO distfile(options.output_path, DistributionIO::Mode::kUpdate);

        syst::CacheBuildOptions cache_options;
        cache_options.overwrite_existing = options.overwrite;
        cache_options.cache_nbins = options.fine_nbins;
        cache_options.enable_genie = options.enable_genie;
        cache_options.enable_flux = options.enable_flux;
        cache_options.enable_reint = options.enable_reint;

        syst::CacheRequest request;
        request.sample_key = options.sample_key;
        request.branch_expr = options.branch_expr;
        request.nbins = options.nbins;
        request.xmin = options.xmin;
        request.xmax = options.xmax;
        request.selection_expr = options.selection_expr;
        request.detector_sample_keys = options.detector_sample_keys;
        cache_options.requests.push_back(request);

        syst::build_systematics_cache(event_list, distfile, cache_options);

        std::cout << "mk_dist: wrote " << options.output_path
                  << " from event list " << options.eventlist_path
                  << " for sample " << options.sample_key << "\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "mk_dist: " << e.what() << "\n";
        return 1;
    }
}
