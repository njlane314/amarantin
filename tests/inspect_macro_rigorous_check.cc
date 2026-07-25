#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DistributionIO.hh"

void inspect_dist(const char *path = "output.dists.root",
                  const char *sample_key = "beam-s0",
                  const char *cache_key = nullptr);
void inspect_systematics(const char *path = "output.dists.root",
                         const char *sample_key = "beam-s0",
                         const char *cache_key = nullptr);

namespace
{
    struct TempDir
    {
        std::filesystem::path path;

        ~TempDir()
        {
            if (path.empty())
                return;
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };

    [[noreturn]] void fail(const std::string &message)
    {
        throw std::runtime_error("inspect_macro_rigorous_check: " + message);
    }

    void require_throws(const std::function<void()> &fn,
                        const std::string &needle,
                        const std::string &label)
    {
        try
        {
            fn();
        }
        catch (const std::exception &error)
        {
            const std::string message = error.what();
            if (message.find(needle) == std::string::npos)
                fail(label + ": unexpected exception message: " + message);
            return;
        }

        fail(label + ": expected an exception");
    }

    TempDir make_temp_dir()
    {
        const std::string templ =
            (std::filesystem::temp_directory_path() / "amarantin-inspect-macro.XXXXXX").string();
        std::vector<char> buffer(templ.begin(), templ.end());
        buffer.push_back('\0');
        char *dir = mkdtemp(buffer.data());
        if (!dir)
            fail("failed to create temporary directory");

        TempDir out;
        out.path = dir;
        return out;
    }

    DistributionIO::Spectrum make_spectrum(const std::string &cache_key,
                                           double first_bin)
    {
        DistributionIO::Spectrum spectrum;
        spectrum.spec.sample_key = "beam";
        spectrum.spec.branch_expr = "x";
        spectrum.spec.selection_expr = "1";
        spectrum.spec.cache_key = cache_key;
        spectrum.spec.nbins = 2;
        spectrum.spec.xmin = 0.0;
        spectrum.spec.xmax = 2.0;
        spectrum.nominal = {first_bin, 1.0};
        spectrum.sumw2 = {first_bin, 1.0};
        return spectrum;
    }

    void write_distribution(const std::filesystem::path &path)
    {
        DistributionIO dist(path.string(), DistributionIO::Mode::kWrite);

        DistributionIO::Metadata metadata;
        metadata.eventlist_path = "synthetic.eventlist.root";
        metadata.build_version = 1;
        dist.write_metadata(metadata);
        dist.write("beam", "shape-a", make_spectrum("shape-a", 1.0));
        dist.write("beam", "shape-b", make_spectrum("shape-b", 2.0));
        dist.flush();
    }
}

int main()
{
    try
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path dist_path = temp.path / "multi-cache.dists.root";
        write_distribution(dist_path);

        require_throws(
            [&]()
            {
                inspect_dist(dist_path.string().c_str(), "beam", nullptr);
            },
            "sample_key has multiple cached distributions; pass cache_key",
            "inspect_dist should reject ambiguous cache selection");

        require_throws(
            [&]()
            {
                inspect_systematics(dist_path.string().c_str(), "beam", nullptr);
            },
            "sample_key has multiple cached distributions; pass cache_key",
            "inspect_systematics should reject ambiguous cache selection");

        inspect_systematics(dist_path.string().c_str(), "beam", "shape-a");

        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
