#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DistributionIO.hh"

#include <TFile.h>
#include <TH1D.h>
#include <TMatrixT.h>

namespace
{
    struct CliOptions
    {
        std::string input_path;
        std::string sample_key;
        std::string output_path;
        std::string cache_key;
        std::string matrix_name = "frac_covariance";
        std::string nominal_name = "nominal_prediction";
    };

    struct MatrixComponent
    {
        std::string label;
        std::vector<double> absolute;
        bool diagonal_only = false;
    };

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_sbnfit_cov [--cache-key <key>] [--matrix-name <name>] "
              "[--nominal-name <name>] <input.dists.root> <sample-key> <output.root>\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_sbnfit_cov: invalid arguments");
    }

    CliOptions parse_args(int argc, char **argv)
    {
        CliOptions options;

        int i = 1;
        for (; i < argc; ++i)
        {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg == "-h" || arg == "--help")
            {
                print_usage(std::cout);
                throw std::runtime_error("");
            }
            if (arg == "--cache-key")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.cache_key = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--matrix-name")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.matrix_name = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--nominal-name")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.nominal_name = argv[i] ? argv[i] : "";
                continue;
            }
            break;
        }

        if (argc - i != 3)
            print_usage_and_throw();

        options.input_path = argv[i] ? argv[i] : "";
        options.sample_key = argv[i + 1] ? argv[i + 1] : "";
        options.output_path = argv[i + 2] ? argv[i + 2] : "";

        if (options.matrix_name.empty())
            throw std::runtime_error("mk_sbnfit_cov: matrix-name must not be empty");
        if (options.nominal_name.empty())
            throw std::runtime_error("mk_sbnfit_cov: nominal-name must not be empty");
        return options;
    }

    std::string pick_cache_key(const DistributionIO &dist,
                               const std::string &sample_key,
                               const std::string &requested_key)
    {
        if (!requested_key.empty())
        {
            if (!dist.has(sample_key, requested_key))
                throw std::runtime_error("mk_sbnfit_cov: requested cache key is not present for sample");
            return requested_key;
        }

        const std::vector<std::string> keys = dist.dist_keys(sample_key);
        if (keys.empty())
            throw std::runtime_error("mk_sbnfit_cov: no cached distributions found for sample");
        return keys.front();
    }

    std::vector<double> zero_matrix(int nbins)
    {
        return std::vector<double>(static_cast<std::size_t>(nbins * nbins), 0.0);
    }

    void add_matrix_in_place(std::vector<double> &target,
                             const std::vector<double> &source,
                             int nbins,
                             const std::string &context)
    {
        const std::size_t expected = static_cast<std::size_t>(nbins * nbins);
        if (target.size() != expected)
            throw std::runtime_error("mk_sbnfit_cov: target matrix size is invalid for " + context);
        if (source.size() != expected)
            throw std::runtime_error("mk_sbnfit_cov: source matrix size is invalid for " + context);

        for (std::size_t i = 0; i < expected; ++i)
            target[i] += source[i];
    }

    std::vector<double> diagonal_matrix_from_sigma(const std::vector<double> &sigma)
    {
        const int nbins = static_cast<int>(sigma.size());
        std::vector<double> out = zero_matrix(nbins);
        for (int bin = 0; bin < nbins; ++bin)
        {
            const double value = sigma[static_cast<std::size_t>(bin)];
            out[static_cast<std::size_t>(bin * nbins + bin)] = value * value;
        }
        return out;
    }

    std::vector<double> detector_covariance_from_shift_vectors(
        const std::vector<double> &shift_vectors,
        int source_count,
        int nbins)
    {
        if (source_count <= 0 || shift_vectors.empty())
            return {};

        const std::size_t expected = static_cast<std::size_t>(source_count * nbins);
        if (shift_vectors.size() != expected)
            throw std::runtime_error("mk_sbnfit_cov: detector shift payload is truncated");

        std::vector<double> out = zero_matrix(nbins);
        for (int source = 0; source < source_count; ++source)
        {
            for (int row = 0; row < nbins; ++row)
            {
                const double row_shift =
                    shift_vectors[static_cast<std::size_t>(source * nbins + row)];
                for (int col = 0; col < nbins; ++col)
                {
                    const double col_shift =
                        shift_vectors[static_cast<std::size_t>(source * nbins + col)];
                    out[static_cast<std::size_t>(row * nbins + col)] += row_shift * col_shift;
                }
            }
        }
        return out;
    }

    MatrixComponent component_from_detector(const DistributionIO::Spectrum &spectrum)
    {
        MatrixComponent component;
        component.label = "detector";

        const int nbins = spectrum.spec.nbins;
        const std::size_t expected = static_cast<std::size_t>(nbins * nbins);
        if (!spectrum.detector_covariance.empty() &&
            spectrum.detector_covariance.size() != expected)
        {
            throw std::runtime_error("mk_sbnfit_cov: detector covariance payload is truncated");
        }
        if (spectrum.detector_covariance.size() == expected)
        {
            component.absolute = spectrum.detector_covariance;
            return component;
        }

        if (spectrum.detector_source_count > 0 && !spectrum.detector_shift_vectors.empty())
        {
            component.absolute = detector_covariance_from_shift_vectors(
                spectrum.detector_shift_vectors,
                spectrum.detector_source_count,
                nbins);
        }

        return component;
    }

    MatrixComponent component_from_family(const DistributionIO::Family &family,
                                          int nbins,
                                          const std::string &label)
    {
        MatrixComponent component;
        component.label = label;

        const std::size_t expected = static_cast<std::size_t>(nbins * nbins);
        if (!family.covariance.empty() && family.covariance.size() != expected)
            throw std::runtime_error("mk_sbnfit_cov: " + label + " covariance payload is truncated");
        if (family.covariance.size() == expected)
        {
            component.absolute = family.covariance;
            return component;
        }

        if (!family.sigma.empty() &&
            family.sigma.size() != static_cast<std::size_t>(nbins))
        {
            throw std::runtime_error("mk_sbnfit_cov: " + label + " sigma payload is truncated");
        }
        if (family.sigma.size() == static_cast<std::size_t>(nbins))
        {
            component.absolute = diagonal_matrix_from_sigma(family.sigma);
            component.diagonal_only = true;
        }

        return component;
    }

    std::vector<float> fractional_matrix(const std::vector<double> &absolute,
                                         const std::vector<double> &nominal)
    {
        const int nbins = static_cast<int>(nominal.size());
        const std::size_t expected = static_cast<std::size_t>(nbins * nbins);
        if (absolute.size() != expected)
            throw std::runtime_error("mk_sbnfit_cov: absolute covariance size does not match nominal bins");

        std::vector<float> out(expected, 0.0f);
        for (int row = 0; row < nbins; ++row)
        {
            const double row_nominal = nominal[static_cast<std::size_t>(row)];
            for (int col = 0; col < nbins; ++col)
            {
                const double col_nominal = nominal[static_cast<std::size_t>(col)];
                if (row_nominal == 0.0 || col_nominal == 0.0)
                    continue;

                out[static_cast<std::size_t>(row * nbins + col)] =
                    static_cast<float>(absolute[static_cast<std::size_t>(row * nbins + col)] /
                                       (row_nominal * col_nominal));
            }
        }
        return out;
    }

    void fill_matrix(TMatrixT<double> &matrix, const std::vector<double> &values)
    {
        const int nbins = matrix.GetNrows();
        for (int row = 0; row < nbins; ++row)
        {
            for (int col = 0; col < nbins; ++col)
            {
                matrix(row, col) = values[static_cast<std::size_t>(row * nbins + col)];
            }
        }
    }

    void fill_matrix(TMatrixT<float> &matrix, const std::vector<float> &values)
    {
        const int nbins = matrix.GetNrows();
        for (int row = 0; row < nbins; ++row)
        {
            for (int col = 0; col < nbins; ++col)
            {
                matrix(row, col) = values[static_cast<std::size_t>(row * nbins + col)];
            }
        }
    }
}

