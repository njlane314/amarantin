#include <cctype>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
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
        std::string manifest_path;
        bool use_manifest = false;
        // single-request positional args
        std::string sample_key;
        std::string branch_expr;
        int nbins = 0;
        double xmin = 0.0;
        double xmax = 0.0;
        std::string selection_expr;
        // shared flags
        std::vector<std::string> detector_sample_keys;
        int fine_nbins = 0;
        bool enable_genie = false;
        bool enable_genie_knobs = false;
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

    std::string trim_copy(const std::string &input)
    {
        const auto first = input.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return "";
        const auto last = input.find_last_not_of(" \t\r\n");
        return input.substr(first, last - first + 1);
    }

    std::string strip_comment(const std::string &line)
    {
        const auto pos = line.find('#');
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

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_dist [--selection <expr>] [--detvars <csv>] [--fine-nbins <n>] "
              "[--genie] [--genie-knobs] [--flux] [--reint] [--no-overwrite] "
              "<output.root> <eventlist.root> <sample-key> <branch-expr> <nbins> <xmin> <xmax>\n"
              "   or: mk_dist [--detvars <csv>] [--fine-nbins <n>] "
              "[--genie] [--genie-knobs] [--flux] [--reint] [--no-overwrite] "
              "--manifest <requests.manifest> <output.root> <eventlist.root>\n"
              "\n"
              "manifest rows: <sample-key> <branch-expr> <nbins> <xmin> <xmax> [<selection-expr...>]\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_dist: invalid arguments");
    }

    std::vector<syst::CacheRequest> read_dist_manifest(
        const std::string &path,
        const std::vector<std::string> &default_detector_keys)
    {
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error("mk_dist: failed to open manifest: " + path);

        std::vector<syst::CacheRequest> requests;
        std::string line;
        int line_number = 0;
        while (std::getline(input, line))
        {
            ++line_number;
            const std::string trimmed = trim_copy(strip_comment(line));
            if (trimmed.empty())
                continue;

            const std::vector<std::string> fields = split_fields(trimmed);
            if (fields.size() < 5)
            {
                throw std::runtime_error(
                    "mk_dist: expected at least 5 fields (sample-key branch-expr nbins xmin xmax) "
                    "at line " + std::to_string(line_number) + " in " + path);
            }

            syst::CacheRequest request;
            request.sample_key = fields[0];
            request.branch_expr = fields[1];

            try { request.nbins = std::stoi(fields[2]); }
            catch (...) {
                throw std::runtime_error(
                    "mk_dist: invalid nbins at line " + std::to_string(line_number) + " in " + path);
            }
            try { request.xmin = std::stod(fields[3]); }
            catch (...) {
                throw std::runtime_error(
                    "mk_dist: invalid xmin at line " + std::to_string(line_number) + " in " + path);
            }
            try { request.xmax = std::stod(fields[4]); }
            catch (...) {
                throw std::runtime_error(
                    "mk_dist: invalid xmax at line " + std::to_string(line_number) + " in " + path);
            }

            // Fields 5+ are joined as the selection expression (handles spaces in expressions)
            for (std::size_t i = 5; i < fields.size(); ++i)
            {
                if (!request.selection_expr.empty())
                    request.selection_expr += " ";
                request.selection_expr += fields[i];
            }

            if (request.sample_key.empty())
                throw std::runtime_error(
                    "mk_dist: empty sample key at line " + std::to_string(line_number) + " in " + path);
            if (request.branch_expr.empty())
                throw std::runtime_error(
                    "mk_dist: empty branch expression at line " + std::to_string(line_number) + " in " + path);
            if (request.nbins <= 0)
                throw std::runtime_error(
                    "mk_dist: nbins must be positive at line " + std::to_string(line_number) + " in " + path);
            if (!(request.xmax > request.xmin))
                throw std::runtime_error(
                    "mk_dist: xmax must be greater than xmin at line " + std::to_string(line_number) + " in " + path);

            request.detector_sample_keys = default_detector_keys;
            requests.push_back(std::move(request));
        }

        if (requests.empty())
            throw std::runtime_error("mk_dist: manifest contains no requests: " + path);
        return requests;
    }

    CliOptions parse_args(int argc, char **argv)
    {
        CliOptions options;

        int i = 1;
        for (; i < argc; ++i)
        {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg == "--manifest")
            {
                if (++i >= argc) print_usage_and_throw();
                options.manifest_path = argv[i] ? argv[i] : "";
                options.use_manifest = true;
                continue;
            }
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
            if (arg == "--genie-knobs")
            {
                options.enable_genie_knobs = true;
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

        if (options.use_manifest)
        {
            if (argc - i != 2)
                print_usage_and_throw();
            options.output_path = argv[i] ? argv[i] : "";
            options.eventlist_path = argv[i + 1] ? argv[i + 1] : "";
        }
        else
        {
            if (argc - i != 7)
                print_usage_and_throw();

            options.output_path = argv[i] ? argv[i] : "";
            options.eventlist_path = argv[i + 1] ? argv[i + 1] : "";
            options.sample_key = argv[i + 2] ? argv[i + 2] : "";
            options.branch_expr = argv[i + 3] ? argv[i + 3] : "";
            options.nbins = std::stoi(argv[i + 4] ? argv[i + 4] : "");
            options.xmin = std::stod(argv[i + 5] ? argv[i + 5] : "");
            options.xmax = std::stod(argv[i + 6] ? argv[i + 6] : "");
        }
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

        // Validate cache provenance: if the dist file was previously built from
        // a different EventListIO, fail rather than silently mixing incompatible
        // caches.
        const std::string current_uuid = event_list.file_uuid();
        {
            std::string stored_uuid;
            try
            {
                stored_uuid = distfile.metadata().eventlist_uuid;
            }
            catch (...) {}

            if (!stored_uuid.empty() && stored_uuid != current_uuid)
            {
                throw std::runtime_error(
                    "mk_dist: distribution cache was built from a different EventListIO file "
                    "(UUID mismatch); delete the output file and rebuild from scratch");
            }
        }

        syst::CacheBuildOptions cache_options;
        cache_options.overwrite_existing = options.overwrite;
        cache_options.cache_nbins = options.fine_nbins;
        cache_options.enable_genie = options.enable_genie;
        cache_options.enable_genie_knobs = options.enable_genie_knobs;
        cache_options.enable_flux = options.enable_flux;
        cache_options.enable_reint = options.enable_reint;

        if (options.use_manifest)
        {
            cache_options.requests =
                read_dist_manifest(options.manifest_path, options.detector_sample_keys);
        }
        else
        {
            syst::CacheRequest request;
            request.sample_key = options.sample_key;
            request.branch_expr = options.branch_expr;
            request.nbins = options.nbins;
            request.xmin = options.xmin;
            request.xmax = options.xmax;
            request.selection_expr = options.selection_expr;
            request.detector_sample_keys = options.detector_sample_keys;
            cache_options.requests.push_back(request);
        }

        syst::build_systematics_cache(event_list, distfile, cache_options);

        // Stamp the EventListIO UUID into the distribution metadata so that
        // subsequent update invocations can detect stale caches.
        try
        {
            DistributionIO::Metadata meta = distfile.metadata();
            if (meta.eventlist_uuid.empty())
            {
                meta.eventlist_uuid = current_uuid;
                distfile.write_metadata(meta);
            }
        }
        catch (...) {}

        if (options.use_manifest)
        {
            std::cout << "mk_dist: wrote " << options.output_path
                      << " from event list " << options.eventlist_path
                      << " using manifest " << options.manifest_path
                      << " (" << cache_options.requests.size() << " request(s))\n";
        }
        else
        {
            std::cout << "mk_dist: wrote " << options.output_path
                      << " from event list " << options.eventlist_path
                      << " for sample " << options.sample_key << "\n";
        }
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "mk_dist: " << e.what() << "\n";
        return 1;
    }
}
