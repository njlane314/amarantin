#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "DistributionIO.hh"

#include "TFile.h"

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
        throw std::runtime_error("mk_cov_rigorous_check: " + message);
    }

    void require(bool condition, const std::string &message)
    {
        if (!condition)
            fail(message);
    }

    TempDir make_temp_dir()
    {
        const std::string templ =
            (std::filesystem::temp_directory_path() / "amarantin-mk-cov-rigorous.XXXXXX").string();
        std::vector<char> buffer(templ.begin(), templ.end());
        buffer.push_back('\0');
        char *dir = mkdtemp(buffer.data());
        if (!dir)
            fail("failed to create temporary directory");

        TempDir out;
        out.path = dir;
        return out;
    }

    std::string read_text(const std::filesystem::path &path)
    {
        std::ifstream input(path);
        if (!input)
            fail("failed to open log file: " + path.string());

        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    }

    std::string shell_quote(const std::string &text)
    {
        std::string out = "'";
        for (const char ch : text)
        {
            if (ch == '\'')
                out += "'\"'\"'";
            else
                out += ch;
        }
        out += "'";
        return out;
    }

    DistributionIO::Spectrum make_zero_nominal_spectrum()
    {
        DistributionIO::Spectrum spectrum;
        spectrum.spec.sample_key = "beam";
        spectrum.spec.branch_expr = "x";
        spectrum.spec.selection_expr = "1";
        spectrum.spec.cache_key = "shape";
        spectrum.spec.nbins = 2;
        spectrum.spec.xmin = 0.0;
        spectrum.spec.xmax = 2.0;

        spectrum.nominal = {0.0, 1.0};
        spectrum.sumw2 = {0.0, 1.0};

        spectrum.detector_source_labels = {"sce"};
        spectrum.detector_sample_keys = {"beam-sce"};
        spectrum.detector_shift_vectors = {1.0, 0.0};
        spectrum.detector_source_count = 1;
        spectrum.detector_covariance = {1.0, 0.0,
                                        0.0, 0.0};
        return spectrum;
    }

    void write_distribution(const std::filesystem::path &path)
    {
        DistributionIO dist(path.string(), DistributionIO::Mode::kWrite);

        DistributionIO::Metadata metadata;
        metadata.eventlist_path = "synthetic.eventlist.root";
        metadata.build_version = 1;
        dist.write_metadata(metadata);
        dist.write("beam", "shape", make_zero_nominal_spectrum());
        dist.flush();
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
            fail("expected <mk_cov binary>");

        const std::string mk_cov_path = argv[1] ? argv[1] : "";
        require(!mk_cov_path.empty(), "mk_cov binary path is required");

        const TempDir temp = make_temp_dir();
        const std::filesystem::path dist_path = temp.path / "zero-nominal.dists.root";
        const std::filesystem::path output_path = temp.path / "zero-nominal.cov.root";
        const std::filesystem::path log_path = temp.path / "mk_cov.log";

        write_distribution(dist_path);

        const std::string command =
            shell_quote(mk_cov_path) + " " +
            shell_quote(dist_path.string()) + " beam " +
            shell_quote(output_path.string()) +
            " >" + shell_quote(log_path.string()) + " 2>&1";
        const int status = std::system(command.c_str());
        if (status == -1)
            fail("failed to launch mk_cov");
        if (status == 0)
            fail("mk_cov should reject zero-nominal fractional covariance export");

        const std::string log = read_text(log_path);
        require(log.find("zero nominal bin prevents fractional covariance export") != std::string::npos,
                "unexpected mk_cov failure log: " + log);

        if (std::filesystem::exists(output_path))
        {
            TFile output(output_path.string().c_str(), "READ");
            if (!output.IsZombie())
            {
                require(output.Get("frac_covariance") == nullptr,
                        "failed mk_cov export should not write frac_covariance");
            }
        }

        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