int main(int argc, char **argv)
{
    try
    {
        const CliOptions options = parse_args(argc, argv);

        DistributionIO dist(options.input_path, DistributionIO::Mode::kRead);
        const std::string cache_key =
            pick_cache_key(dist, options.sample_key, options.cache_key);
        const DistributionIO::Spectrum spectrum = dist.read(options.sample_key, cache_key);

        const int nbins = spectrum.spec.nbins;
        if (nbins <= 0)
            throw std::runtime_error("mk_sbnfit_cov: cached histogram nbins must be positive");
        if (spectrum.nominal.size() != static_cast<std::size_t>(nbins))
            throw std::runtime_error("mk_sbnfit_cov: nominal payload size does not match cached nbins");

        std::vector<MatrixComponent> components;
        components.push_back(component_from_detector(spectrum));
        components.push_back(component_from_family(spectrum.genie, nbins, "genie"));
        components.push_back(component_from_family(spectrum.flux, nbins, "flux"));
        components.push_back(component_from_family(spectrum.reint, nbins, "reint"));

        std::vector<double> total_absolute = zero_matrix(nbins);
        std::vector<std::string> included_components;
        std::vector<std::string> approximate_components;
        for (const auto &component : components)
        {
            if (component.absolute.empty())
                continue;

            add_matrix_in_place(total_absolute, component.absolute, nbins, component.label);
            included_components.push_back(component.label);
            if (component.diagonal_only)
                approximate_components.push_back(component.label);
        }

        const std::vector<float> total_fractional =
            fractional_matrix(total_absolute, spectrum.nominal);

        TFile output(options.output_path.c_str(), "RECREATE");
        if (output.IsZombie())
            throw std::runtime_error("mk_sbnfit_cov: failed to open output ROOT file");

        TH1D nominal_hist(options.nominal_name.c_str(),
                          options.nominal_name.c_str(),
                          nbins,
                          spectrum.spec.xmin,
                          spectrum.spec.xmax);
        nominal_hist.SetDirectory(nullptr);
        for (int bin = 0; bin < nbins; ++bin)
            nominal_hist.SetBinContent(bin + 1, spectrum.nominal[static_cast<std::size_t>(bin)]);
        nominal_hist.Write(options.nominal_name.c_str());

        TMatrixT<float> frac_covariance(nbins, nbins);
        fill_matrix(frac_covariance, total_fractional);
        frac_covariance.Write(options.matrix_name.c_str());

        TMatrixT<double> abs_covariance(nbins, nbins);
        fill_matrix(abs_covariance, total_absolute);
        abs_covariance.Write("abs_covariance");

        for (const auto &component : components)
        {
            if (component.absolute.empty())
                continue;

            const std::vector<float> component_fractional =
                fractional_matrix(component.absolute, spectrum.nominal);

            TMatrixT<double> component_absolute_matrix(nbins, nbins);
            fill_matrix(component_absolute_matrix, component.absolute);
            component_absolute_matrix.Write((component.label + "_covariance").c_str());

            TMatrixT<float> component_fractional_matrix(nbins, nbins);
            fill_matrix(component_fractional_matrix, component_fractional);
            component_fractional_matrix.Write((component.label + "_frac_covariance").c_str());
        }

        output.Write();
        output.Close();

        std::cout << "mk_sbnfit_cov: wrote " << options.output_path
                  << " from " << options.input_path
                  << " sample " << options.sample_key
                  << " cache " << cache_key
                  << " as " << options.matrix_name << "\n";
        if (!included_components.empty())
        {
            std::cout << "mk_sbnfit_cov: included components:";
            for (const auto &label : included_components)
                std::cout << " " << label;
            std::cout << "\n";
        }
        else
        {
            std::cout << "mk_sbnfit_cov: no systematic covariance payloads were present; wrote zero covariance matrices\n";
        }
        if (!approximate_components.empty())
        {
            std::cout << "mk_sbnfit_cov: diagonal-only fallback used for:";
            for (const auto &label : approximate_components)
                std::cout << " " << label;
            std::cout << "\n";
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        if (*e.what() != '\0')
            std::cerr << e.what() << "\n";
        return (*e.what() == '\0') ? 0 : 1;
    }
}
