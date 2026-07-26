#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "DistributionIO.hh"

#include "TFile.h"
#include "TMatrixT.h"

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

    template <class TObjectType>
    TObjectType *must_get(TFile &input, const std::string &name)
    {
        TObject *object = input.Get(name.c_str());
        if (!object)
            fail("missing object: " + name);
        auto *typed = dynamic_cast<TObjectType *>(object);
        if (!typed)
            fail("unexpected object type: " + name);
        return typed;
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

    DistributionIO::Spectrum make_zero_nominal_spectrum(double zero_bin_covariance)
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
        spectrum.detector_source_count = 1;
        spectrum.detector_covariance = {zero_bin_covariance, 0.0,
                                        0.0, 1.0};
        return spectrum;
    }

    void write_distribution(const std::filesystem::path &path,
                            double zero_bin_covariance)
    {
        DistributionIO dist(path.string(), DistributionIO::Mode::kWrite);

        DistributionIO::Metadata metadata;
        metadata.eventlist_path = "synthetic.eventlist.root";
        metadata.build_version = 1;
        dist.write_metadata(metadata);
        dist.write("beam", "shape", make_zero_nominal_spectrum(zero_bin_covariance));
        dist.flush();
    }

    int run_mk_cov(const std::string &mk_cov_path,
                   const std::filesystem::path &dist_path,
                   const std::filesystem::path &output_path,
                   const std::filesystem::path &log_path)
    {
        const std::string command =
            shell_quote(mk_cov_path) + " " +
            shell_quote(dist_path.string()) + " beam " +
            shell_quote(output_path.string()) +
            " >" + shell_quote(log_path.string()) + " 2>&1";
        const int status = std::system(command.c_str());
        if (status == -1)
            fail("failed to launch mk_cov");
        return status;
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
        const std::filesystem::path rejected_dist_path =
            temp.path / "zero-nominal-rejected.dists.root";
        const std::filesystem::path rejected_output_path =
            temp.path / "zero-nominal-rejected.cov.root";
        const std::filesystem::path rejected_log_path =
            temp.path / "zero-nominal-rejected.log";

        write_distribution(rejected_dist_path, 1.0);
        if (run_mk_cov(mk_cov_path,
                       rejected_dist_path,
                       rejected_output_path,
                       rejected_log_path) == 0)
        {
            fail("mk_cov should reject zero-nominal fractional covariance export");
        }

        const std::string log = read_text(rejected_log_path);
        require(log.find("zero nominal bin prevents fractional covariance export") != std::string::npos,
                "unexpected mk_cov failure log: " + log);
        require(!std::filesystem::exists(rejected_output_path),
                "failed mk_cov export should not create an output file");

        const std::string original_output = "existing output must survive\n";
        {
            std::ofstream output(rejected_output_path);
            require(static_cast<bool>(output), "failed to create existing output sentinel");
            output << original_output;
        }
        require(run_mk_cov(mk_cov_path,
                           rejected_dist_path,
                           rejected_output_path,
                           rejected_log_path) != 0,
                "mk_cov should still reject export over an existing output");
        require(read_text(rejected_output_path) == original_output,
                "failed mk_cov export should preserve an existing output file");

        const std::filesystem::path roundoff_dist_path =
            temp.path / "zero-nominal-roundoff.dists.root";
        const std::filesystem::path roundoff_output_path =
            temp.path / "zero-nominal-roundoff.cov.root";
        const std::filesystem::path roundoff_log_path =
            temp.path / "zero-nominal-roundoff.log";

        write_distribution(roundoff_dist_path, 1e-16);
        const int roundoff_status = run_mk_cov(mk_cov_path,
                                               roundoff_dist_path,
                                               roundoff_output_path,
                                               roundoff_log_path);
        if (roundoff_status != 0)
        {
            fail("mk_cov should accept scale-level covariance roundoff: " +
                 read_text(roundoff_log_path));
        }

        TFile output(roundoff_output_path.string().c_str(), "READ");
        require(!output.IsZombie(), "failed to open roundoff export");
        auto *fractional = must_get<TMatrixT<float>>(output, "frac_covariance");
        auto *absolute = must_get<TMatrixT<double>>(output, "abs_covariance");
        auto *detector_fractional =
            must_get<TMatrixT<float>>(output, "detector_frac_covariance");
        require(fractional->GetNrows() == 2 && fractional->GetNcols() == 2,
                "roundoff export dimensions should match the cached spectrum");
        require(std::fabs((*fractional)(0, 0)) < 1e-12,
                "unsupported roundoff entry should export as zero fractional covariance");
        require(std::fabs((*fractional)(1, 1) - 1.0f) < 1e-6,
                "supported fractional covariance entry should be preserved");
        require(std::fabs((*detector_fractional)(0, 0)) < 1e-12,
                "component roundoff entry should export as zero fractional covariance");
        require(std::fabs((*absolute)(0, 0) - 1e-16) < 1e-28,
                "absolute covariance roundoff entry should remain available");

        const std::filesystem::path non_finite_dist_path =
            temp.path / "non-finite.dists.root";
        const std::filesystem::path non_finite_output_path =
            temp.path / "non-finite.cov.root";
        const std::filesystem::path non_finite_log_path =
            temp.path / "non-finite.log";

        write_distribution(non_finite_dist_path,
                           std::numeric_limits<double>::quiet_NaN());
        require(run_mk_cov(mk_cov_path,
                           non_finite_dist_path,
                           non_finite_output_path,
                           non_finite_log_path) != 0,
                "mk_cov should reject non-finite covariance values");
        require(read_text(non_finite_log_path).find("non-finite value") != std::string::npos,
                "non-finite covariance rejection should explain the failure");
        require(!std::filesystem::exists(non_finite_output_path),
                "non-finite covariance rejection should not create an output file");

        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
